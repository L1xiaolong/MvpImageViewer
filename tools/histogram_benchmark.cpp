#include "core/display_histogram.h"
#include "core/raw_plane_histogram.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <optional>

namespace mvpview {
namespace {

constexpr int kMeasuredRuns = 5;

struct Measurement {
    double medianMilliseconds = 0.0;
    qint64 sampledPixels = 0;
};

std::optional<Measurement> measure(const QSize& size, const std::optional<QRectF>& region) {
    QImage image(size, QImage::Format_RGBA8888);
    if (image.isNull()) {
        return std::nullopt;
    }
    image.fill(QColor(96, 144, 208, 255));
    ImageFrame frame;
    frame.descriptor.size = size;
    frame.storage = std::move(image);
    const auto analyze = [&] {
        return region ? DisplayHistogramAnalyzer::analyzeRegion(frame, *region)
                      : DisplayHistogramAnalyzer::analyze(frame);
    };
    if (!analyze().isValid()) {
        return std::nullopt;
    }

    std::array<qint64, kMeasuredRuns> elapsed{};
    DisplayHistogram last;
    for (qint64& nanoseconds : elapsed) {
        QElapsedTimer timer;
        timer.start();
        last = analyze();
        nanoseconds = timer.nsecsElapsed();
        if (!last.isValid()) {
            return std::nullopt;
        }
    }
    std::sort(elapsed.begin(), elapsed.end());
    return Measurement{elapsed.at(kMeasuredRuns / 2) / 1'000'000.0,
                       last.sampledPixelCount};
}

std::optional<Measurement> measureRaw(const QSize& size, bool yuv,
                                      const std::optional<QRectF>& region) {
    RawImageParameters parameters;
    parameters.size = size;
    parameters.format = yuv ? RawPixelFormat::NV12 : RawPixelFormat::Raw16;
    parameters.validBitsOverride = yuv ? 0 : 14;
    auto storage = std::make_shared<PlaneBufferSet>();
    const qsizetype primaryStride = minimumRowStride(parameters);
    const qsizetype primaryBytes = primaryStride * size.height();
    const qsizetype totalBytes = frameByteSize(parameters);
    if (primaryStride <= 0 || totalBytes <= 0) {
        return std::nullopt;
    }
    storage->storage.resize(totalBytes);
    storage->storage.fill(yuv ? static_cast<char>(128) : '\0');
    storage->planes.push_back({0, primaryStride, primaryBytes});
    if (yuv) {
        const qsizetype chromaStride = minimumChromaRowStride(parameters);
        storage->planes.push_back({primaryBytes, chromaStride, totalBytes - primaryBytes});
    }
    ImageFrame frame;
    frame.descriptor.size = size;
    frame.rawParameters = parameters;
    frame.storage = std::shared_ptr<const PlaneBufferSet>(storage);
    const auto analyze = [&] {
        return region ? RawPlaneHistogramAnalyzer::analyzeRegion(frame, *region)
                      : RawPlaneHistogramAnalyzer::analyze(frame);
    };
    if (!analyze().isValid()) {
        return std::nullopt;
    }

    std::array<qint64, kMeasuredRuns> elapsed{};
    RawPlaneHistogram last;
    for (qint64& nanoseconds : elapsed) {
        QElapsedTimer timer;
        timer.start();
        last = analyze();
        nanoseconds = timer.nsecsElapsed();
        if (!last.isValid()) {
            return std::nullopt;
        }
    }
    std::sort(elapsed.begin(), elapsed.end());
    qint64 samples = 0;
    for (const RawHistogramChannel& channel : last.channels) {
        samples += channel.sampledSampleCount;
    }
    return Measurement{elapsed.at(kMeasuredRuns / 2) / 1'000'000.0, samples};
}

bool printMeasurement(QTextStream& output, const QString& label, const QString& scope,
                      const QSize& size, const std::optional<QRectF>& region = std::nullopt) {
    const auto measurement = measure(size, region);
    if (!measurement) {
        return false;
    }
    output << label << '\t' << scope << '\t' << size.width() << 'x' << size.height() << '\t'
           << measurement->sampledPixels << '\t'
           << QString::number(measurement->medianMilliseconds, 'f', 3) << '\n';
    return true;
}

bool printRawMeasurement(QTextStream& output, const QString& label, const QString& scope,
                         const QSize& size, bool yuv,
                         const std::optional<QRectF>& region = std::nullopt) {
    const auto measurement = measureRaw(size, yuv, region);
    if (!measurement) {
        return false;
    }
    output << label << '\t' << scope << '\t' << size.width() << 'x' << size.height() << '\t'
           << measurement->sampledPixels << '\t'
           << QString::number(measurement->medianMilliseconds, 'f', 3) << '\n';
    return true;
}

} // namespace
} // namespace mvpview

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QTextStream output(stdout);
    output << "Source\tScope\tSize\tSamples\tMedianMs\n";
    if (!mvpview::printMeasurement(output, QStringLiteral("4K"), QStringLiteral("Full"),
                                   {3840, 2160}) ||
        !mvpview::printMeasurement(output, QStringLiteral("4K"), QStringLiteral("Center25%"),
                                   {3840, 2160}, QRectF(0.25, 0.25, 0.5, 0.5)) ||
        !mvpview::printRawMeasurement(output, QStringLiteral("4K NV12 Source"),
                                      QStringLiteral("Full"), {3840, 2160}, true) ||
        !mvpview::printRawMeasurement(output, QStringLiteral("4K RAW14 Source"),
                                      QStringLiteral("Full"), {3840, 2160}, false)) {
        return 1;
    }
    if (application.arguments().contains(QStringLiteral("--48mp"))) {
        if (!mvpview::printMeasurement(output, QStringLiteral("48MP"), QStringLiteral("Full"),
                                       {8000, 6000}) ||
            !mvpview::printMeasurement(output, QStringLiteral("48MP"),
                                       QStringLiteral("Center25%"), {8000, 6000},
                                       QRectF(0.25, 0.25, 0.5, 0.5)) ||
            !mvpview::printRawMeasurement(output, QStringLiteral("48MP NV12 Source"),
                                          QStringLiteral("Full"), {8000, 6000}, true) ||
            !mvpview::printRawMeasurement(output, QStringLiteral("48MP RAW14 Source"),
                                          QStringLiteral("Full"), {8000, 6000}, false)) {
            return 1;
        }
    }
    return 0;
}
