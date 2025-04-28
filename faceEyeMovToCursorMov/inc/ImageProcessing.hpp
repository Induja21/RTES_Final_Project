#ifndef EYE_DETECTION_HPP
#define EYE_DETECTION_HPP

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/objdetect/objdetect.hpp>
#include "MessageQueue.hpp"
#include <vector>
#include <string>
#include <zmq.hpp>

#define NSEC_PER_SEC (1000000000)
#define NSEC_PER_MSEC (1000000)
#define NSEC_PER_MICROSEC (1000)

void faceCenterDetectionService();

#endif // EYE_DETECTION_HPP