#pragma once
#include <opencv2/core.hpp>
#include <string>

namespace artistic {
cv::Mat loadLabels(const std::string& path, cv::Size expectedSize);
void saveLabels(const std::string& path, const cv::Mat& labels);
}
