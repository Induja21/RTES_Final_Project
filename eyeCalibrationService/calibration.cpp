#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/objdetect/objdetect.hpp>
#include <linux/videodev2.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>

#define CAMERA_X 640
#define CAMERA_Y 480
#define CASCADE_PATH "/usr/share/opencv4/haarcascades/haarcascade_frontalface_alt.xml"

using namespace cv;
using namespace std;

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

void detectEyeCenter(cv::Mat& frame, cv::CascadeClassifier& faceCascade, cv::CascadeClassifier& eyeCascade, cv::Point& eyeCenter) {
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
        circle(frame, faces[0].tl() + eyeRect.tl() + eyeCenters, radius, Scalar(0, 0, 255), 2);
        circle(eye, eyeCenters, radius, Scalar(255, 255, 255), 2);
        
    }
}

bool captureEyeCenter(cv::VideoCapture& cap, cv::CascadeClassifier& faceCascade , cv::CascadeClassifier& eyeCascade, int& x, int& y, const std::string& position) {
    std::cout << "Position: " << position << "\n";
    std::cout << "Press Enter when ready to capture this position (press 'q' to skip)...\n";

    cv::namedWindow("Calibration", cv::WINDOW_AUTOSIZE);
    bool captured = false;
    const int max_attempts = 50; // Try for 5 seconds (100ms per frame)

    while (true) {
        cv::Mat frame;
        if (!cap.read(frame)) {
            std::cerr << "Failed to capture frame.\n";
            break;
        }
        if (frame.empty()) {
            std::cerr << "Empty frame captured.\n";
            break;
        }

        // Detect eye
        cv::Point eyeCenter;
        detectEyeCenter(frame, faceCascade,eyeCascade, eyeCenter);

        // Display frame with face detection
        if (eyeCenter.x >= 0 && eyeCenter.y >= 0) {
            std::string coord_text = "x=" + std::to_string(eyeCenter.x) + ", y=" + std::to_string(eyeCenter.y);
            cv::putText(frame, coord_text, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
        } else {
            cv::putText(frame, "No face detected", cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 2);
        }
        cv::imshow("Calibration", frame);

        // Check for keypress
        int key = cv::waitKey(100);
        if (key == 13 || key == 10) { // Enter key
            if (eyeCenter.x >= 0 && eyeCenter.x <= CAMERA_X && eyeCenter.y >= 0 && eyeCenter.y <= CAMERA_Y) {
                x = eyeCenter.x;
                y = eyeCenter.y;
                std::cout << "Captured: x=" << x << ", y=" << y << "\n";
                captured = true;
                break;
            } else {
                std::cerr << "No face detected. Please ensure your face is visible and try again.\n";
            }
        } else if (key == 'q' || key == 'Q') {
            std::cout << "Skipping position. Using default (320, 240).\n";
            x = 320;
            y = 240;
            captured = true;
            break;
        }
    }

    cv::destroyWindow("Calibration");
    return captured;
}

int main(int argc, char* argv[]) {
    // Initialize face cascade
     cv::CascadeClassifier faceCascade;
     cv::CascadeClassifier eyeCascade;
 
    // Initialize classifiers

    if (!faceCascade.load("/usr/share/opencv4/haarcascades/haarcascade_frontalface_alt.xml")) {
        cerr << "Failed to load face cascade classifier" << endl;
        return 1;
    }
    if (!eyeCascade.load("/usr/share/opencv4/haarcascades/haarcascade_eye.xml")) {
        cerr << "Failed to load eye cascade classifier" << endl;
        return 1;
    }

    

    // Open camera
    cv::VideoCapture cap(0); // Default camera (/dev/video0)
    if (!cap.isOpened()) {
        std::cerr << "Failed to open camera.\n";
        return 1;
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, CAMERA_X);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, CAMERA_Y);

    // Calibration data
    int straight_x, straight_y;
    int left_x, right_x;
    int top_y, bottom_y;

    std::cout << "Starting eye movement calibration.\n";
    std::cout << "Ensure your camera is working and your face is clearly visible.\n";
    std::cout << "A window will show your face with a bounding box. Position your head as instructed.\n\n";

    // Capture each position
    captureEyeCenter(cap, faceCascade,eyeCascade, straight_x, straight_y, "Straight facing");
    captureEyeCenter(cap, faceCascade,eyeCascade, left_x, top_y, "Max left turn"); // Reuse top_y temporarily
    captureEyeCenter(cap, faceCascade,eyeCascade, right_x, top_y, "Max right turn"); // Reuse top_y temporarily
    captureEyeCenter(cap, faceCascade,eyeCascade, straight_x, top_y, "Max up"); // Reuse straight_x
    captureEyeCenter(cap, faceCascade,eyeCascade, straight_x, bottom_y, "Max down"); // Reuse straight_x

    // Save calibration data to file
    std::ofstream file("../faceEyeMovToCursorMov/calibration_eye.csv");
    if (!file.is_open()) {
        std::cerr << "Failed to open calibration_eye.csv for writing.\n";
        return 1;
    }

    file << "straight_x,straight_y,left_x,right_x,top_y,bottom_y\n";
    file << straight_x << "," << straight_y << ","
         << left_x << "," << right_x << ","
         << top_y << "," << bottom_y << "\n";
    file.close();

    std::cout << "Calibration complete. Data saved to calibration_face.csv:\n";
    std::cout << "Straight: (" << straight_x << "," << straight_y << ")\n";
    std::cout << "Left: x=" << left_x << "\n";
    std::cout << "Right: x=" << right_x << "\n";
    std::cout << "Top: y=" << top_y << "\n";
    std::cout << "Bottom: y=" << bottom_y << "\n";

    // Release camera
    cap.release();
    return 0;
}
