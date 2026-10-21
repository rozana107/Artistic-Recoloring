#include "recolor.h"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

cv::Vec3b gray(int v) { return cv::Vec3b(v, v, v); }

// Independent all-pairs max/min oracle checks the priority-queue solver,
// including asymmetric destination-size weights and zero-capacity edges.
void testWidestPaths() {
    artistic::Graph graph;
    graph.regions.resize(5);
    graph.neighbors.resize(5);
    int areas[] = {1, 3, 7, 2, 12};
    for (int i = 0; i < 5; ++i) graph.regions[i].area = areas[i];
    graph.medianDifference = 4.0f;
    graph.edges = {{0,1,1,1}, {0,2,9,9}, {1,2,8,8},
                   {1,3,2,2}, {2,3,4,4}, {3,4,5,5}};
    std::vector<std::vector<float>> capacity(5, std::vector<float>(5, -1.0f));
    for (int i = 0; i < 5; ++i) capacity[i][i] = std::numeric_limits<float>::infinity();
    for (int i = 0; i < static_cast<int>(graph.edges.size()); ++i) {
        const auto& e = graph.edges[i];
        graph.neighbors[e.first].push_back(i);
        graph.neighbors[e.second].push_back(i);
        capacity[e.first][e.second] = (1.0f + areas[e.second] / 5.0f) * std::abs(e.difference - 4.0f);
        capacity[e.second][e.first] = (1.0f + areas[e.first] / 5.0f) * std::abs(e.difference - 4.0f);
    }
    for (int k = 0; k < 5; ++k)
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 5; ++j)
                capacity[i][j] = std::max(capacity[i][j], std::min(capacity[i][k], capacity[k][j]));
    for (int root = 0; root < 5; ++root) {
        artistic::Tree tree = artistic::buildWidestPathTree(graph, root);
        std::vector<int> rank(5);
        for (int i = 0; i < 5; ++i) rank[tree.order[i]] = i;
        for (int i = 0; i < 5; ++i) {
            if (i == root) { require(tree.parent[i] == -1, "Root has a parent"); continue; }
            require(std::abs(tree.width[i] - capacity[root][i]) < 1e-5f, "Incorrect widest-path width");
            require(rank[tree.parent[i]] < rank[i], "Tree order contains a cycle or late parent");
        }
    }
    // All capacities zero still must produce a spanning tree.
    for (auto& e : graph.edges) e.difference = 4.0f;
    auto tree = artistic::buildWidestPathTree(graph, 0);
    require(tree.order.size() == 5 && tree.width[4] == 0.0f, "Zero capacities disconnected the tree");
}

void testKnownAssignmentAndFallback() {
    cv::Mat image(1, 3, CV_8UC3), labels(1, 3, CV_32S);
    image.at<cv::Vec3b>(0,0) = gray(200);
    image.at<cv::Vec3b>(0,1) = gray(100);
    image.at<cv::Vec3b>(0,2) = gray(0);
    labels.at<int>(0,0) = 70000; labels.at<int>(0,1) = 2; labels.at<int>(0,2) = 8;
    artistic::Settings settings;
    settings.histogramMatching = false;
    settings.root = cv::Point(0,0);
    auto result = artistic::recolor(image, labels, {gray(0), gray(100), gray(200)}, settings);
    require(cv::norm(image, result.image, cv::NORM_INF) == 0, "Known descending chain changed");
    require(result.edgeCount == 2 && result.regionCount == 3, "Chain adjacency is wrong");

    image = cv::Mat(1, 2, CV_8UC3);
    image.at<cv::Vec3b>(0,0) = gray(80); image.at<cv::Vec3b>(0,1) = gray(20);
    labels = (cv::Mat_<int>(1,2) << 0, 1);
    result = artistic::recolor(image, labels, {gray(0), gray(255)}, settings);
    require(result.lightnessFallbacks == 1, "Palette-extreme fallback was not used");
    require(result.image.at<cv::Vec3b>(0,1) == gray(0), "Fallback chose the wrong color");
}

void testFlatImageAndOneColor() {
    cv::Mat image(9, 11, CV_8UC3, cv::Scalar(75,75,75));
    cv::Mat labels(image.size(), CV_32S);
    for (int y = 0; y < image.rows; ++y)
        for (int x = 0; x < image.cols; ++x) labels.at<int>(y,x) = (x + y) % 2;
    std::vector<cv::Vec3b> palette = {gray(0), gray(80), gray(255)};
    auto result = artistic::recolor(image, labels, palette);
    require(result.regionCount == 99, "Disconnected label pieces were not separated");
    require(cv::norm(result.image, cv::Mat(image.size(), CV_8UC3, cv::Scalar(80,80,80)), cv::NORM_INF) == 0,
            "A constant image acquired false edges");
    cv::randu(image, 0, 256);
    result = artistic::recolor(image, labels, {cv::Vec3b(17,99,230)});
    require(cv::norm(result.image, cv::Mat(image.size(), CV_8UC3, cv::Scalar(17,99,230)), cv::NORM_INF) == 0,
            "A one-color palette produced other colors");
}

void testHistogramChangesDifferences() {
    cv::Mat image(1,4,CV_8UC3);
    int values[] = {0, 1, 11, 111};
    for (int i = 0; i < 4; ++i) image.at<cv::Vec3b>(0,i) = gray(values[i]);
    cv::Mat labels = (cv::Mat_<int>(1,4) << 0, 1, 2, 3);
    artistic::Settings settings;
    settings.root = cv::Point(0,0);
    settings.preserveLightness = false;
    auto matched = artistic::recolor(image, labels, {gray(0), gray(100), gray(255)}, settings);
    int expected[] = {0, 0, 100, 255};
    for (int i = 0; i < 4; ++i)
        require(matched.image.at<cv::Vec3b>(0,i) == gray(expected[i]), "CDF matching failed on a known low-contrast chain");
    settings.histogramMatching = false;
    auto direct = artistic::recolor(image, labels, {gray(0), gray(100), gray(255)}, settings);
    require(cv::norm(matched.image, direct.image, cv::NORM_INF) > 0,
            "Histogram matching did not affect a narrow input distribution");
}

void testLightnessConversion() {
    int values[] = {0, 10, 128, 255};
    float expected[] = {0.0f, 2.74175f, 53.58501f, 100.0f};
    for (int i = 0; i < 4; ++i) {
        cv::Mat image(1,1,CV_8UC3,cv::Scalar(values[i],values[i],values[i]));
        auto graph = artistic::buildGraph(image, cv::Mat(1,1,CV_32S,cv::Scalar(0)), 1,
                                         artistic::Metric::RGB);
        require(std::abs(graph.regions[0].lightness - expected[i]) < 0.01f,
                "CIE L* conversion failed at a known sRGB gray value");
    }
}

void testPaletteAndRegionInvariants() {
    cv::Mat image(37, 49, CV_8UC3), labels(image.size(), CV_32S);
    cv::RNG random(12345);
    random.fill(image, cv::RNG::UNIFORM, 0, 256);
    for (int y = 0; y < image.rows; ++y)
        for (int x = 0; x < image.cols; ++x) labels.at<int>(y,x) = (y / 4) * 20 + x / 5;
    std::vector<cv::Vec3b> palette = {{0,0,0}, {255,255,255}, {19,73,235}, {211,90,28}};
    for (auto metric : {artistic::Metric::RGB, artistic::Metric::Lab}) {
        artistic::Settings settings;
        settings.metric = metric;
        auto a = artistic::recolor(image, labels, palette, settings);
        auto b = artistic::recolor(image, labels, palette, settings);
        require(cv::norm(a.image, b.image, cv::NORM_INF) == 0, "Repeated runs are not deterministic");
        std::vector<cv::Vec3b> regionColors(a.regionCount);
        std::vector<bool> seen(a.regionCount, false);
        for (int y = 0; y < image.rows; ++y)
            for (int x = 0; x < image.cols; ++x) {
                cv::Vec3b color = a.image.at<cv::Vec3b>(y,x);
                require(std::find(palette.begin(), palette.end(), color) != palette.end(), "Output color outside palette");
                int id = a.labels.at<int>(y,x);
                if (seen[id]) require(color == regionColors[id], "A region has multiple output colors");
                regionColors[id] = color; seen[id] = true;
            }
    }
    auto single = artistic::recolor(cv::Mat(1,1,CV_8UC3,cv::Scalar(0,0,0)),
                                    cv::Mat(1,1,CV_32S,cv::Scalar(0)), palette);
    require(single.regionCount == 1 && single.edgeCount == 0, "Single-pixel image failed");
    bool rejected = false;
    try { artistic::recolor(image, cv::Mat(2,2,CV_32S), palette); }
    catch (const std::exception&) { rejected = true; }
    require(rejected, "Incorrect label dimensions were accepted");
}

int main() {
    try {
        cv::setNumThreads(1);
        testWidestPaths();
        testKnownAssignmentAndFallback();
        testFlatImageAndOneColor();
        testHistogramChangesDifferences();
        testLightnessConversion();
        testPaletteAndRegionInvariants();
        std::cout << "All correctness checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
