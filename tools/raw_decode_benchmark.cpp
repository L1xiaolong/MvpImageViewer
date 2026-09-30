#include "io/raw_image_decoder.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <optional>

namespace mvpview {
namespace {

constexpr QSize kDefaultFrameSize{3840, 2160};
constexpr QSize kLargeFrameSize{8000, 6000};
constexpr QSize kPreviewSize{960, 720};
constexpr int kMeasuredRuns = 3;

struct Measurement {
    double medianMilliseconds = 0.0;
    double frameMiB = 0.0;
    QSize outputSize;
};

bool writeFixture(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray nv12Fixture(const QSize& size) {
    RawImageParameters parameters;
    parameters.size = size;
    parameters.format = RawPixelFormat::NV12;
    QByteArray bytes(frameByteSize(parameters), static_cast<char>(0x80));
    return bytes;
}

QByteArray p010Fixture(const QSize& size) {
    RawImageParameters parameters;
    parameters.size = size;
    parameters.format = RawPixelFormat::P010;
    QByteArray bytes(frameByteSize(parameters), Qt::Uninitialized);
    const quint16 neutral = static_cast<quint16>(512U << 6U);
    for (qsizetype offset = 0; offset < bytes.size(); offset += 2) {
        qToLittleEndian(neutral, reinterpret_cast<uchar*>(bytes.data() + offset));
    }
    return bytes;
}

std::optional<Measurement> measure(const QString& path, RawImageParameters parameters,
                                   DecodePurpose purpose, const QSize& maximumSize) {
    RawImageDecoder decoder;
    const DecodeRequest request(path, purpose, maximumSize, parameters);
    if (!decoder.decode(request).succeeded()) {
        return std::nullopt;
    }

    std::array<qint64, kMeasuredRuns> elapsedNanoseconds{};
    ImageFramePtr lastFrame;
    for (qint64& elapsed : elapsedNanoseconds) {
        lastFrame.reset();
        QElapsedTimer timer;
        timer.start();
        DecodeResult result = decoder.decode(request);
        elapsed = timer.nsecsElapsed();
        if (!result.frame) {
            return std::nullopt;
        }
        lastFrame = std::move(result.frame);
    }
    std::sort(elapsedNanoseconds.begin(), elapsedNanoseconds.end());
    return Measurement{elapsedNanoseconds.at(kMeasuredRuns / 2) / 1'000'000.0,
                       lastFrame->byteSize() / (1024.0 * 1024.0),
                       lastFrame->descriptor.size};
}

bool printMeasurements(QTextStream& output, const QString& label, const QString& path,
                       RawImageParameters parameters) {
    const auto preview = measure(path, parameters, DecodePurpose::Preview, kPreviewSize);
    const auto full = measure(path, parameters, DecodePurpose::Full, {});
    if (!preview || !full) {
        return false;
    }
    const QString name = rawPixelFormatName(parameters.format);
    output << label << "\t" << name << "\tPreview\t"
           << QString::number(preview->medianMilliseconds, 'f', 2)
           << "\t" << QString::number(preview->frameMiB, 'f', 2) << "\t"
           << preview->outputSize.width() << "x" << preview->outputSize.height() << '\n';
    output << label << "\t" << name << "\tFull\t"
           << QString::number(full->medianMilliseconds, 'f', 2) << "\t"
           << QString::number(full->frameMiB, 'f', 2) << "\t" << full->outputSize.width()
           << "x" << full->outputSize.height() << '\n';
    return true;
}

bool runFrameSize(QTextStream& output, const QString& directory, const QString& label,
                  const QSize& size) {
    const QString nv12Path = directory + QStringLiteral("/%1_nv12.yuv").arg(label);
    const QString p010Path = directory + QStringLiteral("/%1_p010.yuv").arg(label);
    if (!writeFixture(nv12Path, nv12Fixture(size)) ||
        !writeFixture(p010Path, p010Fixture(size))) {
        return false;
    }
    RawImageParameters nv12;
    nv12.size = size;
    nv12.format = RawPixelFormat::NV12;
    RawImageParameters p010 = nv12;
    p010.format = RawPixelFormat::P010;
    p010.msbAligned = true;
    return printMeasurements(output, label, nv12Path, nv12) &&
           printMeasurements(output, label, p010Path, p010);
}

} // namespace
} // namespace mvpview

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    QTextStream output(stdout);
    if (!directory.isValid()) {
        output << "Could not create benchmark directory\n";
        return 1;
    }

    output << "Source\tFormat\tPurpose\tMedianMs\tFrameMiB\tOutput\n";
    if (!mvpview::runFrameSize(output, directory.path(), QStringLiteral("4K"),
                               mvpview::kDefaultFrameSize)) {
        output << "Decode failed\n";
        return 2;
    }
    if (application.arguments().contains(QStringLiteral("--48mp")) &&
        !mvpview::runFrameSize(output, directory.path(), QStringLiteral("48MP"),
                               mvpview::kLargeFrameSize)) {
        output << "48MP decode failed\n";
        return 3;
    }
    return 0;
}
