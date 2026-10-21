#include "recolor.h"

#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>
#include <utility>

namespace artistic {
namespace {

using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

float distance(const cv::Vec3f& a, const cv::Vec3f& b) {
    cv::Vec3f d = a - b;
    return std::sqrt(d.dot(d));
}

// Flood fill makes IDs consecutive and separates disconnected components.
cv::Mat connectedLabels(const cv::Mat& input, int& count) {
    cv::Mat labels(input.size(), CV_32S, cv::Scalar(-1));
    std::vector<cv::Point> queue;
    const int dx[] = {-1, 1, 0, 0};
    const int dy[] = {0, 0, -1, 1};
    count = 0;
    for (int y = 0; y < input.rows; ++y) {
        for (int x = 0; x < input.cols; ++x) {
            if (labels.at<int>(y, x) >= 0) continue;
            int original = input.at<int>(y, x);
            if (original < 0) throw std::runtime_error("Labels must be nonnegative.");
            queue.clear();
            queue.push_back(cv::Point(x, y));
            labels.at<int>(y, x) = count;
            for (size_t head = 0; head < queue.size(); ++head) {
                cv::Point p = queue[head];
                for (int k = 0; k < 4; ++k) {
                    int nx = p.x + dx[k], ny = p.y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= input.cols || ny >= input.rows) continue;
                    if (labels.at<int>(ny, nx) == -1 && input.at<int>(ny, nx) == original) {
                        labels.at<int>(ny, nx) = count;
                        queue.push_back(cv::Point(nx, ny));
                    }
                }
            }
            ++count;
        }
    }
    return labels;
}

// The abstraction cleanup may attach a fragment diagonally. In this mode its
// supplied region membership is authoritative; only compact the integer IDs.
cv::Mat compactLabels(const cv::Mat& input, int& count) {
    cv::Mat labels(input.size(), CV_32S);
    std::unordered_map<int, int> ids;
    count = 0;
    for (int y = 0; y < input.rows; ++y) {
        const int* source = input.ptr<int>(y);
        int* target = labels.ptr<int>(y);
        for (int x = 0; x < input.cols; ++x) {
            if (source[x] < 0) throw std::runtime_error("Labels must be nonnegative.");
            auto entry = ids.emplace(source[x], count);
            if (entry.second) ++count;
            target[x] = entry.first->second;
        }
    }
    return labels;
}

// OpenCV float Lab uses L* in [0,100], with signed a* and b*.
cv::Mat toLab(const cv::Mat& bgr) {
    cv::Mat scaled, lab;
    bgr.convertTo(scaled, CV_32F, 1.0 / 255.0);
    cv::cvtColor(scaled, lab, cv::COLOR_BGR2Lab);
    return lab;
}

double linearChannel(float value) {
    double s = value / 255.0;
    return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

float cieLightness(const cv::Vec3f& bgr) {
    // sRGB -> D65 relative luminance -> CIE L*. These are the same
    // coefficients/curve used in OpenCV's Lab conversion. Computing just L*
    // avoids initializing the large Lab lookup tables in the default RGB mode.
    double y = 0.212671 * linearChannel(bgr[2]) + 0.715160 * linearChannel(bgr[1]) +
               0.072169 * linearChannel(bgr[0]);
    return static_cast<float>(y > 216.0 / 24389.0 ? 116.0 * std::cbrt(y) - 16.0
                                               : (24389.0 / 27.0) * y);
}

void matchDifferences(Graph& graph, const std::vector<float>& paletteDistances) {
    if (graph.edges.empty()) return;
    std::vector<float> sortedPalette = paletteDistances;
    std::sort(sortedPalette.begin(), sortedPalette.end());
    std::vector<std::pair<float, int>> sortedEdges;
    for (int i = 0; i < static_cast<int>(graph.edges.size()); ++i)
        sortedEdges.push_back({graph.edges[i].difference, i});
    std::sort(sortedEdges.begin(), sortedEdges.end());

    // Empirical CDF matching. Equal differences share their midpoint rank.
    // Include palette self-distances: assigning the parent's color is allowed.
    for (size_t begin = 0; begin < sortedEdges.size();) {
        size_t end = begin + 1;
        while (end < sortedEdges.size() && sortedEdges[end].first == sortedEdges[begin].first)
            ++end;
        double quantile = (static_cast<double>(begin) + end) / (2.0 * sortedEdges.size());
        size_t index = std::min(sortedPalette.size() - 1,
                               static_cast<size_t>(quantile * sortedPalette.size()));
        // Keep truly identical input colors identical even in a flat image.
        float target = sortedEdges[begin].first == 0.0f ? 0.0f : sortedPalette[index];
        for (size_t i = begin; i < end; ++i)
            graph.edges[sortedEdges[i].second].target = target;
        begin = end;
    }
}

std::vector<int> assignColors(const Graph& graph, const Tree& tree,
                             const std::vector<cv::Vec3f>& paletteColors,
                             const std::vector<float>& paletteLightness,
                             const std::vector<float>& paletteDistances,
                             const Settings& settings, int& fallbacks) {
    int k = static_cast<int>(paletteColors.size());
    std::vector<int> assigned(graph.regions.size(), -1);
    int root = tree.order.front();
    float best = std::numeric_limits<float>::infinity();
    for (int p = 0; p < k; ++p) {
        float error = distance(graph.regions[root].color, paletteColors[p]);
        if (error < best) {
            best = error;
            assigned[root] = p;
        }
    }

    for (size_t i = 1; i < tree.order.size(); ++i) {
        int node = tree.order[i];
        int parent = tree.parent[node];
        int parentColor = assigned[parent];
        float target = graph.edges[tree.parentEdge[node]].target;
        float change = graph.regions[node].lightness - graph.regions[parent].lightness;
        int choice = -1;

        // Equation 5, first with the lightness constraint, then with the paper's
        // fallback if no palette color can satisfy it (palette extrema).
        for (int pass = 0; pass < 2 && choice == -1; ++pass) {
            best = std::numeric_limits<float>::infinity();
            for (int p = 0; p < k; ++p) {
                if (pass == 0 && settings.preserveLightness) {
                    float outputChange = paletteLightness[p] - paletteLightness[parentColor];
                    if (change > settings.lightnessTolerance && outputChange <= 0.0f) continue;
                    if (change < -settings.lightnessTolerance && outputChange >= 0.0f) continue;
                }
                float error = std::abs(target - paletteDistances[parentColor * k + p]);
                if (error < best) {
                    best = error;
                    choice = p;
                }
            }
            if (pass == 0 && choice == -1) ++fallbacks;
        }
        assigned[node] = choice;
    }
    return assigned;
}

} // namespace

Graph buildGraph(const cv::Mat& image, const cv::Mat& labels, int count, Metric metric) {
    if (image.empty() || image.type() != CV_8UC3 || labels.type() != CV_32SC1 ||
        image.size() != labels.size() || count <= 0 || count > static_cast<int>(image.total()))
        throw std::runtime_error("Invalid image, dense labels, or region count.");

    Graph graph;
    graph.regions.resize(count);
    graph.neighbors.resize(count);
    std::vector<cv::Vec3d> sums(count, cv::Vec3d(0, 0, 0));
    for (int y = 0; y < image.rows; ++y) {
        const cv::Vec3b* pixels = image.ptr<cv::Vec3b>(y);
        const int* ids = labels.ptr<int>(y);
        for (int x = 0; x < image.cols; ++x) {
            int id = ids[x];
            if (id < 0 || id >= count) throw std::runtime_error("Dense label ID out of range.");
            ++graph.regions[id].area;
            sums[id] += cv::Vec3d(pixels[x][0], pixels[x][1], pixels[x][2]);
        }
    }

    cv::Mat means(1, count, CV_32FC3);
    for (int i = 0; i < count; ++i) {
        if (graph.regions[i].area == 0) throw std::runtime_error("Dense labels contain an empty region.");
        means.at<cv::Vec3f>(0, i) = cv::Vec3f(sums[i] / graph.regions[i].area);
    }
    cv::Mat lab;
    if (metric == Metric::Lab) lab = toLab(means);
    for (int i = 0; i < count; ++i) {
        graph.regions[i].color = metric == Metric::RGB ? means.at<cv::Vec3f>(0, i)
                                                     : lab.at<cv::Vec3f>(0, i);
        cv::Vec3f mean = means.at<cv::Vec3f>(0, i);
        graph.regions[i].lightness = cieLightness(mean);
        graph.regions[i].meanBgr = cv::Vec3b(cv::saturate_cast<uchar>(mean[0]),
                                            cv::saturate_cast<uchar>(mean[1]),
                                            cv::saturate_cast<uchar>(mean[2]));
    }

    std::unordered_set<uint64_t> seen;
    seen.reserve(static_cast<size_t>(count) * 6);
    for (int y = 0; y < labels.rows; ++y) {
        const int* ids = labels.ptr<int>(y);
        for (int x = 0; x < labels.cols; ++x) {
            // Right and down are sufficient for undirected 4-neighbor adjacency.
            int adjacent[] = {x + 1 < labels.cols ? ids[x + 1] : ids[x],
                              y + 1 < labels.rows ? labels.at<int>(y + 1, x) : ids[x]};
            for (int other : adjacent) {
                int a = std::min(ids[x], other), b = std::max(ids[x], other);
                if (a == b) continue;
                uint64_t key = (static_cast<uint64_t>(a) << 32) | static_cast<uint32_t>(b);
                if (!seen.insert(key).second) continue;
                float d = distance(graph.regions[a].color, graph.regions[b].color);
                int edge = static_cast<int>(graph.edges.size());
                graph.edges.push_back({a, b, d, d});
                graph.neighbors[a].push_back(edge);
                graph.neighbors[b].push_back(edge);
            }
        }
    }
    if (!graph.edges.empty()) {
        std::vector<float> differences;
        for (const Edge& edge : graph.edges) differences.push_back(edge.difference);
        std::sort(differences.begin(), differences.end());
        size_t n = differences.size();
        graph.medianDifference = (differences[(n - 1) / 2] + differences[n / 2]) * 0.5f;
    }
    return graph;
}

Tree buildWidestPathTree(const Graph& graph, int root) {
    int count = static_cast<int>(graph.regions.size());
    if (root < 0 || root >= count) throw std::runtime_error("Root region out of range.");
    double totalArea = 0.0;
    for (const Region& region : graph.regions) totalArea += region.area;
    double meanArea = totalArea / count;
    if (meanArea <= 0.0) throw std::runtime_error("Graph has no pixels.");

    Tree tree;
    tree.parent.assign(count, -1);
    tree.parentEdge.assign(count, -1);
    tree.width.assign(count, -1.0f);
    std::vector<bool> finished(count, false);
    std::priority_queue<std::pair<float, int>> queue;
    tree.width[root] = std::numeric_limits<float>::infinity();
    queue.push({tree.width[root], root});

    while (!queue.empty()) {
        float width = queue.top().first;
        int node = queue.top().second;
        queue.pop();
        if (finished[node] || width < tree.width[node]) continue;
        finished[node] = true;
        tree.order.push_back(node);
        for (int index : graph.neighbors[node]) {
            const Edge& edge = graph.edges[index];
            int next = edge.first == node ? edge.second : edge.first;
            if (finished[next]) continue;
            // Equation 4: use the DESTINATION region's area. The two traversal
            // directions may have different capacities; do not use an MST.
            float weight = static_cast<float>((1.0 + graph.regions[next].area / meanArea) *
                           std::abs(edge.difference - graph.medianDifference));
            float candidate = std::min(width, weight);
            if (candidate > tree.width[next]) {
                tree.width[next] = candidate;
                tree.parent[next] = node;
                tree.parentEdge[next] = index;
                queue.push({candidate, next});
            }
        }
    }
    if (tree.order.size() != graph.regions.size())
        throw std::runtime_error("Region graph is disconnected.");
    return tree;
}

Result recolor(const cv::Mat& image, const cv::Mat& inputLabels,
               const std::vector<cv::Vec3b>& palette, const Settings& settings) {
    if (image.empty() || image.type() != CV_8UC3)
        throw std::runtime_error("Input must be a nonempty 8-bit BGR image.");
    if (image.total() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Image is too large; resize it first.");
    if (inputLabels.type() != CV_32SC1 || inputLabels.size() != image.size())
        throw std::runtime_error("Labels must be CV_32SC1 and match the image dimensions.");
    if (palette.empty() || palette.size() > 256)
        throw std::runtime_error("Use between 1 and 256 palette colors.");
    if (!std::isfinite(settings.lightnessTolerance) || settings.lightnessTolerance < 0)
        throw std::runtime_error("Lightness tolerance must be finite and nonnegative.");

    Result result;
    auto start = Clock::now();
    result.labels = settings.splitDisconnectedLabels ? connectedLabels(inputLabels, result.regionCount)
                                                   : compactLabels(inputLabels, result.regionCount);
    result.labelsMs = elapsedMs(start);
    start = Clock::now();
    Graph graph = buildGraph(image, result.labels, result.regionCount, settings.metric);
    result.edgeCount = static_cast<int>(graph.edges.size());
    result.graphMs = elapsedMs(start);

    start = Clock::now();
    int k = static_cast<int>(palette.size());
    cv::Mat paletteBgr(1, k, CV_32FC3);
    for (int i = 0; i < k; ++i) paletteBgr.at<cv::Vec3f>(0, i) = cv::Vec3f(palette[i]);
    cv::Mat paletteLab;
    if (settings.metric == Metric::Lab) paletteLab = toLab(paletteBgr);
    std::vector<cv::Vec3f> colors(k);
    std::vector<float> lightness(k), distances(k * k);
    for (int i = 0; i < k; ++i) {
        colors[i] = settings.metric == Metric::RGB ? paletteBgr.at<cv::Vec3f>(0, i)
                                                 : paletteLab.at<cv::Vec3f>(0, i);
        lightness[i] = cieLightness(paletteBgr.at<cv::Vec3f>(0, i));
    }
    for (int i = 0; i < k; ++i)
        for (int j = 0; j < k; ++j) distances[i * k + j] = distance(colors[i], colors[j]);
    if (settings.histogramMatching) matchDifferences(graph, distances);
    result.histogramMs = elapsedMs(start);

    cv::Point root = settings.root;
    if (root.x < 0 && root.y < 0) root = cv::Point(image.cols / 2, image.rows / 2);
    if (root.x < 0 || root.y < 0 || root.x >= image.cols || root.y >= image.rows)
        throw std::runtime_error("Root pixel is outside the image.");
    start = Clock::now();
    Tree tree = buildWidestPathTree(graph, result.labels.at<int>(root.y, root.x));
    result.treeMs = elapsedMs(start);
    start = Clock::now();
    std::vector<int> assigned = assignColors(graph, tree, colors, lightness, distances,
                                            settings, result.lightnessFallbacks);
    result.assignmentMs = elapsedMs(start);

    start = Clock::now();
    result.image.create(image.size(), CV_8UC3);
    result.flatImage.create(image.size(), CV_8UC3);
    for (int y = 0; y < image.rows; ++y) {
        const int* ids = result.labels.ptr<int>(y);
        cv::Vec3b* output = result.image.ptr<cv::Vec3b>(y);
        cv::Vec3b* flat = result.flatImage.ptr<cv::Vec3b>(y);
        for (int x = 0; x < image.cols; ++x) {
            output[x] = palette[assigned[ids[x]]];
            flat[x] = graph.regions[ids[x]].meanBgr;
        }
    }
    result.renderMs = elapsedMs(start);
    return result;
}

} // namespace artistic
