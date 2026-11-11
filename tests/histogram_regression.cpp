#include "core/display_histogram.h"
#include "core/raw_plane_histogram.h"
#include "core/display_color_space.h"
#include "core/raw_plane_access.h"
#include "io/raw_image_decoder.h"
#include "io/raw_preset_store.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QtEndian>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <future>
#include <thread>
using namespace mvpview;
constexpr qint64 exactSamples = std::numeric_limits<qint64>::max();
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void compare(const DisplayHistogram& actual, const DisplayHistogram& expected) {
    const auto channel = [](const HistogramChannel& a, const HistogramChannel& b) {
        return a.bins == b.bins && a.minimum == b.minimum && a.maximum == b.maximum &&
               a.mean == b.mean && a.standardDeviation == b.standardDeviation;
    };
    check(channel(actual.red, expected.red) && channel(actual.green, expected.green) &&
          channel(actual.blue, expected.blue) && channel(actual.luma, expected.luma),
          "display histogram differs from reference RGB pixels");
    check(actual.sampledPixelCount == expected.sampledPixelCount &&
          actual.availablePixelCount == expected.availablePixelCount &&
          actual.logicalRegion == expected.logicalRegion, "histogram metadata differs");
}

// Reference uses the checked public per-pixel accessor, independent of the bulk reader.
void checkSource(const ImageFrame& frame, const QRectF& region, qint64 maximum) {
    const auto actual = RawPlaneHistogramAnalyzer::analyzeRegion(frame, region, maximum);
    const RawPlaneAccessor accessor(frame);
    check(actual.isValid() && actual.channels.size() == 3, "source histogram invalid");
    for (int channel = 0; channel < 3; ++channel) {
        const QRect source = actual.sourceRegion;
        const QRect grid = channel == 0 ? source : QRect(QPoint(source.left() / 2, source.top() / 2),
                                                         QPoint(source.right() / 2, source.bottom() / 2));
        int rows = grid.height(), columns = grid.width();
        if (qint64(rows) * columns > maximum) {
            rows = std::clamp(int(std::floor(std::sqrt(double(maximum) * rows / columns))),
                              1, int(std::min<qint64>(rows, maximum)));
            columns = int(std::clamp(maximum / rows, qint64(1), qint64(columns)));
        }
        QVector<quint64> bins(actual.maximumValue + 1);
        quint64 sum = 0, squared = 0;
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                QPoint point = detail::histogramSamplePoint(grid, column, row, columns, rows);
                if (channel != 0) point *= 2;
                const auto sample = accessor.yuvAtSourcePixel(point);
                check(sample.has_value(), "source reference read failed");
                const int value = channel == 0 ? sample->y : channel == 1 ? sample->u : sample->v;
                ++bins[value]; sum += quint64(value); squared += quint64(value) * value;
            }
        }
        const qint64 count = qint64(rows) * columns;
        const double mean = double(sum) / double(count);
        const auto& got = actual.channels[channel];
        check(got.bins == bins && got.sampledSampleCount == count && got.mean == mean &&
              got.standardDeviation == std::sqrt(std::max(0.0, double(squared) / double(count) - mean * mean)),
              "source histogram differs from checked accessor");
    }
}

QString digest(const QVector<quint64>& bins) {
    QByteArray data; for (auto count : bins) data += QByteArray::number(count) + ',';
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

void checkDefaultSampling() {
    constexpr int edge = 1024;
    constexpr qint64 limit = DisplayHistogramAnalyzer::kDefaultMaximumSamples;
    QImage checker(edge, edge, QImage::Format_RGBA64);
    for (int y = 0; y < edge; ++y) {
        auto* row = reinterpret_cast<QRgba64*>(checker.scanLine(y));
        for (int x = 0; x < edge; ++x) {
            const quint16 value = ((x + y) & 1) ? 65535 : 0;
            row[x] = QRgba64::fromRgba64(value, value, value, 65535);
        }
    }
    ImageFrame rgb; rgb.storage = checker; rgb.descriptor.size = checker.size();
    const auto display = DisplayHistogramAnalyzer::analyze(rgb);
    const auto native = DisplayHistogramAnalyzer::analyzeNativeRgb(rgb);
    check(display.sampledPixelCount == limit && native.sampledPixelCount == limit &&
          display.isSubsampled() && native.isSubsampled(), "RGB default sampling not bounded");
    // A regular four-pixel step sees only black here. Jitter must retain both populations.
    check(std::abs(display.luma.mean - 127.5) < 1.5 &&
          std::abs(native.luma.mean / 257 - 127.5) < 1.5, "checkerboard sampling alias");
    compare(display, DisplayHistogramAnalyzer::analyze(rgb));
    for (const auto format : {RawPixelFormat::NV12, RawPixelFormat::Raw16}) {
        ImageFrame frame;
        RawImageParameters p; p.size = {edge, edge}; p.format = format;
        frame.rawParameters = p;
        auto planes = std::make_shared<PlaneBufferSet>();
        if (p.isYuv()) {
            planes->storage = QByteArray(edge * edge * 3 / 2, char(128));
            planes->planes = {{0, edge, edge * edge}, {edge * edge, edge, edge * edge / 2}};
            for (int y = 0; y < edge; ++y)
                for (int x = 0; x < edge; ++x)
                    planes->storage[y * edge + x] = ((x + y) & 1) ? char(255) : char(0);
        } else {
            planes->storage = QByteArray(edge * edge * 2, Qt::Uninitialized);
            planes->planes = {{0, edge * 2, edge * edge * 2}};
            for (int y = 0; y < edge; ++y)
                for (int x = 0; x < edge; ++x)
                    qToLittleEndian<quint16>(quint16(1000 * (1 + 2 * (y & 1) + (x & 1))),
                                            planes->storage.data() + 2 * (y * edge + x));
        }
        frame.storage = std::shared_ptr<const PlaneBufferSet>(planes);
        const auto source = RawPlaneHistogramAnalyzer::analyze(frame);
        check(source.channels.size() == (p.isYuv() ? 3 : 4), "native channels missing");
        for (int i = 0; i < source.channels.size(); ++i) {
            const auto& channel = source.channels[i];
            check(channel.sampledSampleCount == limit && channel.isSubsampled(),
                  "native default sampling not bounded");
            if (!p.isYuv())
                check(channel.bins[(i + 1) * 1000] == quint64(limit), "Bayer samples crossed CFA channels");
        }
        if (p.isYuv()) check(std::abs(source.channels[0].mean - 127.5) < 1.5,
                             "source checkerboard sampling alias");
    }
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        RawImageDecoder decoder;
        if (app.arguments().size() > 1) {
            const auto path = app.arguments().at(1);
            auto parameters = RawPresetStore::inferFromFileName(path);
            parameters.format = RawPixelFormat::NV12;
            auto decoded = decoder.decode({path, DecodePurpose::Full, {}, parameters});
            check(decoded.succeeded(), "decode failed");
            QElapsedTimer timer; timer.start();
            auto display = DisplayHistogramAnalyzer::analyze(*decoded.frame, exactSamples);
            std::cout << "display " << timer.elapsed() << " ms samples " << display.sampledPixelCount
                      << " hash " << digest(display.luma.bins).toStdString() << std::endl;
            for (const qint64 budget : {qint64(16384), qint64(65536), qint64(262144)}) {
                timer.restart();
                const auto sampled = DisplayHistogramAnalyzer::analyzeLuma(*decoded.frame, {}, budget);
                const auto elapsed = timer.nsecsElapsed();
                double exactCdf = 0, sampledCdf = 0, maxCdfError = 0;
                for (int bin = 0; bin < 256; ++bin) {
                    exactCdf += double(display.luma.bins[bin]) / double(display.sampledPixelCount);
                    sampledCdf += double(sampled.luma.bins[bin]) / double(sampled.sampledPixelCount);
                    maxCdfError = std::max(maxCdfError, std::abs(exactCdf - sampledCdf));
                }
                std::cout << "sample budget " << budget << " actual " << sampled.sampledPixelCount
                          << " ms " << double(elapsed) / 1e6
                          << " mean error " << std::abs(sampled.luma.mean - display.luma.mean)
                          << " max CDF error % " << 100 * maxCdfError << '\n';
            }
            timer.restart();
            auto source = RawPlaneHistogramAnalyzer::analyze(*decoded.frame);
            std::cout << "source " << timer.elapsed() << " ms hash " << digest(source.channels.first().bins).toStdString() << std::endl;
            return 0;
        }
        QTemporaryDir root(QDir::currentPath() + "/histogram-XXXXXX");
        check(root.isValid(), "temporary directory failed");
        checkDefaultSampling();
        for (auto format : {RawPixelFormat::NV12, RawPixelFormat::NV21, RawPixelFormat::I420, RawPixelFormat::P010}) {
            for (int variant = 0; variant < 14; ++variant) {
                RawImageParameters p; p.size = variant & 1 ? QSize(17, 13) : QSize(32, 24);
                if (variant >= 8) p.size = QSize(1200, 900); // Exercise parallel accumulation.
                p.format = format; p.orientation = ImageOrientation(variant % 4);
                p.littleEndian = variant & 1; p.msbAligned = variant & 2;
                p.range = variant & 4 ? QuantizationRange::Full : QuantizationRange::Limited;
                p.chromaLocation = variant & 1 ? ChromaLocation::Left : ChromaLocation::Center;
                p.yuvMatrix = YuvMatrix(variant % 3); p.yuvPrimaries = YuvPrimaries(variant % 4);
                p.yuvTransfer = YuvTransfer(variant % 3);
                p.rowStride = minimumRowStride(p) + 8; p.chromaStride = minimumChromaRowStride(p) + 6;
                setCurrentDisplayColorSpace(DisplayColorSpace(variant % 4));
                if (variant >= 10) {
                    // Exercise the exact byte-transfer cache for every source transfer,
                    // including odd dimensions, both ranges and both chroma locations.
                    p.size = QSize(1001, 1001);
                    p.yuvPrimaries = YuvPrimaries::BT709;
                    p.rowStride = minimumRowStride(p) + 8;
                    p.chromaStride = minimumChromaRowStride(p) + 6;
                    setCurrentDisplayColorSpace(DisplayColorSpace::Srgb);
                }
                QByteArray bytes(frameByteSize(p), Qt::Uninitialized);
                quint32 random = 0x1a7b349dU;
                for (qsizetype i = 0; i < bytes.size(); ++i) {
                    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                    bytes[i] = variant >= 10 ? char(random & 255U) : char((i * 17 + i / 13) % 256);
                }
                const QString path = root.path() + "/fixture.yuv";
                QFile file(path); check(file.open(QIODevice::WriteOnly), "open failed");
                check(file.write(bytes) == bytes.size(), "write failed"); file.close();
                DecodeRequest request{path, DecodePurpose::Full, {}, p}; request.requireDisplayImage = true;
                auto decoded = decoder.decode(request); check(decoded.succeeded(), "fixture decode failed");
                ImageFrame rgb; rgb.storage = *decoded.frame->qImage(); rgb.descriptor.size = decoded.frame->descriptor.size;
                for (auto region : {QRectF(0, 0, 1, 1), QRectF(.1, .2, .65, .6)}) {
                    for (qint64 maximum : {qint64(37), DisplayHistogramAnalyzer::kDefaultMaximumSamples, exactSamples}) {
                        auto actual = DisplayHistogramAnalyzer::analyzeRegion(*decoded.frame, region, maximum);
                        auto expected = DisplayHistogramAnalyzer::analyzeRegion(rgb, region, maximum);
                        compare(actual, expected);
                        checkSource(*decoded.frame, region, maximum);
                        if (region == QRectF(0, 0, 1, 1) &&
                            maximum == DisplayHistogramAnalyzer::kDefaultMaximumSamples) {
                            expected.red = {}; expected.green = {}; expected.blue = {};
                            compare(DisplayHistogramAnalyzer::analyzeLuma(*decoded.frame, {}, maximum), expected);
                            compare(DisplayHistogramAnalyzer::analyzeLuma(rgb, {}, maximum), expected);
                        }
                    }
                }
                check(!DisplayHistogramAnalyzer::analyze(*decoded.frame, 100, [] { return true; }).isValid(), "cancellation failed");
                check(!DisplayHistogramAnalyzer::analyzeLuma(*decoded.frame, [] { return true; }).isValid(),
                      "luma cancellation failed");
                int calls = 0;
                check(!RawPlaneHistogramAnalyzer::analyze(*decoded.frame,
                    RawPlaneHistogramAnalyzer::kDefaultMaximumSamplesPerChannel,
                    [&] { return ++calls >= 3; }).isValid(), "source mid-analysis cancellation failed");
                if (variant == 8 && format == RawPixelFormat::NV12) {
                    const auto expected = DisplayHistogramAnalyzer::analyze(rgb, exactSamples);
                    std::array<std::future<DisplayHistogram>, 4> pending;
                    for (auto& future : pending)
                        future = std::async(std::launch::async, [&] {
                            return DisplayHistogramAnalyzer::analyze(*decoded.frame, exactSamples);
                        });
                    for (auto& future : pending) compare(future.get(), expected);
                    calls = 0;
                    const auto caller = std::this_thread::get_id();
                    check(!DisplayHistogramAnalyzer::analyze(*decoded.frame,
                        exactSamples, [&] {
                            check(std::this_thread::get_id() == caller, "callback thread changed");
                            return ++calls >= 5;
                        }).isValid(), "parallel cancellation failed");
                    compare(DisplayHistogramAnalyzer::analyze(*decoded.frame, exactSamples), expected);
                }
                if (variant == 0) {
                    auto planes = std::make_shared<PlaneBufferSet>(
                        *std::get<std::shared_ptr<const PlaneBufferSet>>(decoded.frame->storage));
                    planes->storage.chop(1);
                    planes->displayImage = {};
                    ImageFrame invalid = *decoded.frame;
                    invalid.storage = std::shared_ptr<const PlaneBufferSet>(planes);
                    check(!DisplayHistogramAnalyzer::analyze(invalid).isValid() &&
                          !RawPlaneHistogramAnalyzer::analyze(invalid).isValid(),
                          "truncated planes accepted by histogram reader");
                }
            }
        }
        ImageFrame rgb16;
        QImage high(19, 17, QImage::Format_RGBA64);
        for (int y = 0; y < high.height(); ++y) {
            auto* row = reinterpret_cast<QRgba64*>(high.scanLine(y));
            for (int x = 0; x < high.width(); ++x)
                row[x] = QRgba64::fromRgba64(quint16(x * 3456), quint16(y * 4000),
                                           quint16((x + y) * 1900), 65535);
        }
        rgb16.storage = high;
        auto expected = DisplayHistogramAnalyzer::analyze(rgb16);
        expected.red = {}; expected.green = {}; expected.blue = {};
        compare(DisplayHistogramAnalyzer::analyzeLuma(rgb16), expected);
        std::cout << "YUV display/source/luma histogram pixel/ROI/sample/color/concurrency/cancellation parity passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
