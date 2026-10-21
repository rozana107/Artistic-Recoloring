#include "label_io.h"
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace artistic {
namespace {
std::string extension(const std::string& path) {
    std::string ext = path.substr(path.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}
bool storageFile(const std::string& path) {
    std::string ext = extension(path);
    return ext == "yml" || ext == "yaml" || ext == "xml" || ext == "json" || ext == "gz";
}
}

cv::Mat loadLabels(const std::string& path, cv::Size expectedSize) {
    cv::Mat labels;
    if (storageFile(path)) {
        cv::FileStorage file(path, cv::FileStorage::READ);
        if (!file.isOpened()) throw std::runtime_error("Cannot open label file: " + path);
        // The abstraction program also saves 'owners'. Those overlapping-region
        // owners include -1 background and are NOT the final segmentation.
        if (file["labels"].empty()) throw std::runtime_error("Label file needs the final 'labels' matrix.");
        file["labels"] >> labels;
        if (labels.type() != CV_32SC1) throw std::runtime_error("Stored labels must be CV_32SC1 integers.");
    } else {
        cv::Mat source = cv::imread(path, cv::IMREAD_UNCHANGED);
        if (source.empty()) throw std::runtime_error("Cannot read labels: " + path);
        if (source.channels() == 1 && (source.depth() == CV_8U || source.depth() == CV_16U))
            source.convertTo(labels, CV_32S);
        else if (source.type() == CV_8UC3 || source.type() == CV_8UC4) {
            labels.create(source.size(), CV_32S);
            int channels = source.channels();
            for (int y = 0; y < source.rows; ++y) {
                const uchar* row = source.ptr<uchar>(y);
                int* output = labels.ptr<int>(y);
                for (int x = 0; x < source.cols; ++x) {
                    const uchar* p = row + x * channels;
                    output[x] = p[0] | (p[1] << 8) | (p[2] << 16);
                }
            }
        } else throw std::runtime_error("Labels must be integer YAML/XML/JSON, 8/16-bit grayscale PNG, or 8-bit color.");
    }
    if (labels.size() != expectedSize) throw std::runtime_error("Labels must match the ORIGINAL image dimensions.");
    double smallest;
    cv::minMaxLoc(labels, &smallest, nullptr);
    if (smallest < 0) throw std::runtime_error("Final labels must be nonnegative; do not use the owners matrix.");
    return labels;
}

void saveLabels(const std::string& path, const cv::Mat& labels) {
    if (storageFile(path)) {
        cv::FileStorage file(path, cv::FileStorage::WRITE);
        if (!file.isOpened()) throw std::runtime_error("Cannot write labels: " + path);
        file << "labels" << labels;
    } else {
        double maximum;
        cv::minMaxLoc(labels, nullptr, &maximum);
        if (extension(path) != "png" || maximum > 65535)
            throw std::runtime_error("Use .yml for large labels, or .png with IDs <= 65535.");
        cv::Mat exported;
        labels.convertTo(exported, CV_16U);
        if (!cv::imwrite(path, exported)) throw std::runtime_error("Cannot write labels: " + path);
    }
}
}
