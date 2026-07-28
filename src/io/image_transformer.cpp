#include "io/image_transformer.h"

#include "io/raw_preset_store.h"

#include <QFile>
#include <QFileInfo>
#include <QColorSpace>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace ispview {
namespace {

double cubicWeight(double value) {
    // Keys' cubic convolution with a=-0.5 (Catmull-Rom): interpolating, sharp, and separable.
    const double x = std::abs(value);
    if (x <= 1.0) return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    if (x < 2.0) return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    return 0.0;
}

std::size_t planeIndex(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x);
}

template <typename T>
std::vector<T> resizePlane(const std::vector<T>& source, int sourceWidth, int sourceHeight,
                           int targetWidth, int targetHeight, int maximum) {
    std::vector<T> result(static_cast<std::size_t>(targetWidth) *
                          static_cast<std::size_t>(targetHeight));
    for (int y = 0; y < targetHeight; ++y) {
        const double sourceY = (y + 0.5) * sourceHeight / targetHeight - 0.5;
        const int baseY = static_cast<int>(std::floor(sourceY));
        for (int x = 0; x < targetWidth; ++x) {
            const double sourceX = (x + 0.5) * sourceWidth / targetWidth - 0.5;
            const int baseX = static_cast<int>(std::floor(sourceX));
            double sum = 0.0;
            double weights = 0.0;
            for (int ky = -1; ky <= 2; ++ky) {
                const int sy = std::clamp(baseY + ky, 0, sourceHeight - 1);
                const double wy = cubicWeight(sourceY - (baseY + ky));
                for (int kx = -1; kx <= 2; ++kx) {
                    const int sx = std::clamp(baseX + kx, 0, sourceWidth - 1);
                    const double weight = wy * cubicWeight(sourceX - (baseX + kx));
                    sum += source[planeIndex(sx, sy, sourceWidth)] * weight;
                    weights += weight;
                }
            }
            const int sample = static_cast<int>(std::lround(weights == 0.0 ? 0.0 : sum / weights));
            result[planeIndex(x, y, targetWidth)] =
                static_cast<T>(std::clamp(sample, 0, maximum));
        }
    }
    return result;
}

template <typename T>
std::vector<T> rotatePlane(const std::vector<T>& source, int width, int height,
                           QuarterTurn direction) {
    std::vector<T> result(source.size());
    const int targetWidth = height;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int tx = direction == QuarterTurn::Clockwise ? height - 1 - y : y;
            const int ty = direction == QuarterTurn::Clockwise ? x : width - 1 - x;
            result[planeIndex(tx, ty, targetWidth)] = source[planeIndex(x, y, width)];
        }
    }
    return result;
}

QImage bicubicImage(const QImage& input, const QSize& size) {
    if (input.depth() > 32) {
        const QImage source = input.convertToFormat(QImage::Format_RGBA64);
        QImage result(size, QImage::Format_RGBA64);
        for (int y = 0; y < size.height(); ++y) {
            const double sourceY = (y + 0.5) * source.height() / size.height() - 0.5;
            const int baseY = static_cast<int>(std::floor(sourceY));
            auto* destination = reinterpret_cast<QRgba64*>(result.scanLine(y));
            for (int x = 0; x < size.width(); ++x) {
                const double sourceX = (x + 0.5) * source.width() / size.width() - 0.5;
                const int baseX = static_cast<int>(std::floor(sourceX));
                std::array<double, 4> sum{};
                double weights = 0.0;
                for (int ky = -1; ky <= 2; ++ky) {
                    const int sy = std::clamp(baseY + ky, 0, source.height() - 1);
                    const auto* row =
                        reinterpret_cast<const QRgba64*>(source.constScanLine(sy));
                    const double wy = cubicWeight(sourceY - (baseY + ky));
                    for (int kx = -1; kx <= 2; ++kx) {
                        const int sx = std::clamp(baseX + kx, 0, source.width() - 1);
                        const double weight = wy * cubicWeight(sourceX - (baseX + kx));
                        const QRgba64 sample = row[sx];
                        sum[0] += sample.red() * weight;
                        sum[1] += sample.green() * weight;
                        sum[2] += sample.blue() * weight;
                        sum[3] += sample.alpha() * weight;
                        weights += weight;
                    }
                }
                auto channel = [&](std::size_t index) {
                    return static_cast<quint16>(std::clamp(
                        static_cast<int>(std::lround(sum[index] / weights)), 0, 65535));
                };
                destination[x] =
                    QRgba64::fromRgba64(channel(0), channel(1), channel(2), channel(3));
            }
        }
        result.setColorSpace(source.colorSpace());
        return result;
    }
    const QImage source = input.convertToFormat(QImage::Format_RGBA8888);
    QImage result(size, QImage::Format_RGBA8888);
    for (int y = 0; y < size.height(); ++y) {
        const double sourceY = (y + 0.5) * source.height() / size.height() - 0.5;
        const int baseY = static_cast<int>(std::floor(sourceY));
        auto* destination = result.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            const double sourceX = (x + 0.5) * source.width() / size.width() - 0.5;
            const int baseX = static_cast<int>(std::floor(sourceX));
            std::array<double, 4> sum{};
            double weights = 0.0;
            for (int ky = -1; ky <= 2; ++ky) {
                const int sy = std::clamp(baseY + ky, 0, source.height() - 1);
                const uchar* row = source.constScanLine(sy);
                const double wy = cubicWeight(sourceY - (baseY + ky));
                for (int kx = -1; kx <= 2; ++kx) {
                    const int sx = std::clamp(baseX + kx, 0, source.width() - 1);
                    const double weight = wy * cubicWeight(sourceX - (baseX + kx));
                    for (int channel = 0; channel < 4; ++channel)
                        sum[static_cast<std::size_t>(channel)] +=
                            row[sx * 4 + channel] * weight;
                    weights += weight;
                }
            }
            for (int channel = 0; channel < 4; ++channel)
                destination[x * 4 + channel] = static_cast<uchar>(
                    std::clamp(static_cast<int>(std::lround(
                                   sum[static_cast<std::size_t>(channel)] / weights)),
                               0, 255));
        }
    }
    result.setColorSpace(source.colorSpace());
    return result;
}

QString writeEncoded(const QString& path, const QImage& image) {
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return output.errorString();
    QImageWriter writer(&output, QFileInfo(path).suffix().toLatin1());
    writer.setQuality(95);
    if (!writer.write(image)) {
        output.cancelWriting();
        return writer.errorString();
    }
    if (!output.commit()) return output.errorString();
    return {};
}

quint16 read16(const char* bytes, bool littleEndian) {
    const auto* value = reinterpret_cast<const uchar*>(bytes);
    return littleEndian ? static_cast<quint16>(value[0] | (value[1] << 8))
                        : static_cast<quint16>((value[0] << 8) | value[1]);
}

void write16(char* bytes, quint16 value, bool littleEndian) {
    if (littleEndian) {
        bytes[0] = static_cast<char>(value & 0xff);
        bytes[1] = static_cast<char>(value >> 8);
    } else {
        bytes[0] = static_cast<char>(value >> 8);
        bytes[1] = static_cast<char>(value & 0xff);
    }
}

struct RawPlanes {
    std::vector<quint16> first;
    std::vector<quint16> second;
    std::vector<quint16> third;
};

int bayerChannel(BayerPattern pattern, int x, int y) {
    static constexpr int layouts[4][4] = {
        {0, 1, 1, 2}, {1, 0, 2, 1}, {1, 2, 0, 1}, {2, 1, 1, 0}};
    return layouts[static_cast<int>(pattern)][(y & 1) * 2 + (x & 1)];
}

BayerPattern rotatedBayerPattern(BayerPattern source, int width, int height,
                                 QuarterTurn direction) {
    std::array<int, 4> target{};
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const int sx = direction == QuarterTurn::Clockwise ? y : width - 1 - y;
            const int sy = direction == QuarterTurn::Clockwise ? height - 1 - x : x;
            target[static_cast<std::size_t>(y * 2 + x)] = bayerChannel(source, sx, sy);
        }
    }
    static constexpr std::array<std::array<int, 4>, 4> layouts{{
        {0, 1, 1, 2}, {1, 0, 2, 1}, {1, 2, 0, 1}, {2, 1, 1, 0}}};
    for (int index = 0; index < static_cast<int>(layouts.size()); ++index)
        if (target == layouts[static_cast<std::size_t>(index)])
            return static_cast<BayerPattern>(index);
    return source;
}

quint16 unpackBayer(const char* row, int x, const RawImageParameters& parameters) {
    switch (parameters.format) {
    case RawPixelFormat::MipiRaw10: {
        const auto* group = reinterpret_cast<const uchar*>(row + (x / 4) * 5);
        const int lane = x % 4;
        return static_cast<quint16>((group[lane] << 2) | ((group[4] >> (lane * 2)) & 3));
    }
    case RawPixelFormat::MipiRaw12: {
        const auto* group = reinterpret_cast<const uchar*>(row + (x / 2) * 3);
        const int lane = x % 2;
        return static_cast<quint16>((group[lane] << 4) | ((group[2] >> (lane * 4)) & 15));
    }
    case RawPixelFormat::Raw16: {
        quint16 value = read16(row + x * 2, parameters.littleEndian);
        if (parameters.validBits() < 16)
            value = parameters.msbAligned ? value >> (16 - parameters.validBits())
                                          : value & ((1 << parameters.validBits()) - 1);
        return value;
    }
    default: return 0;
    }
}

void packBayer(char* row, int x, quint16 sample, const RawImageParameters& parameters) {
    switch (parameters.format) {
    case RawPixelFormat::MipiRaw10: {
        auto* group = reinterpret_cast<uchar*>(row + (x / 4) * 5);
        const int lane = x % 4;
        group[lane] = static_cast<uchar>(sample >> 2);
        group[4] = static_cast<uchar>((group[4] & ~(3 << (lane * 2))) |
                                     ((sample & 3) << (lane * 2)));
        break;
    }
    case RawPixelFormat::MipiRaw12: {
        auto* group = reinterpret_cast<uchar*>(row + (x / 2) * 3);
        const int lane = x % 2;
        group[lane] = static_cast<uchar>(sample >> 4);
        group[2] = static_cast<uchar>((group[2] & ~(15 << (lane * 4))) |
                                     ((sample & 15) << (lane * 4)));
        break;
    }
    case RawPixelFormat::Raw16: {
        const quint16 stored =
            parameters.msbAligned && parameters.validBits() < 16
                ? static_cast<quint16>(sample << (16 - parameters.validBits()))
                : sample;
        write16(row + x * 2, stored, parameters.littleEndian);
        break;
    }
    default: break;
    }
}

RawPlanes unpackFrame(const QByteArray& frame, const RawImageParameters& parameters) {
    RawPlanes planes;
    const int width = parameters.size.width();
    const int height = parameters.size.height();
    const qsizetype yStride =
        parameters.rowStride > 0 ? parameters.rowStride : minimumRowStride(parameters);
    planes.first.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    if (!parameters.isYuv()) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                planes.first[planeIndex(x, y, width)] =
                    unpackBayer(frame.constData() + y * yStride, x, parameters);
        return planes;
    }
    const int bytes = parameters.format == RawPixelFormat::P010 ? 2 : 1;
    auto sample = [&](qsizetype offset) {
        quint16 value = bytes == 1 ? static_cast<uchar>(frame.at(offset))
                                   : read16(frame.constData() + offset, parameters.littleEndian);
        return static_cast<quint16>(bytes == 2 && parameters.msbAligned ? value >> 6 : value);
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            planes.first[planeIndex(x, y, width)] =
                sample(y * yStride + x * bytes);
    const int cw = (width + 1) / 2;
    const int ch = (height + 1) / 2;
    const qsizetype cStride = parameters.chromaStride > 0
                                  ? parameters.chromaStride
                                  : minimumChromaRowStride(parameters);
    const qsizetype yBytes = yStride * height;
    planes.second.resize(static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch));
    planes.third.resize(static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch));
    for (int y = 0; y < ch; ++y) {
        for (int x = 0; x < cw; ++x) {
            if (parameters.format == RawPixelFormat::I420) {
                planes.second[planeIndex(x, y, cw)] =
                    sample(yBytes + y * cStride + x);
                planes.third[planeIndex(x, y, cw)] =
                    sample(yBytes + cStride * ch + y * cStride + x);
            } else {
                const qsizetype offset = yBytes + y * cStride + x * bytes * 2;
                const quint16 a = sample(offset);
                const quint16 b = sample(offset + bytes);
                const bool vu = parameters.format == RawPixelFormat::NV21;
                planes.second[planeIndex(x, y, cw)] = vu ? b : a;
                planes.third[planeIndex(x, y, cw)] = vu ? a : b;
            }
        }
    }
    return planes;
}

QByteArray packFrame(const RawPlanes& planes, const RawImageParameters& parameters) {
    QByteArray frame(frameByteSize(parameters), '\0');
    const int width = parameters.size.width();
    const int height = parameters.size.height();
    const qsizetype yStride = minimumRowStride(parameters);
    if (!parameters.isYuv()) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                packBayer(frame.data() + y * yStride, x,
                          planes.first[planeIndex(x, y, width)], parameters);
        return frame;
    }
    const int bytes = parameters.format == RawPixelFormat::P010 ? 2 : 1;
    auto store = [&](qsizetype offset, quint16 sample) {
        if (bytes == 1) frame[offset] = static_cast<char>(sample);
        else write16(frame.data() + offset,
                     parameters.msbAligned ? static_cast<quint16>(sample << 6) : sample,
                     parameters.littleEndian);
    };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            store(y * yStride + x * bytes,
                  planes.first[planeIndex(x, y, width)]);
    const int cw = (width + 1) / 2;
    const int ch = (height + 1) / 2;
    const qsizetype cStride = minimumChromaRowStride(parameters);
    const qsizetype yBytes = yStride * height;
    for (int y = 0; y < ch; ++y) {
        for (int x = 0; x < cw; ++x) {
            const quint16 u = planes.second[planeIndex(x, y, cw)];
            const quint16 v = planes.third[planeIndex(x, y, cw)];
            if (parameters.format == RawPixelFormat::I420) {
                store(yBytes + y * cStride + x, u);
                store(yBytes + cStride * ch + y * cStride + x, v);
            } else {
                const qsizetype offset = yBytes + y * cStride + x * bytes * 2;
                const bool vu = parameters.format == RawPixelFormat::NV21;
                store(offset, vu ? v : u);
                store(offset + bytes, vu ? u : v);
            }
        }
    }
    return frame;
}

QString transformRaw(const QString& path, RawImageParameters sourceParameters,
                     const QSize& targetSize, std::optional<QuarterTurn> turn) {
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) return input.errorString();
    const QByteArray bytes = input.readAll();
    const qsizetype sourceFrameSize = frameByteSize(sourceParameters);
    if (sourceFrameSize <= 0 || sourceParameters.headerOffset > bytes.size())
        return QStringLiteral("Invalid RAW/YUV layout.");
    const qsizetype frameCount =
        (bytes.size() - sourceParameters.headerOffset) / sourceFrameSize;
    if (frameCount <= 0) return QStringLiteral("RAW/YUV file does not contain a complete frame.");

    RawImageParameters targetParameters = sourceParameters;
    targetParameters.size = targetSize;
    targetParameters.rowStride = 0;
    targetParameters.chromaStride = 0;
    if (turn && !sourceParameters.isYuv())
        targetParameters.bayerPattern =
            rotatedBayerPattern(sourceParameters.bayerPattern, sourceParameters.size.width(),
                                sourceParameters.size.height(), *turn);
    const int maximum = sourceParameters.maximumSampleValue();
    QByteArray output = bytes.left(sourceParameters.headerOffset);
    for (qsizetype frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const QByteArray frame =
            bytes.mid(sourceParameters.headerOffset + frameIndex * sourceFrameSize, sourceFrameSize);
        RawPlanes planes = unpackFrame(frame, sourceParameters);
        const int sw = sourceParameters.size.width();
        const int sh = sourceParameters.size.height();
        const int tw = targetSize.width();
        const int th = targetSize.height();
        if (turn) {
            planes.first = rotatePlane(planes.first, sw, sh, *turn);
            if (sourceParameters.isYuv()) {
                planes.second = rotatePlane(planes.second, (sw + 1) / 2, (sh + 1) / 2, *turn);
                planes.third = rotatePlane(planes.third, (sw + 1) / 2, (sh + 1) / 2, *turn);
            }
        } else {
            planes.first = resizePlane(planes.first, sw, sh, tw, th, maximum);
            if (sourceParameters.isYuv()) {
                planes.second = resizePlane(planes.second, (sw + 1) / 2, (sh + 1) / 2,
                                            (tw + 1) / 2, (th + 1) / 2, maximum);
                planes.third = resizePlane(planes.third, (sw + 1) / 2, (sh + 1) / 2,
                                           (tw + 1) / 2, (th + 1) / 2, maximum);
            }
        }
        output += packFrame(planes, targetParameters);
    }
    const qsizetype consumed = sourceParameters.headerOffset + frameCount * sourceFrameSize;
    output += bytes.mid(consumed);
    QSaveFile saved(path);
    if (!saved.open(QIODevice::WriteOnly)) return saved.errorString();
    if (saved.write(output) != output.size() || !saved.commit()) return saved.errorString();
    RawPresetStore::saveForFile(path, targetParameters);
    QString sidecarError;
    if (!RawPresetStore::saveSidecar(path, targetParameters, &sidecarError)) return sidecarError;
    return {};
}

QString loadEncoded(const QString& path, QImage& image) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    image = reader.read();
    return image.isNull() ? reader.errorString() : QString{};
}

} // namespace

QString ImageTransformer::backupPath(const QString& path) {
    return path + QStringLiteral(".ispview-original");
}

bool ImageTransformer::canRestore(const QString& path) {
    return QFileInfo::exists(backupPath(path)) && QFileInfo::exists(backupManifestPath(path));
}

QString ImageTransformer::backupManifestPath(const QString& path) {
    return path + QStringLiteral(".ispview-original.json");
}

QString ImageTransformer::ensureBackup(const QString& path) {
    if (canRestore(path)) return {};
    if (!QFileInfo::exists(path)) return QStringLiteral("The selected image no longer exists.");
    if (!QFile::copy(path, backupPath(path))) return QStringLiteral("Unable to create original backup.");
    QJsonObject manifest;
    const QString rawSidecar = RawPresetStore::sidecarPath(path);
    if (QFile sidecar(rawSidecar); sidecar.open(QIODevice::ReadOnly))
        manifest.insert(QStringLiteral("rawSidecar"),
                        QString::fromLatin1(sidecar.readAll().toBase64()));
    manifest.insert(QStringLiteral("hadRawSidecar"), QFileInfo::exists(rawSidecar));
    QSaveFile saved(backupManifestPath(path));
    if (!saved.open(QIODevice::WriteOnly) ||
        saved.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact)) < 0 ||
        !saved.commit()) {
        QFile::remove(backupPath(path));
        return QStringLiteral("Unable to record restore information.");
    }
    return {};
}

QString ImageTransformer::rotate(const QString& path, QuarterTurn direction,
                                 const std::optional<RawImageParameters>& raw) {
    const bool createdBackup = !canRestore(path);
    if (const QString error = ensureBackup(path); !error.isEmpty()) return error;
    if (raw && !QFileInfo::exists(RawPresetStore::sidecarPath(backupPath(path)))) {
        QString backupError;
        if (!RawPresetStore::saveSidecar(backupPath(path), *raw, &backupError)) {
            if (createdBackup) {
                QFile::remove(backupPath(path));
                QFile::remove(backupManifestPath(path));
            }
            return backupError;
        }
    }
    QString error;
    if (raw) {
        error = transformRaw(path, *raw, {raw->size.height(), raw->size.width()}, direction);
    } else {
        QImage image;
        error = loadEncoded(path, image);
        if (error.isEmpty()) {
            QTransform transform;
            transform.rotate(direction == QuarterTurn::Clockwise ? 90 : -90);
            error = writeEncoded(path, image.transformed(transform));
        }
    }
    if (!error.isEmpty() && createdBackup) {
        QFile::remove(backupPath(path));
        QFile::remove(backupManifestPath(path));
        QFile::remove(RawPresetStore::sidecarPath(backupPath(path)));
    }
    return error;
}

QString ImageTransformer::resize(const QString& path, const QSize& size,
                                 const std::optional<RawImageParameters>& raw) {
    if (!size.isValid()) return QStringLiteral("Width and height must be greater than zero.");
    if (static_cast<qint64>(size.width()) * size.height() > 250'000'000)
        return QStringLiteral("The requested image exceeds the 250-megapixel safety limit.");
    const bool createdBackup = !canRestore(path);
    if (const QString error = ensureBackup(path); !error.isEmpty()) return error;
    if (raw && !QFileInfo::exists(RawPresetStore::sidecarPath(backupPath(path)))) {
        QString backupError;
        if (!RawPresetStore::saveSidecar(backupPath(path), *raw, &backupError)) {
            if (createdBackup) {
                QFile::remove(backupPath(path));
                QFile::remove(backupManifestPath(path));
            }
            return backupError;
        }
    }
    if (raw) {
        const QString error = transformRaw(path, *raw, size, {});
        if (!error.isEmpty() && createdBackup) {
            QFile::remove(backupPath(path));
            QFile::remove(backupManifestPath(path));
            QFile::remove(RawPresetStore::sidecarPath(backupPath(path)));
        }
        return error;
    }
    QImage image;
    QString error = loadEncoded(path, image);
    if (error.isEmpty()) error = writeEncoded(path, bicubicImage(image, size));
    if (!error.isEmpty() && createdBackup) {
        QFile::remove(backupPath(path));
        QFile::remove(backupManifestPath(path));
        QFile::remove(RawPresetStore::sidecarPath(backupPath(path)));
    }
    return error;
}

QString ImageTransformer::restore(const QString& path) {
    if (!canRestore(path)) return QStringLiteral("No original image backup is available.");
    QFile source(backupPath(path));
    if (!source.open(QIODevice::ReadOnly)) return source.errorString();
    QSaveFile destination(path);
    if (!destination.open(QIODevice::WriteOnly)) return destination.errorString();
    const QByteArray original = source.readAll();
    if (destination.write(original) != original.size() || !destination.commit())
        return destination.errorString();

    const QString backupRawSidecar = RawPresetStore::sidecarPath(backupPath(path));
    if (QFileInfo::exists(backupRawSidecar)) {
        if (const auto originalRaw = RawPresetStore::loadForFile(backupPath(path))) {
            RawPresetStore::saveForFile(path, *originalRaw);
            QString ignoredError;
            (void)RawPresetStore::saveSidecar(path, *originalRaw, &ignoredError);
        }
    } else if (QFile manifest(backupManifestPath(path)); manifest.open(QIODevice::ReadOnly)) {
        const QJsonObject values = QJsonDocument::fromJson(manifest.readAll()).object();
        const QString sidecarPath = RawPresetStore::sidecarPath(path);
        if (values.value(QStringLiteral("hadRawSidecar")).toBool()) {
            QSaveFile sidecar(sidecarPath);
            const QByteArray data =
                QByteArray::fromBase64(values.value(QStringLiteral("rawSidecar")).toString().toLatin1());
            if (sidecar.open(QIODevice::WriteOnly)) {
                sidecar.write(data);
                sidecar.commit();
            }
        } else {
            QFile::remove(sidecarPath);
        }
    }
    QFile::remove(backupPath(path));
    QFile::remove(backupManifestPath(path));
    QFile::remove(backupRawSidecar);
    return {};
}

} // namespace ispview
