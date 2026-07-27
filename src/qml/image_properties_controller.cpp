#include "qml/image_properties_controller.h"

#include "core/display_histogram.h"
#include "core/raw_plane_access.h"
#include "core/raw_plane_histogram.h"
#include "io/image_loader.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <cmath>

namespace ispview {
namespace {

QVariantMap field(const QString& label, const QString& value) {
    return {{QStringLiteral("label"), label}, {QStringLiteral("value"), value}};
}

void appendField(QVariantList& fields, const QString& label, const QString& value,
                 bool keepEmpty = false) {
    if (keepEmpty || !value.isEmpty()) fields.append(field(label, value));
}

QString byteSizeText(qint64 bytes) {
    return QLocale().formattedDataSize(std::max<qint64>(0, bytes), 2,
                                      QLocale::DataSizeTraditionalFormat);
}

QString exposureText(double seconds) {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) return {};
    if (seconds >= 1.0) return QStringLiteral("%1 s").arg(seconds, 0, 'g', 4);
    const double denominator = 1.0 / seconds;
    const double rounded = std::round(denominator);
    if (std::abs(denominator - rounded) < 0.05 && rounded <= 100000.0)
        return QStringLiteral("1/%1 s").arg(static_cast<int>(rounded));
    return QStringLiteral("%1 s").arg(seconds, 0, 'g', 4);
}

QString metadataOrientationText(ImageMetadata::Orientation orientation) {
    switch (orientation) {
    case ImageMetadata::Orientation::Normal: return QStringLiteral("Normal");
    case ImageMetadata::Orientation::MirrorHorizontal: return QStringLiteral("Mirror horizontal");
    case ImageMetadata::Orientation::Rotate180: return QStringLiteral("Rotate 180°");
    case ImageMetadata::Orientation::MirrorVertical: return QStringLiteral("Mirror vertical");
    case ImageMetadata::Orientation::MirrorHorizontalRotate270:
        return QStringLiteral("Mirror horizontal, rotate 270° CW");
    case ImageMetadata::Orientation::Rotate90: return QStringLiteral("Rotate 90° CW");
    case ImageMetadata::Orientation::MirrorHorizontalRotate90:
        return QStringLiteral("Mirror horizontal, rotate 90° CW");
    case ImageMetadata::Orientation::Rotate270: return QStringLiteral("Rotate 270° CW");
    case ImageMetadata::Orientation::Unspecified: return {};
    }
    return {};
}

QString rawOrientationText(ImageOrientation orientation) {
    switch (orientation) {
    case ImageOrientation::Normal: return QStringLiteral("Normal");
    case ImageOrientation::Rotate90Clockwise: return QStringLiteral("Rotate 90° clockwise");
    case ImageOrientation::Rotate180: return QStringLiteral("Rotate 180°");
    case ImageOrientation::Rotate270Clockwise: return QStringLiteral("Rotate 270° clockwise");
    }
    return {};
}

QString matrixText(const std::array<double, 9>& matrix) {
    QStringList rows;
    for (int row = 0; row < 3; ++row) {
        QStringList values;
        for (int column = 0; column < 3; ++column)
            values.append(QString::number(matrix.at(static_cast<std::size_t>(row * 3 + column)),
                                          'g', 6));
        rows.append(values.join(QStringLiteral(", ")));
    }
    return rows.join(QStringLiteral("  |  "));
}

QVariantList rawFieldRows(const RawImageParameters& parameters) {
    QVariantList result;
    appendField(result, QStringLiteral("Format"), rawPixelFormatName(parameters.format));
    appendField(result, QStringLiteral("Dimensions"),
                QStringLiteral("%1 × %2").arg(parameters.size.width()).arg(parameters.size.height()));
    appendField(result, QStringLiteral("Header Offset"), QString::number(parameters.headerOffset));
    appendField(result, QStringLiteral("Row Stride"), QString::number(parameters.rowStride));
    appendField(result, QStringLiteral("Orientation"), rawOrientationText(parameters.orientation));
    if (parameters.isYuv()) {
        appendField(result, QStringLiteral("Chroma Stride"), QString::number(parameters.chromaStride));
        appendField(result, QStringLiteral("YUV Matrix"), yuvMatrixName(parameters.yuvMatrix));
        appendField(result, QStringLiteral("YUV Range"),
                    parameters.range == QuantizationRange::Full ? QStringLiteral("Full")
                                                                : QStringLiteral("Limited"));
    } else {
        appendField(result, QStringLiteral("Valid Bits"), QString::number(parameters.validBits()));
        appendField(result, QStringLiteral("Bayer Pattern"), bayerPatternName(parameters.bayerPattern));
        appendField(result, QStringLiteral("Demosaic"),
                    parameters.demosaic ? QStringLiteral("Yes") : QStringLiteral("No"));
        appendField(result, QStringLiteral("Black Level"), QString::number(parameters.blackLevel));
        appendField(result, QStringLiteral("White Level"), QString::number(parameters.whiteLevel));
        appendField(result, QStringLiteral("White Balance"),
                    QStringLiteral("R %1  G %2  B %3")
                        .arg(parameters.whiteBalanceGains.at(0), 0, 'g', 6)
                        .arg(parameters.whiteBalanceGains.at(1), 0, 'g', 6)
                        .arg(parameters.whiteBalanceGains.at(2), 0, 'g', 6));
        appendField(result, QStringLiteral("Color Matrix"), matrixText(parameters.colorCorrectionMatrix));
        appendField(result, QStringLiteral("Display Gamma"),
                    QString::number(parameters.displayGamma, 'g', 6));
    }
    if (parameters.format == RawPixelFormat::P010 || parameters.format == RawPixelFormat::Raw16) {
        appendField(result, QStringLiteral("Byte Order"),
                    parameters.littleEndian ? QStringLiteral("Little endian")
                                            : QStringLiteral("Big endian"));
        appendField(result, QStringLiteral("Bit Alignment"),
                    parameters.msbAligned ? QStringLiteral("MSB aligned")
                                          : QStringLiteral("LSB aligned"));
    }
    return result;
}

template <typename Bins>
double medianFor(const Bins& bins, quint64 samples) {
    if (samples == 0) return 0.0;
    const quint64 firstTarget = (samples - 1) / 2;
    const quint64 secondTarget = samples / 2;
    quint64 cumulative = 0;
    int first = -1;
    int second = -1;
    for (qsizetype index = 0; index < static_cast<qsizetype>(bins.size()); ++index) {
        cumulative += bins.at(static_cast<typename Bins::size_type>(index));
        if (first < 0 && cumulative > firstTarget) first = static_cast<int>(index);
        if (cumulative > secondTarget) {
            second = static_cast<int>(index);
            break;
        }
    }
    return (std::max(0, first) + std::max(0, second)) / 2.0;
}

QVariantList binsFor(const auto& bins) {
    QVariantList values;
    values.reserve(static_cast<qsizetype>(bins.size()));
    for (const auto value : bins) values.append(QVariant::fromValue<qulonglong>(value));
    return values;
}

QVariantMap channelMap(const QString& id, const QString& name, const QString& color,
                       const auto& bins, quint64 samples, double mean, double deviation,
                       int minimum, int maximum) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
            {QStringLiteral("color"), color}, {QStringLiteral("bins"), binsFor(bins)},
            {QStringLiteral("mean"), mean},
            {QStringLiteral("variance"), deviation * deviation},
            {QStringLiteral("min"), minimum}, {QStringLiteral("max"), maximum},
            {QStringLiteral("median"), medianFor(bins, samples)}};
}

QVariantMap displayHistogramMap(const DisplayHistogram& histogram) {
    if (!histogram.isValid())
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("message"), QStringLiteral("Display histogram unavailable")}};
    const quint64 samples = static_cast<quint64>(std::max<qint64>(0, histogram.sampledPixelCount));
    QVariantList channels;
    channels << channelMap(QStringLiteral("luma"), QStringLiteral("Luma"), QStringLiteral("#52616B"),
                           histogram.luma.bins, samples, histogram.luma.mean,
                           histogram.luma.standardDeviation, histogram.luma.minimum,
                           histogram.luma.maximum)
             << channelMap(QStringLiteral("red"), QStringLiteral("Red"), QStringLiteral("#D65A5A"),
                           histogram.red.bins, samples, histogram.red.mean,
                           histogram.red.standardDeviation, histogram.red.minimum,
                           histogram.red.maximum)
             << channelMap(QStringLiteral("green"), QStringLiteral("Green"), QStringLiteral("#4E9D69"),
                           histogram.green.bins, samples, histogram.green.mean,
                           histogram.green.standardDeviation, histogram.green.minimum,
                           histogram.green.maximum)
             << channelMap(QStringLiteral("blue"), QStringLiteral("Blue"), QStringLiteral("#527BC9"),
                           histogram.blue.bins, samples, histogram.blue.mean,
                           histogram.blue.standardDeviation, histogram.blue.minimum,
                           histogram.blue.maximum);
    QString summary = QStringLiteral("%1 × %2 analysis · %3 samples")
                          .arg(histogram.analyzedSize.width()).arg(histogram.analyzedSize.height())
                          .arg(QLocale().toString(histogram.sampledPixelCount));
    if (histogram.usesDisplayProxy())
        summary += QStringLiteral(" · source %1 × %2")
                       .arg(histogram.logicalSize.width()).arg(histogram.logicalSize.height());
    return {{QStringLiteral("valid"), true}, {QStringLiteral("loading"), false},
            {QStringLiteral("maximumValue"), 255}, {QStringLiteral("channels"), channels},
            {QStringLiteral("summary"), summary}};
}

QString rawChannelColor(RawHistogramChannelId id) {
    switch (id) {
    case RawHistogramChannelId::Y: return QStringLiteral("#52616B");
    case RawHistogramChannelId::U: return QStringLiteral("#4F9DA8");
    case RawHistogramChannelId::V: return QStringLiteral("#A6629B");
    case RawHistogramChannelId::Red: return QStringLiteral("#D65A5A");
    case RawHistogramChannelId::GreenRedRow: return QStringLiteral("#4E9D69");
    case RawHistogramChannelId::GreenBlueRow: return QStringLiteral("#347C50");
    case RawHistogramChannelId::Blue: return QStringLiteral("#527BC9");
    }
    return QStringLiteral("#52616B");
}

QVariantMap rawHistogramMap(const RawPlaneHistogram& histogram) {
    if (!histogram.isValid())
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("message"), QStringLiteral("Source-plane histogram unavailable")}};
    QVariantList channels;
    for (const RawHistogramChannel& channel : histogram.channels) {
        if (!channel.isValid()) continue;
        const QString name = rawHistogramChannelName(channel.id);
        channels.append(channelMap(QString::number(static_cast<int>(channel.id)), name,
                                   rawChannelColor(channel.id), channel.bins,
                                   static_cast<quint64>(std::max<qint64>(0, channel.sampledSampleCount)),
                                   channel.mean, channel.standardDeviation, channel.minimum,
                                   channel.maximum));
    }
    const QString summary = QStringLiteral("Source %1 · %2-bit · %3 × %4")
                                .arg(histogram.domain == RawHistogramDomain::Yuv
                                         ? QStringLiteral("YUV") : QStringLiteral("Bayer"))
                                .arg(histogram.validBits).arg(histogram.logicalSize.width())
                                .arg(histogram.logicalSize.height());
    return {{QStringLiteral("valid"), true}, {QStringLiteral("loading"), false},
            {QStringLiteral("maximumValue"), histogram.maximumValue},
            {QStringLiteral("channels"), channels}, {QStringLiteral("summary"), summary}};
}

} // namespace

ImagePropertiesController::ImagePropertiesController(ImageLoader* loader, QObject* parent)
    : QObject(parent), loader_(loader) {
    Q_ASSERT(loader_);
}

void ImagePropertiesController::loadPath(const QString& requestedPath) {
    loadHandle_.cancel();
    const QFileInfo info(requestedPath);
    path_ = info.absoluteFilePath();
    fileName_ = info.fileName().isEmpty() ? QDir::toNativeSeparators(path_) : info.fileName();
    directory_ = info.isDir();
    loading_ = false;
    errorText_.clear();
    basicFields_.clear();
    exifFields_.clear();
    rawFields_.clear();
    frame_.reset();
    pendingHistogramSources_.clear();
    ++loadGeneration_;
    resetHistograms();

    if (directory_) {
        appendField(basicFields_, QStringLiteral("Type"), QStringLiteral("Folder"));
        appendField(basicFields_, QStringLiteral("Location"),
                    QDir::toNativeSeparators(info.absolutePath()));
        appendField(basicFields_, QStringLiteral("Modified"),
                    QLocale().toString(info.lastModified(), QLocale::LongFormat));
        emit stateChanged();
        return;
    }

    if (const auto parameters = loader_->rawParameters(path_))
        rawFields_ = rawFieldRows(*parameters);
    loading_ = true;
    emit stateChanged();

    const quint64 generation = loadGeneration_;
    const QPointer<ImagePropertiesController> self(this);
    loadHandle_ = loader_->request(generation, {path_, DecodePurpose::Full, {}},
                     [self, generation](quint64 id, const DecodeResult& result) {
        if (!self || id != generation || self->loadGeneration_ != generation) return;
        self->loading_ = false;
        if (!result.frame) {
            self->errorText_ = result.error;
            emit self->stateChanged();
            return;
        }
        self->setFrame(result.frame);
    }, RequestOptions{LoadCategory::Metadata, 10, QStringLiteral("properties")});
}

void ImagePropertiesController::setFrame(ImageFramePtr frame) {
    frame_ = std::move(frame);
    basicFields_.clear();
    exifFields_.clear();
    const ImageMetadata& metadata = frame_->metadata;
    const QSize dimensions = metadata.sourceSize.isValid() ? metadata.sourceSize
                                                            : frame_->descriptor.size;
    appendField(basicFields_, QStringLiteral("File Name"), metadata.fileName);
    appendField(basicFields_, QStringLiteral("Location"), metadata.path);
    appendField(basicFields_, QStringLiteral("Type"), metadata.format);
    appendField(basicFields_, QStringLiteral("File Size"), byteSizeText(metadata.fileSize));
    appendField(basicFields_, QStringLiteral("Date / Time"), metadata.modifiedAt.toString(Qt::ISODate));
    appendField(basicFields_, QStringLiteral("Dimensions"),
                QStringLiteral("%1 × %2").arg(dimensions.width()).arg(dimensions.height()));
    appendField(basicFields_, QStringLiteral("Bit Depth"),
                QStringLiteral("%1-bit valid in %2-bit storage")
                    .arg(frame_->descriptor.validBits).arg(frame_->descriptor.storageBits));

    constexpr bool keepEmpty = true;
    if (metadata.camera) {
        const ImageMetadata::Camera& camera = *metadata.camera;
        appendField(exifFields_, QStringLiteral("Make"), camera.make, keepEmpty);
        appendField(exifFields_, QStringLiteral("Model"), camera.model, keepEmpty);
        appendField(exifFields_, QStringLiteral("Software"), camera.software, keepEmpty);
        appendField(exifFields_, QStringLiteral("Captured At"),
                    camera.capturedAt.toString(Qt::ISODate), keepEmpty);
        appendField(exifFields_, QStringLiteral("Exposure Time"),
                    exposureText(camera.exposureSeconds), keepEmpty);
        appendField(exifFields_, QStringLiteral("Aperture"), camera.aperture > 0.0
                        ? QStringLiteral("f/%1").arg(camera.aperture, 0, 'g', 3) : QString{}, keepEmpty);
        appendField(exifFields_, QStringLiteral("ISO"), camera.iso > 0
                        ? QString::number(camera.iso) : QString{}, keepEmpty);
        appendField(exifFields_, QStringLiteral("Exposure Program"), camera.exposureProgram, keepEmpty);
        appendField(exifFields_, QStringLiteral("Metering Mode"), camera.meteringMode, keepEmpty);
        appendField(exifFields_, QStringLiteral("Exposure Compensation"), camera.exposureCompensation, keepEmpty);
        appendField(exifFields_, QStringLiteral("Flash"), camera.flash, keepEmpty);
        appendField(exifFields_, QStringLiteral("Focal Length"), camera.focalLengthMm > 0.0
                        ? QStringLiteral("%1 mm").arg(camera.focalLengthMm, 0, 'g', 4) : QString{}, keepEmpty);
        appendField(exifFields_, QStringLiteral("Lens"), camera.lens, keepEmpty);
        appendField(exifFields_, QStringLiteral("GPS"), camera.gps, keepEmpty);
        appendField(exifFields_, QStringLiteral("Sensor Size"), camera.sensorSize.isValid()
                        ? QStringLiteral("%1 × %2").arg(camera.sensorSize.width()).arg(camera.sensorSize.height())
                        : QString{}, keepEmpty);
    } else {
        const QStringList names{QStringLiteral("Make"), QStringLiteral("Model"),
            QStringLiteral("Software"), QStringLiteral("Captured At"),
            QStringLiteral("Exposure Time"), QStringLiteral("Aperture"), QStringLiteral("ISO"),
            QStringLiteral("Exposure Program"), QStringLiteral("Metering Mode"),
            QStringLiteral("Exposure Compensation"), QStringLiteral("Flash"),
            QStringLiteral("Focal Length"), QStringLiteral("Lens"), QStringLiteral("GPS"),
            QStringLiteral("Sensor Size")};
        for (const QString& name : names) appendField(exifFields_, name, {}, keepEmpty);
    }
    appendField(exifFields_, QStringLiteral("Orientation"),
                metadataOrientationText(metadata.sourceOrientation), keepEmpty);
    if (metadata.descriptive) {
        appendField(exifFields_, QStringLiteral("Title"), metadata.descriptive->title);
        appendField(exifFields_, QStringLiteral("Description"), metadata.descriptive->description);
        appendField(exifFields_, QStringLiteral("Creator"), metadata.descriptive->creator);
        appendField(exifFields_, QStringLiteral("Copyright"), metadata.descriptive->copyright);
    }
    appendField(exifFields_, QStringLiteral("Metadata Warning"), metadata.metadataWarning, keepEmpty);
    if (frame_->rawParameters) rawFields_ = rawFieldRows(*frame_->rawParameters);
    errorText_.clear();
    emit stateChanged();

    pendingHistogramSources_.insert(0);
    const QSet<int> requested = pendingHistogramSources_;
    pendingHistogramSources_.clear();
    for (const int source : requested) requestHistogram(source);
}

void ImagePropertiesController::requestHistogram(int source) {
    source = source == 1 ? 1 : 0;
    if (!frame_) {
        pendingHistogramSources_.insert(source);
        return;
    }
    if (source == 1 && !RawPlaneAccessor(*frame_).isValid()) {
        sourceHistogram_ = {{QStringLiteral("valid"), false}, {QStringLiteral("loading"), false},
                            {QStringLiteral("message"),
                             QStringLiteral("Source planes are unavailable for this image")}};
        ++histogramRevision_;
        emit histogramChanged(source);
        emit histogramRevisionChanged();
        return;
    }
    quint64& generation = source == 0 ? displayHistogramGeneration_ : sourceHistogramGeneration_;
    ++generation;
    const quint64 requestedGeneration = generation;
    QVariantMap& destination = source == 0 ? displayHistogram_ : sourceHistogram_;
    destination = {{QStringLiteral("valid"), false}, {QStringLiteral("loading"), true},
                   {QStringLiteral("message"), QStringLiteral("Analyzing histogram…")}};
    ++histogramRevision_;
    emit histogramChanged(source);
    emit histogramRevisionChanged();

    const ImageFramePtr frame = frame_;
    const QPointer<ImagePropertiesController> self(this);
    QThreadPool::globalInstance()->start([self, frame, source, requestedGeneration] {
        const QVariantMap result = source == 0
            ? displayHistogramMap(DisplayHistogramAnalyzer::analyze(*frame))
            : rawHistogramMap(RawPlaneHistogramAnalyzer::analyze(*frame));
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, source, requestedGeneration, result] {
            if (self) self->setHistogram(source, requestedGeneration, result);
        }, Qt::QueuedConnection);
    }, -20);
}

QVariantMap ImagePropertiesController::histogram(int source) const {
    return source == 1 ? sourceHistogram_ : displayHistogram_;
}

void ImagePropertiesController::setHistogram(int source, quint64 generation, QVariantMap value) {
    const quint64 current = source == 1 ? sourceHistogramGeneration_ : displayHistogramGeneration_;
    if (generation != current) return;
    (source == 1 ? sourceHistogram_ : displayHistogram_) = std::move(value);
    ++histogramRevision_;
    emit histogramChanged(source);
    emit histogramRevisionChanged();
}

void ImagePropertiesController::resetHistograms() {
    ++displayHistogramGeneration_;
    ++sourceHistogramGeneration_;
    displayHistogram_.clear();
    sourceHistogram_.clear();
    ++histogramRevision_;
    emit histogramRevisionChanged();
}

} // namespace ispview
