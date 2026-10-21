#pragma once

#include <opencv2/core.hpp>
#include <vector>

namespace artistic {

enum class Metric { RGB, Lab };

struct Settings {
    Metric metric = Metric::RGB;
    bool histogramMatching = true;
    bool preserveLightness = true;
    bool splitDisconnectedLabels = true; // False preserves supplied region membership exactly.
    double lightnessTolerance = 0.0; // CIE L* units; zero follows the paper.
    cv::Point root = cv::Point(-1, -1); // Negative means image center.
};

struct Region {
    int area = 0;
    cv::Vec3f color; // Mean BGR (RGB metric) or mean color converted to Lab.
    cv::Vec3b meanBgr; // For the diagnostic flat image.
    float lightness = 0.0f;
};

struct Edge {
    int first = 0;
    int second = 0;
    float difference = 0.0f;
    float target = 0.0f;
};

struct Graph {
    std::vector<Region> regions;
    std::vector<Edge> edges;
    std::vector<std::vector<int>> neighbors; // Incident edge indices.
    float medianDifference = 0.0f;
};

struct Tree {
    std::vector<int> parent;
    std::vector<int> parentEdge;
    std::vector<float> width;
    std::vector<int> order; // A parent always occurs before its children.
};

struct Result {
    cv::Mat image;
    cv::Mat labels; // Dense region IDs, CV_32SC1.
    cv::Mat flatImage; // Original mean color per region.
    int regionCount = 0;
    int edgeCount = 0;
    int lightnessFallbacks = 0;
    double labelsMs = 0.0;
    double graphMs = 0.0;
    double histogramMs = 0.0;
    double treeMs = 0.0;
    double assignmentMs = 0.0;
    double renderMs = 0.0;
};

// These two steps are public so the graph algorithm can also be studied alone.
Graph buildGraph(const cv::Mat& image, const cv::Mat& denseLabels,
                 int regionCount, Metric metric);
Tree buildWidestPathTree(const Graph& graph, int root);

// image: 8-bit BGR; labels: nonnegative integer IDs, same dimensions.
// splitDisconnectedLabels controls whether disconnected pieces are separated.
Result recolor(const cv::Mat& image, const cv::Mat& labels,
               const std::vector<cv::Vec3b>& palette,
               const Settings& settings = Settings());

} // namespace artistic
