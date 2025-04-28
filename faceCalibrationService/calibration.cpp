#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>

#define CAMERA_X 640
#define CAMERA_Y 480
#define CASCADE_PATH "/usr/share/opencv4/haarcascades/haarcascade_frontalface_alt.xml"

void detectFaceCenter(cv::Mat& frame, cv::CascadeClassifier& faceCascade, cv::Point& faceCenter) {
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
        faceCenter = cv::Point(-1, -1); // No face detected
        return;
    }

    // Use the first detected face
    cv::Rect faceRect = faces[0];
    faceCenter = cv::Point(faceRect.x + faceRect.width / 2, faceRect.y + faceRect.height / 2);

    // Draw rectangle and center for feedback
    cv::rectangle(frame, faceRect, cv::Scalar(255, 0, 255), 2);
    cv::circle(frame, faceCenter, faceRect.width / 8, cv::Scalar(0, 0, 255), 2);
}

bool captureFaceCenter(cv::VideoCapture& cap, cv::CascadeClassifier& faceCascade, int& x, int& y, const std::string& position) {
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

        // Detect face
        cv::Point faceCenter;
        detectFaceCenter(frame, faceCascade, faceCenter);

        // Display frame with face detection
        if (faceCenter.x >= 0 && faceCenter.y >= 0) {
            std::string coord_text = "x=" + std::to_string(faceCenter.x) + ", y=" + std::to_string(faceCenter.y);
            cv::putText(frame, coord_text, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
        } else {
            cv::putText(frame, "No face detected", cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 2);
        }
        cv::imshow("Calibration", frame);

        // Check for keypress
        int key = cv::waitKey(100);
        if (key == 13) { // Enter key
            if (faceCenter.x >= 0 && faceCenter.x <= CAMERA_X && faceCenter.y >= 0 && faceCenter.y <= CAMERA_Y) {
                x = faceCenter.x;
                y = faceCenter.y;
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
    if (!faceCascade.load(CASCADE_PATH)) {
        std::cerr << "Failed to load face cascade: " << CASCADE_PATH << "\n";
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

    std::cout << "Starting head movement calibration.\n";
    std::cout << "Ensure your camera is working and your face is clearly visible.\n";
    std::cout << "A window will show your face with a bounding box. Position your head as instructed.\n\n";

    // Capture each position
    captureFaceCenter(cap, faceCascade, straight_x, straight_y, "Straight facing");
    captureFaceCenter(cap, faceCascade, left_x, top_y, "Max left turn"); // Reuse top_y temporarily
    captureFaceCenter(cap, faceCascade, right_x, top_y, "Max right turn"); // Reuse top_y temporarily
    captureFaceCenter(cap, faceCascade, straight_x, top_y, "Max up"); // Reuse straight_x
    captureFaceCenter(cap, faceCascade, straight_x, bottom_y, "Max down"); // Reuse straight_x

    // Save calibration data to file
    std::ofstream file("../faceEyeMovToCursorMov/calibration_face.csv");
    if (!file.is_open()) {
        std::cerr << "Failed to open calibration_face.csv for writing.\n";
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