#include "ImageProcessing.hpp"
#include <linux/videodev2.h>
#include <iostream>

using namespace cv;
using namespace std;

extern zmq::socket_t zmq_sub_socket_eye; // Changed to sub socket

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

Vec3f eyeBallDetection(Mat& eye, vector<Vec3f>& circles) {
    vector<int> sums(circles.size(), 0);
    for (int y = 0; y < eye.rows; y++) {
        uchar* data = eye.ptr<uchar>(y);
        for (int x = 0; x < eye.cols; x++) {
            int pixel_value = static_cast<int>(*data);
            for (int i = 0; i < circles.size(); i++) {
                Point center((int)round(circles[i][0]), (int)round(circles[i][1]));
                int radius = (int)round(circles[i][2]);
                if (pow(x - center.x, 2) + pow(y - center.y, 2) < pow(radius, 2)) {
                    sums[i] += pixel_value;
                }
            }
            ++data;
        }
    }
    int smallestSum = 9999999;
    int smallestSumIndex = -1;
    for (int i = 0; i < circles.size(); i++) {
        if (sums[i] < smallestSum) {
            smallestSum = sums[i];
            smallestSumIndex = i;
        }
    }
    return circles[smallestSumIndex];
}

Rect detectLeftEye(vector<Rect>& eyes) {
    int leftEye = 99999999;
    int index = 0;
    for (int i = 0; i < eyes.size(); i++) {
        if (eyes[i].tl().x < leftEye) {
            leftEye = eyes[i].tl().x;
            index = i;
        }
    }
    return eyes[index];
}

vector<Point> centers;
Point track_Eyeball;
Point makeStable(vector<Point>& points, int iteration) {
    float sum_of_X = 0, sum_of_Y = 0;
    int count = 0;
    int j = max(0, (int)(points.size() - iteration));
    int number_of_points = points.size();
    for (; j < number_of_points; j++) {
        sum_of_X += points[j].x;
        sum_of_Y += points[j].y;
        ++count;
    }
    if (count > 0) {
        sum_of_X /= count;
        sum_of_Y /= count;
    }
    return Point(sum_of_X, sum_of_Y);
}

void eyeDetection(Mat& frame, CascadeClassifier& faceCascade, CascadeClassifier& eyeCascade) {
    Mat grayImage;
    cvtColor(frame, grayImage, COLOR_BGR2GRAY);
    equalizeHist(grayImage, grayImage);

    // Detect faces
    Mat inputImage = grayImage;
    vector<Rect> storedFaces;
    float scaleFactor = 1.1;
    int minimumNeighbour = 2;
    Size minImageSize = Size(150, 150);
    faceCascade.detectMultiScale(inputImage, storedFaces, scaleFactor, minimumNeighbour, 0 | CASCADE_SCALE_IMAGE, minImageSize);
    if (storedFaces.empty()) return;

    Mat face = grayImage(storedFaces[0]);
    int x = storedFaces[0].x;
    int y = storedFaces[0].y;
    int h = y + storedFaces[0].height;
    int w = x + storedFaces[0].width;
    rectangle(frame, Point(x, y), Point(w, h), Scalar(255, 0, 255), 2, 8, 0);

    // Detect eyes
    vector<Rect> eyes;
    float eyeScaleFactor = 1.1;
    int eyeMinimumNeighbour = 2;
    Size eyeMinImageSize = Size(30, 30);
    eyeCascade.detectMultiScale(face, eyes, eyeScaleFactor, eyeMinimumNeighbour, 0 | CASCADE_SCALE_IMAGE, eyeMinImageSize);
    if (eyes.size() != 2) return;

    for (Rect& eye : eyes) {
        rectangle(frame, storedFaces[0].tl() + eye.tl(), storedFaces[0].tl() + eye.br(), Scalar(0, 255, 0), 2);
    }

    // Detect left eye and eyeball
    Rect eyeRect = detectLeftEye(eyes);
    Mat eye = face(eyeRect);
    equalizeHist(eye, eye);

    vector<Vec3f> circles;
    int method = 3;
    int detect_Pixel = 1;
    int minimum_Distance = eye.cols / 8;
    int threshold = 250;
    int minimum_Area = 15;
    int minimum_Radius = eye.rows / 8;
    int maximum_Radius = eye.rows / 3;
    HoughCircles(eye, circles, HOUGH_GRADIENT, detect_Pixel, minimum_Distance, threshold, minimum_Area, minimum_Radius, maximum_Radius);

    if (circles.size()>0) {
        Vec3f eyeball = eyeBallDetection(eye, circles);
        Point center(eyeball[0], eyeball[1]);
        centers.push_back(center);
        center = makeStable(centers, 5);
        track_Eyeball = center;
        int radius = (int)eyeball[2];
        circle(frame, storedFaces[0].tl() + eyeRect.tl() + center, radius, Scalar(0, 0, 255), 2);
        circle(eye, center, radius, Scalar(255, 255, 255), 2);
        cout << "Eyeball location: " << track_Eyeball << endl;
    }


}

void eyeDetectionService() {
    static bool initialized = false;
    static CascadeClassifier faceCascade;
    static CascadeClassifier eyeCascade;

    // Initialize classifiers
    if (!initialized) {
        if (!faceCascade.load("/usr/share/opencv4/haarcascades/haarcascade_frontalface_alt.xml")) {
            cerr << "Failed to load face cascade classifier" << endl;
            return;
        }
        if (!eyeCascade.load("/usr/share/opencv4/haarcascades/haarcascade_eye.xml")) {
            cerr << "Failed to load eye cascade classifier" << endl;
            return;
        }
        initialized = true;
    }

    // Non-blocking receive loop
    while (true) {
        // Receive the metadata (first part of the multi-part message)
        zmq::message_t metadata_msg;
        if (!zmq_sub_socket_eye.recv(metadata_msg, zmq::recv_flags::dontwait)) {
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

        // Receive the raw frame data (second part)
        zmq::message_t frame_msg;
        if (!zmq_sub_socket_eye.recv(frame_msg, zmq::recv_flags::dontwait)) {
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

        // Perform eye detection
        struct timespec start_time = {0, 0};
        struct timespec finish_time = {0, 0};
        struct timespec thread_dt = {0, 0};

        clock_gettime(CLOCK_MONOTONIC, &start_time);
        eyeDetection(frame, faceCascade, eyeCascade);
        clock_gettime(CLOCK_MONOTONIC, &finish_time);
        delta_t(&finish_time, &start_time, &thread_dt);

        char buffer[100];
        sprintf(buffer, "Timing for eye detection: %ld sec, %ld msec, %ld usec\n",
                thread_dt.tv_sec, (thread_dt.tv_nsec / NSEC_PER_MSEC), (thread_dt.tv_nsec / NSEC_PER_MICROSEC));
        puts(buffer);
    }
}