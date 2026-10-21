#include "recolor.h"
#include "label_io.h"
#include "image_abstraction.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/ximgproc/segmentation.hpp>
#include <opencv2/ximgproc/slic.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    std::string input, output, paletteFile, labelsFile;
    std::string saveLabels, flatOutput, boundaryOutput;
    std::string method = "abstraction";
    std::string filteredFile, saveFiltered, abstractionOutput;
    std::string regionImageFile;
    bool originalMeans = false;
    abstraction::Parameters abstractionParameters;
    int regions = 4000, iterations = 10, minSize = 20;
    int maxSide = 0, repeat = 1, threads = 1;
    double sigma = 0.5, k = 150.0;
    artistic::Settings settings;
};

void help() {
    std::cout <<
        "Artistic recoloring (C++ / OpenCV)\n"
        "Usage: recolor input.jpg result.png [options]\n\n"
        "  --palette FILE       One RRGGBB or #RRGGBB color per line\n"
        "                       Default: built-in sunset palette\n"
        "  --method abstraction|graph|slico (default abstraction)\n"
        "  --mask NUMBER        CRGF neighborhood pixel count (160)\n"
        "  --gamma NUMBER       CRGF local color-change weight (1)\n"
        "  --detail-threshold N Signed residual seed threshold (3)\n"
        "  --seed-min-size N    Minimum seed component area (11)\n"
        "  --hops NUMBER        Region growth depth parameter (10)\n"
        "  --growth NUMBER      Growth stopping multiplier (100)\n"
        "  --bins NUMBER        Abstraction grayscale histogram bins (64)\n"
        "  --background NUMBER  SLIC background region count (580)\n"
        "  --filtered FILE      Matching precomputed CRGF image\n"
        "  --save-filtered FILE Save the CRGF image for later reuse\n"
        "  --abstraction FILE   Save the supplied method's rendered abstraction\n"
        "  --region-image FILE  Region-color image to use with --labels\n"
        "  --original-means     Use original photo means instead of abstraction colors\n"
        "  --k NUMBER           Graph scale: smaller = more regions (150)\n"
        "  --sigma NUMBER       Graph Gaussian smoothing (0.5)\n"
        "  --min-size NUMBER    Graph minimum region size (20)\n"
        "  --regions NUMBER     Approximate SLICO region count (4000)\n"
        "  --iterations NUMBER  SLICO iterations (10)\n"
        "  --labels FILE        Use existing labels instead of segmenting\n"
        "                       Final 'labels' from YAML, or a label PNG\n"
        "  --save-labels FILE   Save final IDs as integer YAML or 16-bit PNG\n"
        "  --flat FILE          Save the mean-color segmentation image\n"
        "  --boundaries FILE    Save boundaries over the input image\n"
        "  --metric rgb|lab     RGB distance or CIE76 distance (rgb)\n"
        "  --max-side NUMBER    Downsize to this maximum side (0 = original)\n"
        "  --root X Y           Starting pixel (default image center)\n"
        "  --no-histogram       Disable difference histogram matching\n"
        "  --no-lightness       Disable relative lightness constraint\n"
        "  --lightness-tolerance NUMBER  Ignore tiny L* changes (0 = paper)\n"
        "  --repeat NUMBER      Recolor repeatedly; report median timings\n"
        "  --threads NUMBER     OpenCV thread count (1, matching supplied code)\n"
        "  --help               Show this message\n\n"
        "Use PNG output to keep the exact palette colors.\n";
}

int integer(const std::string& value) {
    size_t used = 0;
    int result = std::stoi(value, &used);
    if (used != value.size()) throw std::runtime_error("Invalid integer: " + value);
    return result;
}

double number(const std::string& value) {
    size_t used = 0;
    double result = std::stod(value, &used);
    if (used != value.size() || !std::isfinite(result))
        throw std::runtime_error("Invalid number: " + value);
    return result;
}

Options parseOptions(int argc, char** argv) {
    if (argc < 3) throw std::runtime_error("Provide an input image and an output PNG. Use --help.");
    Options options;
    options.input = argv[1];
    options.output = argv[2];
    for (int i = 3; i < argc; ++i) {
        std::string name = argv[i];
        if (name == "--no-histogram") options.settings.histogramMatching = false;
        else if (name == "--no-lightness") options.settings.preserveLightness = false;
        else if (name == "--original-means") options.originalMeans = true;
        else {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + name);
            std::string value = argv[++i];
            if (name == "--palette") options.paletteFile = value;
            else if (name == "--method") options.method = value;
            else if (name == "--filtered") options.filteredFile = value;
            else if (name == "--save-filtered") options.saveFiltered = value;
            else if (name == "--abstraction") options.abstractionOutput = value;
            else if (name == "--region-image") options.regionImageFile = value;
            else if (name == "--mask") options.abstractionParameters.maskSize = integer(value);
            else if (name == "--gamma") options.abstractionParameters.gamma = number(value);
            else if (name == "--detail-threshold") options.abstractionParameters.detailThreshold = number(value);
            else if (name == "--seed-min-size") options.abstractionParameters.minimumSeedSize = integer(value);
            else if (name == "--hops") options.abstractionParameters.hops = integer(value);
            else if (name == "--growth") options.abstractionParameters.growthFactor = number(value);
            else if (name == "--bins") options.abstractionParameters.histogramBins = integer(value);
            else if (name == "--background") options.abstractionParameters.backgroundRegions = integer(value);
            else if (name == "--labels") options.labelsFile = value;
            else if (name == "--save-labels") options.saveLabels = value;
            else if (name == "--flat") options.flatOutput = value;
            else if (name == "--boundaries") options.boundaryOutput = value;
            else if (name == "--regions") options.regions = integer(value);
            else if (name == "--iterations") options.iterations = integer(value);
            else if (name == "--min-size") options.minSize = integer(value);
            else if (name == "--max-side") options.maxSide = integer(value);
            else if (name == "--repeat") options.repeat = integer(value);
            else if (name == "--threads") options.threads = integer(value);
            else if (name == "--sigma") options.sigma = number(value);
            else if (name == "--k") options.k = number(value);
            else if (name == "--lightness-tolerance") options.settings.lightnessTolerance = number(value);
            else if (name == "--metric") {
                if (value != "rgb" && value != "lab") throw std::runtime_error("Metric must be rgb or lab.");
                options.settings.metric = value == "rgb" ? artistic::Metric::RGB : artistic::Metric::Lab;
            } else if (name == "--root") {
                options.settings.root.x = integer(value);
                if (i + 1 >= argc) throw std::runtime_error("--root needs X and Y.");
                options.settings.root.y = integer(argv[++i]);
                if (options.settings.root.x < 0 || options.settings.root.y < 0)
                    throw std::runtime_error("Root coordinates must be nonnegative.");
            } else throw std::runtime_error("Unknown option: " + name);
        }
    }
    if (options.method != "abstraction" && options.method != "graph" && options.method != "slico")
        throw std::runtime_error("Method must be abstraction, graph, or slico.");
    const auto& p = options.abstractionParameters;
    if (p.maskSize < 1 || p.gamma < 0 || p.detailThreshold < 0 || p.minimumSeedSize < 1 ||
        p.hops < 1 || p.growthFactor <= 0 || p.histogramBins < 1 || p.histogramBins > 256 || p.backgroundRegions < 1)
        throw std::runtime_error("Invalid abstraction parameter.");
    if ((!options.filteredFile.empty() || !options.saveFiltered.empty() || !options.abstractionOutput.empty()) &&
        (options.method != "abstraction" || !options.labelsFile.empty()))
        throw std::runtime_error("--filtered, --save-filtered and --abstraction require fresh abstraction segmentation.");
    if (!options.regionImageFile.empty() && (options.labelsFile.empty() || options.originalMeans))
        throw std::runtime_error("--region-image requires --labels and cannot be combined with --original-means.");
    if (options.regions < 1 || options.iterations < 1 || options.iterations > 100 ||
        options.minSize < 1 || options.sigma <= 0 || options.k <= 0 ||
        options.maxSide < 0 || options.repeat < 1 || options.repeat > 1000 ||
        options.threads < 1 || options.settings.lightnessTolerance < 0)
        throw std::runtime_error("Parameters outside their valid range. Use --help.");
    return options;
}

std::vector<cv::Vec3b> loadPalette(const std::string& path) {
    if (path.empty()) {
        // RGB hex: 241332 563474 9B63C4 E83F6F FF795C FFB47D FFE1A8
        return {{50,19,36}, {116,52,86}, {196,99,155}, {111,63,232},
                {92,121,255}, {125,180,255}, {168,225,255}};
    }
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open palette: " + path);
    std::vector<cv::Vec3b> palette;
    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        std::istringstream stream(line);
        std::string hex;
        stream >> hex;
        if (hex.empty() || hex.substr(0, 2) == "//") continue;
        if (hex[0] == '#') hex.erase(0, 1);
        if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            throw std::runtime_error("Invalid palette color on line " + std::to_string(lineNumber));
        unsigned long rgb = std::stoul(hex, nullptr, 16);
        cv::Vec3b color(static_cast<uchar>(rgb & 255), static_cast<uchar>((rgb >> 8) & 255),
                        static_cast<uchar>((rgb >> 16) & 255));
        if (std::find(palette.begin(), palette.end(), color) == palette.end()) palette.push_back(color);
        if (palette.size() > 256) throw std::runtime_error("Palette is limited to 256 unique colors.");
    }
    if (palette.empty()) throw std::runtime_error("Palette contains no colors.");
    return palette;
}

cv::Mat segment(const cv::Mat& image, const Options& options) {
    cv::Mat labels;
    if (options.method == "graph") {
        auto segmentation = cv::ximgproc::segmentation::createGraphSegmentation(
            options.sigma, static_cast<float>(options.k), options.minSize);
        segmentation->processImage(image, labels);
    } else {
        // SLICO cannot safely initialize a grid in very small/narrow images.
        if (std::min(image.cols, image.rows) < 4)
            throw std::runtime_error("SLICO needs both dimensions >= 4; use --method graph.");
        double size = std::sqrt(static_cast<double>(image.total()) / options.regions);
        int step = std::max(2, static_cast<int>(std::round(size)));
        step = std::min(step, std::min(image.cols, image.rows) / 2);
        cv::Mat blurred, lab;
        cv::GaussianBlur(image, blurred, cv::Size(3, 3), 0.8);
        cv::cvtColor(blurred, lab, cv::COLOR_BGR2Lab);
        auto slic = cv::ximgproc::createSuperpixelSLIC(lab, cv::ximgproc::SLICO, step);
        slic->iterate(options.iterations);
        slic->enforceLabelConnectivity(25);
        slic->getLabels(labels);
    }
    return labels;
}

void saveImage(const std::string& path, const cv::Mat& image) {
    if (!path.empty() && !cv::imwrite(path, image))
        throw std::runtime_error("Cannot write image: " + path);
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return (values[(values.size() - 1) / 2] + values[values.size() / 2]) / 2.0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) { help(); return 0; }
    try {
        Options options = parseOptions(argc, argv);
        cv::setNumThreads(options.threads);
        auto totalStart = std::chrono::steady_clock::now();
        std::vector<cv::Vec3b> palette = loadPalette(options.paletteFile);
        // Ignore EXIF rotation so labels and pixel coordinates refer to file pixels.
        cv::Mat image = cv::imread(options.input, cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
        if (image.empty()) throw std::runtime_error("Cannot read image: " + options.input);
        cv::Mat labels;
        if (!options.labelsFile.empty()) labels = artistic::loadLabels(options.labelsFile, image.size());
        cv::Mat regionImage;
        if (!options.regionImageFile.empty()) {
            regionImage = cv::imread(options.regionImageFile, cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
            if (regionImage.empty() || regionImage.size() != image.size())
                throw std::runtime_error("Region-color image must match the original image dimensions.");
        }
        if (options.maxSide > 0 && std::max(image.cols, image.rows) > options.maxSide) {
            double scale = static_cast<double>(options.maxSide) / std::max(image.cols, image.rows);
            cv::Size size(std::max(1, static_cast<int>(std::round(image.cols * scale))),
                          std::max(1, static_cast<int>(std::round(image.rows * scale))));
            cv::resize(image, image, size, 0, 0, cv::INTER_AREA);
            if (!regionImage.empty()) cv::resize(regionImage, regionImage, size, 0, 0, cv::INTER_NEAREST);
            if (!labels.empty()) {
                // resize does not support CV_32S. Convert losslessly through double.
                cv::Mat temporary;
                labels.convertTo(temporary, CV_64F);
                cv::resize(temporary, temporary, size, 0, 0, cv::INTER_NEAREST);
                temporary.convertTo(labels, CV_32S);
            }
        }
        auto segmentationStart = std::chrono::steady_clock::now();
        abstraction::Result abstractionResult;
        bool ranAbstraction = labels.empty() && options.method == "abstraction";
        if (ranAbstraction) {
            cv::Mat filtered;
            if (!options.filteredFile.empty()) {
                filtered = cv::imread(options.filteredFile, cv::IMREAD_COLOR | cv::IMREAD_IGNORE_ORIENTATION);
                if (filtered.empty()) throw std::runtime_error("Cannot read precomputed CRGF image.");
            }
            options.abstractionParameters.verbose = true;
            abstractionResult = abstraction::abstractImage(image, options.abstractionParameters, filtered);
            labels = abstractionResult.labels; // Final flattened/merged labels, NOT owners.
            regionImage = abstractionResult.image; // Paper input: the region abstraction itself.
        } else if (labels.empty()) labels = segment(image, options);
        double segmentationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - segmentationStart).count();
        // Preserve the supplied method's membership, including diagonal fragment
        // attachments. Explicit label files are authoritative in the same way.
        if (ranAbstraction || !options.labelsFile.empty()) options.settings.splitDisconnectedLabels = false;
        const cv::Mat& colorInput = options.originalMeans || regionImage.empty() ? image : regionImage;

        artistic::Result result;
        std::vector<double> totals, labelsTimes, graphTimes, histogramTimes, treeTimes, assignmentTimes, renderTimes;
        for (int i = 0; i < options.repeat; ++i) {
            auto start = std::chrono::steady_clock::now();
            result = artistic::recolor(colorInput, labels, palette, options.settings);
            totals.push_back(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count());
            labelsTimes.push_back(result.labelsMs);
            graphTimes.push_back(result.graphMs);
            histogramTimes.push_back(result.histogramMs);
            treeTimes.push_back(result.treeMs);
            assignmentTimes.push_back(result.assignmentMs);
            renderTimes.push_back(result.renderMs);
        }

        if (!options.saveLabels.empty()) artistic::saveLabels(options.saveLabels, result.labels);
        saveImage(options.output, result.image);
        saveImage(options.flatOutput, result.flatImage);
        if (ranAbstraction) {
            saveImage(options.abstractionOutput, abstractionResult.image);
            saveImage(options.saveFiltered, abstractionResult.smoothed);
        }
        if (!options.boundaryOutput.empty()) {
            cv::Mat overlay = image.clone();
            for (int y = 0; y < image.rows; ++y)
                for (int x = 0; x < image.cols; ++x) {
                    int id = result.labels.at<int>(y, x);
                    if ((x + 1 < image.cols && id != result.labels.at<int>(y, x + 1)) ||
                        (y + 1 < image.rows && id != result.labels.at<int>(y + 1, x)))
                        overlay.at<cv::Vec3b>(y, x) = cv::Vec3b(0, 0, 0);
                }
            saveImage(options.boundaryOutput, overlay);
        }
        double totalMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - totalStart).count();
        std::cout << std::fixed << std::setprecision(3)
            << "OpenCV " << CV_VERSION << ", threads: " << cv::getNumThreads() << '\n'
            << "Image: " << image.cols << " x " << image.rows << '\n'
            << "Segmentation method: " << (options.labelsFile.empty() ? options.method : "supplied labels") << '\n'
            << "Region colors: " << (options.originalMeans || regionImage.empty() ? "original photo means" : "abstraction/region image") << '\n'
            << "Regions: " << result.regionCount << ", edges: " << result.edgeCount
            << ", palette colors: " << palette.size() << '\n'
            << "Segmentation: " << segmentationMs << " ms (once; 0 if labels supplied)\n"
            << "Recoloring median (" << options.repeat << " runs): " << median(totals) << " ms\n"
            << "  Label normalization: " << median(labelsTimes) << " ms\n"
            << "  Region graph:     " << median(graphTimes) << " ms\n"
            << "  Histogram:        " << median(histogramTimes) << " ms\n"
            << "  Widest-path tree: " << median(treeTimes) << " ms\n"
            << "  Color assignment: " << median(assignmentTimes) << " ms\n"
            << "  Rendering:        " << median(renderTimes) << " ms\n"
            << "Lightness fallbacks: " << result.lightnessFallbacks << '\n'
            << "Wall time (all repeats + load/save): " << totalMs << " ms\n"
            << "Saved: " << options.output << '\n';
        if (ranAbstraction) {
            const auto& p = options.abstractionParameters;
            std::cout << "Abstraction parameters: mask=" << p.maskSize << ", gamma=" << p.gamma
                << ", threshold=" << p.detailThreshold << ", seed-min-size=" << p.minimumSeedSize
                << ", hops=" << p.hops << ", growth=" << p.growthFactor << ", bins=" << p.histogramBins
                << ", background=" << p.backgroundRegions << '\n'
                << "  CRGF: " << abstractionResult.filteringSeconds * 1000 << " ms\n"
                << "  Seeds: " << abstractionResult.seedSeconds * 1000 << " ms\n"
                << "  Growth: " << abstractionResult.growthSeconds * 1000 << " ms\n"
                << "  Abstraction rendering: " << abstractionResult.renderingSeconds * 1000 << " ms\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
