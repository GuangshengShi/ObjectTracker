#include "contour_finder.hpp"

#include <numeric>
#include <unordered_map>

#include <opencv2/opencv.hpp>
#include <opencv2/video/tracking.hpp>

#include "disjoint_set.hpp"

namespace OT {
    ContourFinder::ContourFinder(int history,
                                 int nMixtures,
                                 float contourSizeThreshold,
                                 int medianFilterSize,
                                 float contourMergeThreshold) {
        this->bg = cv::createBackgroundSubtractorMOG2();
        this->bg->setHistory(history);
        this->bg->setNMixtures(nMixtures);
        this->bg->setDetectShadows(true);
        this->bg->setShadowThreshold(0.5);
        this->contourSizeThreshold = contourSizeThreshold;
        this->medianFilterSize = medianFilterSize;
        this->contourMergeThreshold = contourMergeThreshold;
    }
    
    /**
     * Remove contours if they are are too small.
     */
    void ContourFinder::filterOutBadContours(std::vector<std::vector<cv::Point>>& contours) {
        std::vector<double> areas(contours.size());
        
        // Find the area of each contour.
        std::transform(contours.cbegin(),
                       contours.cend(),
                       areas.begin(),
                       [](std::vector<cv::Point> contour) {
                           return cv::contourArea(contour);
                       });
        
        // Select the largest contour.
        double maxArea = std::accumulate(areas.cbegin(),
                                         areas.cend(),
                                         0,
                                         [](double prevMaxArea, double area) {
                                             return std::max(prevMaxArea, area);
                                         });
        
        // Create the threshold.
        int threshold = this->contourSizeThreshold * maxArea;
        
        // Remove contours that have a size less than the threshold.
        auto removeThese = std::remove_if(contours.begin(), contours.end(), [threshold](std::vector<cv::Point> contour) {
            return cv::contourArea(contour) <= threshold;
        });
        contours.erase(removeThese, contours.end());
    }
    
    void ContourFinder::findContours(const cv::Mat& frame,
                                     std::vector<cv::Vec4i>& hierarchy,
                                     std::vector<std::vector<cv::Point>>& contours,
                                     std::vector<cv::Point2f>& massCenters,
                                     std::vector<cv::Rect>& boundingBoxes) {
        // First clear the conotour and hierarchy objects.
        contours.clear();
        hierarchy.clear();
        
        // Find the foreground.
        this->bg->apply(frame, this->foreground);
        cv::threshold(this->foreground, this->foreground, 130, 255, CV_THRESH_BINARY);
        
        // Get rid little specks of noise by doing a median blur.
        // The median blur is good for salt-and-pepper noise, not Gaussian noise.
        cv::medianBlur(this->foreground, this->foreground, this->medianFilterSize);
        
        // Dilate the image to make the blobs larger.
        cv::dilate(this->foreground, this->foreground, cv::Mat());
        cv::dilate(this->foreground, this->foreground, cv::Mat());
        cv::dilate(this->foreground, this->foreground, cv::Mat());
        cv::dilate(this->foreground, this->foreground, cv::Mat());
        
        cv::imshow("foreground", this->foreground);
        
        // Find the contours.
        cv::findContours(this->foreground, contours, hierarchy, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE, cv::Point(0, 0));
        
        // Keep only those contours that are sufficiently large.
        this->filterOutBadContours(contours);
        
        // Get the mass centers and bounding boxes.
        this->getCentersAndBoundingBoxes(contours, massCenters, boundingBoxes);
        
        // Merge nearby contours.
        this->mergeContours(contours, massCenters, boundingBoxes);
        
        // Now find the mass centers and bounding boxes again.
        this->getCentersAndBoundingBoxes(contours, massCenters, boundingBoxes);
    }
    
    void ContourFinder::mergeContours(std::vector<std::vector<cv::Point> > &contours,
                                      const std::vector<cv::Point2f>& massCenters,
                                      const std::vector<cv::Rect>& boundingBoxes) {
        DisjointSets sets(contours.size());
        double dimension;
        for (size_t i = 0; i < contours.size(); i++) {
            for (size_t j = 0; j < contours.size(); j++) {
                if (i == j) {
                    continue;
                }
                
                // Otherwise, measure the distance between the mass centers,
                // and if it's small enough, merge them.
                dimension = std::max(std::max(boundingBoxes[i].width,
                                              boundingBoxes[i].height),
                                     std::max(boundingBoxes[j].width,
                                              boundingBoxes[j].height));
                if (cv::norm(massCenters[i] - massCenters[j]) < this->contourMergeThreshold * dimension) {
                    sets.Union(i, j);
                }
            }
        }
        
        // Create a map such that the values are the sets of
        // indices of contours that should be merged.
        std::unordered_map<int, std::vector<int>> itemsForSet;
        for (size_t i = 0; i < contours.size(); i++) {
            if (itemsForSet.find(sets.FindSet(i)) == itemsForSet.end()) {
                itemsForSet.insert(std::pair<int, std::vector<int>>(sets.FindSet(i), std::vector<int>()));
            }
            itemsForSet[sets.FindSet(i)].push_back(i);
        }
        
        // Now merge the contours.
        std::vector<std::vector<cv::Point>> newContours;
        for (auto kv : itemsForSet) {
            // If there's only one item, just add it without doing any merge.
            if (kv.second.size() == 1) {
                newContours.push_back(contours[kv.first]);
                continue;
            }
            
            // Combine all the points of every contour
            // that must be merged into the vector<vector<Point>> aggregate.
            auto aggregate = contours[kv.second[0]];
            for (size_t i = 1; i < kv.second.size(); i++) {
                std::copy(contours[kv.second[i]].cbegin(),
                          contours[kv.second[i]].cend(),
                          std::back_inserter(aggregate));
            }
            
            // Now use the merged contours instead of the original contours.
            newContours.push_back(aggregate);
        }
        
        // Replace the old contours with the new ones.
        contours.clear();
        std::copy(newContours.cbegin(), newContours.cend(), std::back_inserter(contours));
    }
    
    void ContourFinder::getCentersAndBoundingBoxes(const std::vector<std::vector<cv::Point> > &contours,
                                                   std::vector<cv::Point2f> &massCenters,
                                                   std::vector<cv::Rect> &boundingBoxes) {
        // Empty the vectors.
        massCenters.clear();
        boundingBoxes.clear();
        
        // Create local variables.
        cv::Moments contourMoments;
        std::vector<std::vector<cv::Point> > contourPolygons(contours.size());
        
        // Iterate through every contour.
        for (size_t i = 0; i < contours.size(); i++) {
            // Compute the center of mass.
            contourMoments = cv::moments(contours[i], false);
            massCenters.push_back(cv::Point2f(contourMoments.m10/contourMoments.m00, contourMoments.m01/contourMoments.m00));
            
            // Compute the polygon represented by the contour, and then compute the bounding box around that polygon.
            cv::approxPolyDP(cv::Mat(contours[i]), contourPolygons[i], 3, true);
            boundingBoxes.push_back(cv::boundingRect(cv::Mat(contourPolygons[i])));
        }
    }
}