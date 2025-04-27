#include "ImageProcessing.hpp"
#include <linux/videodev2.h>
#include <iostream>
#include <zmq.hpp>

using namespace cv;
using namespace std;

extern zmq::socket_t zmq_sub_socket_face; // ZMQ subscriber socket for frame input
extern zmq::socket_t zmq_push_face_socket; // ZMQ push socket to send face center

#define NSEC_PER_MSEC 1000000
#define NSEC_PER_MICROSEC 1000

int delta_t(struct timespec *stop, struct timespec *start, struct timespec *delta_t) {
    int dt_sec = stop->tv_sec - start->tv_sec;
    int dt_nsec = stop->tv_nsec - start->tv_nsec;

    if (dt_sec >= 0) {
        if (dt_nsec >= 0) {
            delta_t->tv_sec = dt_sec;
            delta_t->tv_nsec = dt_nsec;
        } else {
            delta_t->tv_sec = dt_sec - 1;
            delta_t->tv_nsec = NSEC_PER_SEC + dt_nsec;
        }
    } else {
        if (dt_nsec >= 0) {
            delta_t->tv_sec = dt_sec;
            delta_t->tv_nsec = dt_nsec;
        } else {
            delta_t->tv_sec = dt_sec - 1;
            delta_t->tv_nsec = NSEC_PER_SEC + dt_nsec;
        }
    }
    return 1;
}

void faceCenterDetection(Mat& frame, CascadeClassifier& faceCascade, Point& faceCenter) {
    Mat grayImage;
    cvtColor(frame, grayImage, COLOR_BGR2GRAY);
    equalizeHist(grayImage, grayImage);

    // Detect faces
    vector<Rect> storedFaces;
    float scaleFactor = 1.1;
    int minimumNeighbour = 2;
    Size minImageSize = Size(150, 150);
    faceCascade.detectMultiScale(grayImage, storedFaces, scaleFactor, minimumNeighbour, 0 | CASCADE_SCALE_IMAGE, minImageSize);
    if (storedFaces.empty()) {
        faceCenter = Point(-1, -1); // Indicate no face detected
        return;
    }

    // Process the first detected face
    Rect faceRect = storedFaces[0];
    int x = faceRect.x;
    int y = faceRect.y;
    int h = y + faceRect.height;
    int w = x + faceRect.width;
    rectangle(frame, Point(x, y), Point(w, h), Scalar(255, 0, 255), 2, 8, 0);

    // Calculate the center of the face
    faceCenter = Point(faceRect.x + faceRect.width / 2, faceRect.y + faceRect.height / 2);

    // Draw a circle at the center of the face
    int radius = faceRect.width / 8;
    circle(frame, faceCenter, radius, Scalar(0, 0, 255), 2);

    cout << "Raw face center: x=" << faceCenter.x << ", y=" << faceCenter.y << endl;
}

void faceCenterDetectionService() {
    static bool initialized = false;
    static CascadeClassifier faceCascade;

    // Initialize face classifier
    if (!initialized) {
        if (!faceCascade.load("/usr/share/opencv4/haarcascades/haarcascade_frontalface_alt.xml")) {
            cerr << "Failed to load face cascade classifier" << endl;
            return;
        }
        initialized = true;
    }

    // Non-blocking receive loop
    while (true) {
        // Receive the metadata (first part of the multi-part message)
        zmq::message_t metadata_msg;
        if (!zmq_sub_socket_face.recv(metadata_msg, zmq::recv_flags::dontwait)) {
            break; // No message available, exit loop
        }

        // Ensure the second part (frame data) is available
        if (!metadata_msg.more()) {
            cerr << "Missing frame data part in ZMQ message" << endl;
            continue;
        }

        // Extract metadata
        struct FrameMetadata {
            int width;
            int height;
            uint32_t format;
            size_t data_size;
        };
        if (metadata_msg.size() != sizeof(FrameMetadata)) {
            cerr << "Invalid metadata size received" << endl;
            continue;
        }
        FrameMetadata metadata;
        memcpy(&metadata, metadata_msg.data(), sizeof(FrameMetadata));

        // Verify format
        if (metadata.format != V4L2_PIX_FMT_YUYV) {
            cerr << "Unsupported frame format: " << metadata.format << endl;
            continue;
        }

        // Log camera resolution
        cout << "Camera resolution: width=" << metadata.width << ", height=" << metadata.height << endl;

        // Receive the raw frame data (second part)
        zmq::message_t frame_msg;
        if (!zmq_sub_socket_face.recv(frame_msg, zmq::recv_flags::dontwait)) {
            cerr << "Failed to receive frame data" << endl;
            continue;
        }

        if (frame_msg.size() != metadata.data_size) {
            cerr << "Frame data size mismatch: expected " << metadata.data_size
                 << ", received " << frame_msg.size() << endl;
            continue;
        }

        // Create a cv::Mat from the raw YUYV data
        Mat yuyv(metadata.height, metadata.width, CV_8UC2, frame_msg.data());
        Mat frame;
        try {
            cvtColor(yuyv, frame, COLOR_YUV2BGR_YUYV);
        } catch (const cv::Exception& e) {
            cerr << "Color conversion failed: " << e.what() << endl;
            continue;
        }
        if (frame.empty()) {
            cerr << "Failed to convert frame to BGR" << endl;
            continue;
        }

        // Perform face center detection
        struct timespec start_time = {0, 0};
        struct timespec finish_time = {0, 0};
        struct timespec thread_dt = {0, 0};

        clock_gettime(CLOCK_MONOTONIC, &start_time);
        Point faceCenter;
        faceCenterDetection(frame, faceCascade, faceCenter);
        clock_gettime(CLOCK_MONOTONIC, &finish_time);
        delta_t(&finish_time, &start_time, &thread_dt);

        // Send face center coordinates via ZeroMQ
        if (faceCenter.x >= 0 && faceCenter.y >= 0) {
            char buffer[50];
            snprintf(buffer, sizeof(buffer), "FaceCenter:%d,%d", faceCenter.x, faceCenter.y);
            zmq::message_t msg(buffer, strlen(buffer));
            zmq_push_face_socket.send(msg, zmq::send_flags::dontwait);
        }

        char buffer[100];
        sprintf(buffer, "Timing for face center detection: %ld sec, %ld msec, %ld usec\n",
                thread_dt.tv_sec, (thread_dt.tv_nsec / NSEC_PER_MSEC), (thread_dt.tv_nsec / NSEC_PER_MICROSEC));
        puts(buffer);
        // imshow("Webcam", frame);
        // if (waitKey(30) >= 0) break;
    }
}