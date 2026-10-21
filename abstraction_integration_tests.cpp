#include "recolor.h"
#include "label_io.h"
#include "image_abstraction.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <stdexcept>

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        cv::setNumThreads(1);
        const std::vector<cv::Vec3b> palette = {{0,0,0}, {255,255,255}, {40,90,220}};
        artistic::Settings settings;
        settings.splitDisconnectedLabels = false;
        // Supplied cleanup can join a fragment diagonally. Preserve membership.
        cv::Mat checker = (cv::Mat_<int>(2,2) << 0,1,1,0);
        cv::Mat tiny(2,2,CV_8UC3,cv::Scalar(90,110,150));
        auto result = artistic::recolor(tiny, checker, palette, settings);
        check(result.regionCount == 2 && cv::norm(result.labels, checker, cv::NORM_INF) == 0,
              "Diagonal fragments were split or relabeled incorrectly.");

        // YAML has no 16-bit ID limit and must read labels rather than owners.
        cv::Mat largeIds = (cv::Mat_<int>(2,2) << 0,70000,70000,0);
        {
            cv::FileStorage file("integration_labels.yml", cv::FileStorage::WRITE);
            file << "owners" << cv::Mat(2,2,CV_32S,cv::Scalar(-1)) << "labels" << largeIds;
        }
        cv::Mat loaded = artistic::loadLabels("integration_labels.yml", tiny.size());
        check(cv::norm(loaded, largeIds, cv::NORM_INF) == 0, "YAML read the wrong matrix or truncated IDs.");
        artistic::saveLabels("integration_saved.yml", loaded);
        check(cv::norm(artistic::loadLabels("integration_saved.yml", tiny.size()), loaded, cv::NORM_INF) == 0,
              "32-bit YAML round trip failed.");
        {
            cv::FileStorage file("integration_owners.yml", cv::FileStorage::WRITE);
            file << "owners" << checker;
        }
        bool rejected = false;
        try { artistic::loadLabels("integration_owners.yml", tiny.size()); }
        catch (const std::exception&) { rejected = true; }
        check(rejected, "An owners-only file was accepted as final segmentation.");

        // Full supplied pipeline, exact labels through recoloring, and reuse of
        // the same CRGF image. This checks the integration without approximating
        // the abstraction by its rendered colors.
        cv::Mat image(24,32,CV_8UC3);
        for (int y = 0; y < image.rows; ++y)
            for (int x = 0; x < image.cols; ++x)
                image.at<cv::Vec3b>(y,x) = cv::Vec3b(20 + 4*x, 30 + 6*y, (x/4)%2 ? 220 : 40);
        cv::circle(image, cv::Point(16,12), 5, cv::Scalar(0,0,0), 2);
        cv::Mat original = image.clone();
        abstraction::Parameters parameters;
        parameters.maskSize = 8;
        parameters.backgroundRegions = 16;
        auto reference = abstraction::abstractImage(image, parameters);
        auto cached = abstraction::abstractImage(image, parameters, reference.smoothed);
        check(cv::norm(reference.labels, cached.labels, cv::NORM_INF) == 0, "CRGF reuse changed the source labels.");
        result = artistic::recolor(reference.image, reference.labels, palette, settings);
        check(cv::norm(result.labels, reference.labels, cv::NORM_INF) == 0, "Supplied segmentation was changed.");
        check(cv::norm(result.flatImage, reference.image, cv::NORM_INF) == 0,
              "The abstraction's representative region colors were changed.");
        check(cv::norm(image, original, cv::NORM_INF) == 0, "Input image was modified.");
        artistic::saveLabels("integration_saved.yml", reference.labels);
        auto reused = artistic::recolor(reference.image, artistic::loadLabels("integration_saved.yml", image.size()), palette, settings);
        check(cv::norm(result.image, reused.image, cv::NORM_INF) == 0, "Saved label reuse changed the recoloring.");
        for (int y = 0; y < image.rows; ++y)
            for (int x = 0; x < image.cols; ++x)
                check(std::find(palette.begin(), palette.end(), result.image.at<cv::Vec3b>(y,x)) != palette.end(),
                      "Output contains a non-palette color.");
        for (const char* path : {"integration_labels.yml", "integration_saved.yml", "integration_owners.yml"}) std::remove(path);
        std::cout << "Supplied abstraction labels, caching, and YAML integration checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
