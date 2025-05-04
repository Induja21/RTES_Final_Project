#include "ImageProcessing.hpp"
#include <linux/videodev2.h>
#include <iostream>
#include <zmq.hpp>

using namespace cv;
using namespace std;

extern zmq::socket_t zmq_sub_socket_face; // ZMQ subscriber socket for frame input
extern zmq::socket_t zmq_push_face_socket; // ZMQ push socket to send face center

int detectiontype = 0;
vector<Point> centers;
Point track_Eyeball;
using namespace cv;
using namespace std;

static bool initialized = false;
static CascadeClassifier faceCascade;
static CascadeClassifier eyeCascade;

void initImageProcessingService(int type)
{
    detectiontype = type;
	if(detectiontype==1)
	{
		if (!faceCascade.load("/usr/share/opencv4/haarcascades/haarcascade_frontalface_alt.xml")) {
            cerr << "Failed to load face cascade classifier" << endl;
            return;
        }

        initialized = true;
	}
	else if(detectiontype ==2)
	{
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

void eyeCenterDetection(Mat& frame, CascadeClassifier& faceCascade, CascadeClassifier& eyeCascade, Point& eyeCenter) {
    cv::Mat grayImage;
    cv::cvtColor(frame, grayImage, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(grayImage, grayImage);

    // Detect faces
    std::vector<cv::Rect> faces;
    float scaleFactor = 1.1;
    int minimumNeighbour = 2;
    cv::Size minImageSize = cv::Size(150, 150);
    faceCascade.detectMultiScale(grayImage, faces, scaleFactor, minimumNeighbour, 0 | cv::CASCADE_SCALE_IMAGE, minImageSize);

    if (faces.empty()) {
        eyeCenter = cv::Point(-1, -1); // No face detected
        return;
    }
    
    Mat grayface = grayImage(faces[0]);
    
    // Detect eyes
    vector<Rect> eyes;
    float eyeScaleFactor = 1.1;
    int eyeMinimumNeighbour = 2;
    Size eyeMinImageSize = Size(30, 30);
    eyeCascade.detectMultiScale(grayface, eyes, eyeScaleFactor, eyeMinimumNeighbour, 0 | CASCADE_SCALE_IMAGE, eyeMinImageSize);
    if (eyes.size() != 2) return;

    for (Rect& eye : eyes) {
        rectangle(frame, faces[0].tl() + eye.tl(), faces[0].tl() + eye.br(), Scalar(0, 255, 0), 2);
    }
    
    Rect eyeRect = detectLeftEye(eyes);
    Mat eye = grayface(eyeRect);
    equalizeHist(eye, eye);
    
    vector<Vec3f> circles;
    int method = 3;
    int detect_Pixel = 1;
    int minimum_Distance = eye.cols / 8;
    int threshold = 250;
    int minimum_Area = 15;
    int minimum_Radius = eye.rows / 6;
    int maximum_Radius = eye.rows / 2;
    HoughCircles(eye, circles, HOUGH_GRADIENT, detect_Pixel, minimum_Distance, threshold, minimum_Area, minimum_Radius, maximum_Radius);
    
    if (circles.size()>0) {
        Vec3f eyeball = eyeBallDetection(eye, circles);
      
        cv::Point eyeCenters = cv::Point(cvRound(eyeball[0]), cvRound(eyeball[1]));
        centers.push_back(eyeCenters);
        eyeCenters = makeStable(centers, 5);
        track_Eyeball = eyeCenter;
        int radius = (int)eyeball[2];
        //eyeCenter = faces[0].tl() + eyeRect.tl() + eyeCenters;
        eyeCenter = eyeCenters;
        
    }
    
    
}

void eyeCenterDetectionService() {

    
    // Initialize face and eye classifier
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


        Point eyeCenter;
        eyeCenterDetection(frame, faceCascade,eyeCascade, eyeCenter);


        // Send face center coordinates via ZeroMQ
        if (eyeCenter.x >= 0 && eyeCenter.y >= 0) {
            char buffer[50];
            snprintf(buffer, sizeof(buffer), "Center:%d,%d", eyeCenter.x, eyeCenter.y);
            zmq::message_t msg(buffer, strlen(buffer));
            zmq_push_face_socket.send(msg, zmq::send_flags::dontwait);
        }

        // imshow("Webcam", frame);
        // if (waitKey(30) >= 0) break;
    }
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
}

void faceCenterDetectionService() {

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


        Point faceCenter;
        faceCenterDetection(frame, faceCascade, faceCenter);


        // Send face center coordinates via ZeroMQ
        if (faceCenter.x >= 0 && faceCenter.y >= 0) {
            char buffer[50];
            snprintf(buffer, sizeof(buffer), "Center:%d,%d", faceCenter.x, faceCenter.y);
            zmq::message_t msg(buffer, strlen(buffer));
            zmq_push_face_socket.send(msg, zmq::send_flags::dontwait);
        }

        // imshow("Webcam", frame);
        // if (waitKey(30) >= 0) break;
    }
}

void DetectionService()
{
    if(detectiontype == 1)
    {
        faceCenterDetectionService();
    }
    else if(detectiontype == 2)
    {
        eyeCenterDetectionService();
    }
}
