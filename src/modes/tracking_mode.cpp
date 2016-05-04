#include "modes/tracking_mode.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <fstream>

#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/video/tracking.hpp>

#include "utils/draw_utils.hpp"
#include "tracker/kalman_tracker.hpp"
#include "tracker/multi_object_tracker.hpp"
#include "tracker/contour_finder.hpp"
#include "tracker/tracker_log.hpp"
#include "lib/cmdparser.hpp"
#include "utils/utils.hpp"
#include "utils/perspective_transformer.hpp"

namespace OT {
    namespace Mode {
        namespace Tracking {
            // The mouse callback to allow the user to draw a rectangle on the screen.
            bool isDragging = false;
            bool hasRectangle = false;
            bool triggerCallback = false;
            cv::Point point1, point2;
            
            void mouseHandler(int event, int x, int y, int flags, void* param) {
                if (event == CV_EVENT_LBUTTONDOWN && !isDragging) {
                    point1 = cv::Point(x, y);
                    std::cout << "Clicked " << point1 << std::endl;
                    isDragging = true;
                } else if (event == CV_EVENT_MOUSEMOVE && isDragging) {
                    point2 = cv::Point(x, y);
                    hasRectangle = true;
                } else if (event == CV_EVENT_LBUTTONUP && isDragging) {
                    point2 = cv::Point(x, y);
                    isDragging = false;
                    triggerCallback = true;
                    hasRectangle = false;
                }
            }
            
            
            
            void run(const cli::Parser& parser) {
                
                // This does the actual tracking of the objects. We can't initialize it now because
                // it needs to know the size of the frame. So, we set it equal to nullptr and initialize
                // it after we get the first frame.
                std::unique_ptr<OT::MultiObjectTracker> tracker = nullptr;
                
                // We'll use this variable to store the current frame captured from the video.
                cv::Mat frame;
                
                // This object represents the video or image sequence that we are reading from.
                cv::VideoCapture capture;
                
                // These two variables store the contours and contour hierarchy for the current frame.
                // We won't use the hierarchy, but we need it in order to be able to call the cv::findContours
                // function.
                std::vector<cv::Vec4i> hierarchy;
                std::vector<std::vector<cv::Point> > contours;
                
                // We'll use a ContourFinder to do the actual extraction of contours from the image.
                OT::ContourFinder contourFinder;
                
                // We'll count the frame with this variable.
                long frameNumber = 0;
                
                // This will log all of the tracked objects.
                OT::TrackerLog trackerLog(true);
                
                // Determine how to scale the video.
                int maxDimension = parser.get<int>("d");
                
                // Read from the webcam or the parser.
                if (parser.get<int>("w") != -1) {
                    capture.open(parser.get<int>("w"));
                } else {
                    capture.open(parser.get<std::string>("i"));
                }
                
                // Get the perspective transform, if there is one.
                auto perspectivePoints = parser.get<std::vector<int>>("p");
                cv::Mat perspectiveMatrix;
                cv::Size perspectiveSize;
                std::vector<cv::Point2f> points;
                OT::Perspective::extractFourPoints(perspectivePoints, points);
                if (!points.empty()) {
                    perspectiveMatrix = OT::Perspective::getPerspectiveMatrix(points, perspectiveSize);
                }
                
                // Read the second positional command line argument and use that as the log
                // for the output file.
                std::string outputFilePath = parser.get<std::string>("s");
                std::ofstream outputFile;
                if (!outputFilePath.empty()) {
                    outputFile.open(outputFilePath);
                }
                
                // Ensure that the video has been opened correctly.
                if(!capture.isOpened()) {
                    std::cerr << "Problem opening video source" << std::endl;
                }
                
                // Set the mouse callback.
                cv::namedWindow("Video");
                cv::namedWindow("Original");
                cv::setMouseCallback("Video", mouseHandler);
                cv::setMouseCallback("Original", mouseHandler);
                
                
                // Repeat while the user has not pressed "q" and while there's another frame.
                while(OT::Utils::hasFrame(capture)) {
                    // Fetch the next frame.
                    capture.retrieve(frame);
                    frameNumber++;
                    
                    imshow("Original", frame);
                    
                    // Do the perspective transform.
                    if (!points.empty()) {
                        cv::warpPerspective(frame, frame, perspectiveMatrix, perspectiveSize);
                    }
                    
                    // Scale the image.
                    OT::Utils::scale(frame, maxDimension);
                    
                    // Create the tracker if it isn't created yet.
                    if (tracker == nullptr) {
                        tracker = std::make_unique<OT::MultiObjectTracker>(cv::Size(frame.rows, frame.cols));
                    }
                    
                    // Set the frame dimension.
                    trackerLog.setDimensions(frame.cols, frame.rows);
                    
                    // Find the contours.
                    std::vector<cv::Point2f> mc(contours.size());
                    std::vector<cv::Rect> boundRect(contours.size());
                    contourFinder.findContours(frame, hierarchy, contours, mc, boundRect);
                    
                    OT::DrawUtils::contourShow("Contours", contours, boundRect, frame.size());
                    
                    // Update the predicted locations of the objects based on the observed
                    // mass centers.
                    std::vector<OT::TrackingOutput> predictions;
                    tracker->update(mc, boundRect, predictions);
                    
                    for (auto pred : predictions) {
                        // Draw a cross at the location of the prediction.
                        OT::DrawUtils::drawCross(frame, pred.location, pred.color, 5);
                        
                        // Draw the trajectory for the prediction.
                        OT::DrawUtils::drawTrajectory(frame, pred.trajectory, pred.color);
                        
                        // Update the tracker log.
                        if (!outputFilePath.empty()) {
                            trackerLog.addTrack(pred.id, pred.location.x, pred.location.y, frameNumber);
                        }
                    }
                    
                    // Handle mouse callbacks.
                    if (hasRectangle || triggerCallback) {
                        cv::rectangle(frame, point1, point2, cv::Scalar::all(255));
                    }
                    
                    if (triggerCallback) {
                        triggerCallback = false;
                        contourFinder.suppressRectangle(cv::Rect(point1, point2));
                    }
                    
                    imshow("Video", frame);
                }
                
                
                // Log the output file if we need to.
                if (!outputFilePath.empty()) {
                    trackerLog.logToFile(outputFile);
                    outputFile.close();
                }
            } // run
        } // Tracking
    } // Mode
} // OT
