#include "io/encoded_color_management.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <optional>

namespace ispview {
namespace {

constexpr int kMeasuredRuns = 3;

struct Measurement {
    double medianMilliseconds = 0.0;
    double imageMiB = 0.0;
};

std::optional<Measurement> measure(const QSize& size) {
    const QColorSpace linearSrgb(QColorSpace::SRgbLinear);
    if (!linearSrgb.isValid() || linearSrgb.iccProfile().isEmpty()) {
        return std::nullopt;
    }
    std::array<qint64, kMeasuredRuns> elapsed{};
    qsizetype imageBytes = 0;
    for (qint64& nanoseconds : elapsed) {
        QImage image(size, QImage::Format_RGBA8888);
        if (image.isNull()) {
            return std::nullopt;
        }
        image.fill(QColor(128, 64, 32, 192));
        image.setColorSpace(linearSrgb);
        ImageMetadata metadata;
        QElapsedTimer timer;
        timer.start();
        EncodedColorManagement::normalizeToDisplay(image, metadata,
                                                   DisplayColorSpace::Srgb);
        nanoseconds = timer.nsecsElapsed();
        if (!metadata.colorProfile || !metadata.colorProfile->converted ||
            !metadata.colorWarning.isEmpty() || image.pixelColor(0, 0).alpha() != 192) {
            return std::nullopt;
        }
        imageBytes = image.sizeInBytes();
    }
    std::sort(elapsed.begin(), elapsed.end());
    return Measurement{elapsed.at(kMeasuredRuns / 2) / 1'000'000.0,
                       imageBytes / (1024.0 * 1024.0)};
}

bool printMeasurement(QTextStream& output, const QString& label, const QSize& size) {
    const auto result = measure(size);
    if (!result) {
        return false;
    }
    output << label << '\t' << size.width() << 'x' << size.height() << '\t'
           << QString::number(result->medianMilliseconds, 'f', 2) << '\t'
           << QString::number(result->imageMiB, 'f', 2) << '\n';
    output.flush();
    return true;
}

} // namespace
} // namespace ispview

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    if (!ispview::EncodedColorManagement::isAvailable()) {
        output << "LittleCMS is not available in this build\n";
        return 0;
    }
    output << "Input\tSize\tICCMedianMs\tImageMiB\n";
    if (!ispview::printMeasurement(output, QStringLiteral("12MP linear-sRGB"), {4000, 3000})) {
        return 1;
    }
    if (application.arguments().contains(QStringLiteral("--48mp")) &&
        !ispview::printMeasurement(output, QStringLiteral("48MP linear-sRGB"), {8000, 6000})) {
        return 1;
    }
    return 0;
}
