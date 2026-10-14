#include "qml/image_properties_controller.h"

#include "core/display_histogram.h"
#include "core/raw_plane_access.h"
#include "core/raw_plane_histogram.h"
#include "io/image_loader.h"

#include <QDir>
#include <QCoreApplication>
#include <QImageReader>
#include <QFileInfo>
#include <QLocale>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <cmath>

namespace mvpview {
namespace {

QString text(const char* source) {
    return QCoreApplication::translate("ImageProperties", source);
}

QVariantMap field(const QString& id, const char* label, const QString& value, bool mono = false) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), text(label)},
            {QStringLiteral("value"), value}, {QStringLiteral("mono"), mono}};
}

void appendField(QVariantList& fields, const QString& id, const char* label,
                 const QString& value, bool mono = false) {
    if (!value.isEmpty()) fields.append(field(id, label, value, mono));
}

void appendGroup(QVariantList& groups, const QString& id, const char* title,
                 const QVariantList& fields) {
    if (!fields.isEmpty()) groups.append(QVariantMap{{QStringLiteral("id"), id},
        {QStringLiteral("title"), text(title)}, {QStringLiteral("fields"), fields}});
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
    case ImageMetadata::Orientation::Normal: return text(QT_TRANSLATE_NOOP("ImageProperties", "Normal"));
    case ImageMetadata::Orientation::MirrorHorizontal: return text(QT_TRANSLATE_NOOP("ImageProperties", "Mirror horizontal"));
    case ImageMetadata::Orientation::Rotate180: return text(QT_TRANSLATE_NOOP("ImageProperties", "Rotate 180°"));
    case ImageMetadata::Orientation::MirrorVertical: return text(QT_TRANSLATE_NOOP("ImageProperties", "Mirror vertical"));
    case ImageMetadata::Orientation::MirrorHorizontalRotate270:
        return text(QT_TRANSLATE_NOOP("ImageProperties", "Mirror horizontal, rotate 270° CW"));
    case ImageMetadata::Orientation::Rotate90: return text(QT_TRANSLATE_NOOP("ImageProperties", "Rotate 90° CW"));
    case ImageMetadata::Orientation::MirrorHorizontalRotate90:
        return text(QT_TRANSLATE_NOOP("ImageProperties", "Mirror horizontal, rotate 90° CW"));
    case ImageMetadata::Orientation::Rotate270: return text(QT_TRANSLATE_NOOP("ImageProperties", "Rotate 270° CW"));
    case ImageMetadata::Orientation::Unspecified: return {};
    }
    return {};
}

QString rawOrientationText(ImageOrientation orientation) {
    switch (orientation) {
    case ImageOrientation::Normal: return text(QT_TRANSLATE_NOOP("ImageProperties", "Normal"));
    case ImageOrientation::Rotate90Clockwise: return text(QT_TRANSLATE_NOOP("ImageProperties", "Rotate 90° clockwise"));
    case ImageOrientation::Rotate180: return text(QT_TRANSLATE_NOOP("ImageProperties", "Rotate 180°"));
    case ImageOrientation::Rotate270Clockwise: return text(QT_TRANSLATE_NOOP("ImageProperties", "Rotate 270° clockwise"));
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

QVariantList rawFieldRows(const RawImageParameters& p) {
    QVariantList result;
    auto add = [&](const char* id, const char* label, const QString& value, bool mono = true) {
        appendField(result, QLatin1String(id), label, value, mono);
    };
    add("raw-format", QT_TRANSLATE_NOOP("ImageProperties", "Format"), rawPixelFormatName(p.format), false);
    add("raw-dimensions", QT_TRANSLATE_NOOP("ImageProperties", "Dimensions"), QStringLiteral("%1 × %2 px").arg(p.size.width()).arg(p.size.height()));
    add("header-offset", QT_TRANSLATE_NOOP("ImageProperties", "Header offset (bytes)"), QString::number(p.headerOffset));
    add("row-stride", QT_TRANSLATE_NOOP("ImageProperties", "Row stride (bytes; 0 = auto)"), QString::number(p.rowStride));
    add("raw-orientation", QT_TRANSLATE_NOOP("ImageProperties", "Orientation"), rawOrientationText(p.orientation), false);
    if (p.isYuv()) {
        add("chroma-stride", QT_TRANSLATE_NOOP("ImageProperties", "Chroma stride (bytes; 0 = auto)"), QString::number(p.chromaStride));
        add("yuv-matrix", QT_TRANSLATE_NOOP("ImageProperties", "YUV matrix"), yuvMatrixName(p.yuvMatrix), false);
        add("yuv-primaries", QT_TRANSLATE_NOOP("ImageProperties", "Color primaries"), yuvPrimariesName(p.yuvPrimaries), false);
        add("yuv-transfer", QT_TRANSLATE_NOOP("ImageProperties", "Transfer function"), yuvTransferName(p.yuvTransfer), false);
        add("yuv-chroma", QT_TRANSLATE_NOOP("ImageProperties", "Chroma location"), chromaLocationName(p.chromaLocation), false);
        add("yuv-range", QT_TRANSLATE_NOOP("ImageProperties", "YUV range"), text(p.range == QuantizationRange::Full ? QT_TRANSLATE_NOOP("ImageProperties", "Full") : QT_TRANSLATE_NOOP("ImageProperties", "Limited")), false);
    } else {
        add("raw-bits", QT_TRANSLATE_NOOP("ImageProperties", "Valid bits"), QString::number(p.validBits()));
        add("bayer-pattern", QT_TRANSLATE_NOOP("ImageProperties", "Bayer pattern"), bayerPatternName(p.bayerPattern));
        add("cfa-sampling", QT_TRANSLATE_NOOP("ImageProperties", "CFA sampling"), bayerSamplingName(p.bayerSampling));
        add("demosaic", QT_TRANSLATE_NOOP("ImageProperties", "Demosaic"), text(p.demosaic ? QT_TRANSLATE_NOOP("ImageProperties", "Yes") : QT_TRANSLATE_NOOP("ImageProperties", "No")), false);
        add("black-level", QT_TRANSLATE_NOOP("ImageProperties", "Black level"), QString::number(p.blackLevel));
        add("white-level", QT_TRANSLATE_NOOP("ImageProperties", "White level (0 = maximum)"), QString::number(p.whiteLevel));
        add("white-balance", QT_TRANSLATE_NOOP("ImageProperties", "White balance gains"), QStringLiteral("R %1  G %2  B %3")
            .arg(p.whiteBalanceGains[0], 0, 'g', 6).arg(p.whiteBalanceGains[1], 0, 'g', 6)
            .arg(p.whiteBalanceGains[2], 0, 'g', 6));
        auto matrix = field(QStringLiteral("color-matrix"), QT_TRANSLATE_NOOP("ImageProperties", "Color correction matrix"), matrixText(p.colorCorrectionMatrix), true);
        QVariantList values;
        for (double value : p.colorCorrectionMatrix) values.append(QString::number(value, 'g', 6));
        matrix.insert(QStringLiteral("matrix"), values);
        result.append(matrix);
        add("display-gamma", QT_TRANSLATE_NOOP("ImageProperties", "Display gamma"), QString::number(p.displayGamma, 'g', 6));
    }
    if (p.format == RawPixelFormat::P010 || p.format == RawPixelFormat::Raw16) {
        add("byte-order", QT_TRANSLATE_NOOP("ImageProperties", "Byte order"), text(p.littleEndian ? QT_TRANSLATE_NOOP("ImageProperties", "Little endian") : QT_TRANSLATE_NOOP("ImageProperties", "Big endian")), false);
        add("bit-alignment", QT_TRANSLATE_NOOP("ImageProperties", "Bit alignment"), text(p.msbAligned ? QT_TRANSLATE_NOOP("ImageProperties", "MSB aligned") : QT_TRANSLATE_NOOP("ImageProperties", "LSB aligned")), false);
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
            {QStringLiteral("median"), medianFor(bins, samples)},
            {QStringLiteral("sampleCount"), QVariant::fromValue<qulonglong>(samples)}};
}

QVariantMap displayHistogramMap(const DisplayHistogram& histogram) {
    if (!histogram.isValid())
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("message"), QStringLiteral("Display histogram unavailable")}};
    const quint64 samples = static_cast<quint64>(std::max<qint64>(0, histogram.sampledPixelCount));
    QVariantList channels;
    channels << channelMap(QStringLiteral("luma"), QString::fromUtf8(QT_TRANSLATE_NOOP("ImageProperties", "Luma")), QStringLiteral("#52616B"),
                           histogram.luma.bins, samples, histogram.luma.mean,
                           histogram.luma.standardDeviation, histogram.luma.minimum,
                           histogram.luma.maximum)
             << channelMap(QStringLiteral("red"), QString::fromUtf8(QT_TRANSLATE_NOOP("ImageProperties", "Red")), QStringLiteral("#D65A5A"),
                           histogram.red.bins, samples, histogram.red.mean,
                           histogram.red.standardDeviation, histogram.red.minimum,
                           histogram.red.maximum)
             << channelMap(QStringLiteral("green"), QString::fromUtf8(QT_TRANSLATE_NOOP("ImageProperties", "Green")), QStringLiteral("#4E9D69"),
                           histogram.green.bins, samples, histogram.green.mean,
                           histogram.green.standardDeviation, histogram.green.minimum,
                           histogram.green.maximum)
             << channelMap(QStringLiteral("blue"), QString::fromUtf8(QT_TRANSLATE_NOOP("ImageProperties", "Blue")), QStringLiteral("#527BC9"),
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
            {QStringLiteral("maximumValue"), histogram.maximumValue},
            {QStringLiteral("channels"), channels},
            {QStringLiteral("summary"), summary},
            {QStringLiteral("analysisWidth"), histogram.analyzedSize.width()},
            {QStringLiteral("analysisHeight"), histogram.analyzedSize.height()},
            {QStringLiteral("sampleCount"), QVariant::fromValue<qlonglong>(histogram.sampledPixelCount)},
            {QStringLiteral("sourceWidth"), histogram.logicalSize.width()},
            {QStringLiteral("sourceHeight"), histogram.logicalSize.height()}};
}

QVariantMap nativeFileHistogramMap(const QString& path, const std::function<bool()>& cancelled) {
    // Analyze original encoded values rather than the display-converted QImage in the cache.
    // Read on the analysis worker and leave application-wide decoder/color settings untouched.
    if (cancelled()) return {};
    QImageReader reader(path);
    QImage image = reader.read();
    if (image.isNull() || cancelled())
        return {{QStringLiteral("valid"), false}, {QStringLiteral("loading"), false}};
    const int bits = image.format() == QImage::Format_Grayscale16 || image.depth() > 32 ? 16 : 8;
    ImageFrame native;
    native.descriptor.size = image.size();
    native.descriptor.validBits = native.descriptor.storageBits = bits;
    native.storage = std::move(image);
    const auto histogram = DisplayHistogramAnalyzer::analyzeNativeRgb(native, cancelled);
    auto result = displayHistogramMap(histogram);
    if (!histogram.isValid()) return result;
    // Encoded files expose Luma/R/G/B, all computed from the same native code values.
    // Grayscale inputs use this same channel list; their RGB distributions coincide.
    result[QStringLiteral("domain")] = QStringLiteral("RGB");
    result[QStringLiteral("validBits")] = bits;
    return result;
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
            {QStringLiteral("channels"), channels}, {QStringLiteral("summary"), summary},
            {QStringLiteral("domain"), histogram.domain == RawHistogramDomain::Yuv ? QStringLiteral("YUV") : QStringLiteral("Bayer")},
            {QStringLiteral("validBits"), histogram.validBits},
            {QStringLiteral("sourceWidth"), histogram.logicalSize.width()},
            {QStringLiteral("sourceHeight"), histogram.logicalSize.height()}};
}

} // namespace

ImagePropertiesController::ImagePropertiesController(ImageLoader* loader, QObject* parent)
    : QObject(parent), loader_(loader) {
    Q_ASSERT(loader_);
    loadTimer_.setSingleShot(true);
    connect(&loadTimer_, &QTimer::timeout, this, &ImagePropertiesController::startLoading);
    connect(loader_, &ImageLoader::rawParametersChanged, this, [this](const QString& path) {
        if (!path_.isEmpty() && QFileInfo(path).absoluteFilePath() == path_) reload();
    });
    connect(loader_, &ImageLoader::displayRevisionChanged, this, [this] {
        if (!path_.isEmpty()) reload();
    });
}

ImagePropertiesController::~ImagePropertiesController() {
    loadHandle_.cancel();
    metadataHandle_.cancel();
    for (const auto& handle : histogramHandles_) handle.cancel();
}

void ImagePropertiesController::clear() {
    loadTimer_.stop();
    loadHandle_.cancel();
    metadataHandle_.cancel();
    ++loadGeneration_;
    pendingHistogramSources_.clear();
    frame_.reset();
    path_.clear();
    fileName_.clear();
    directory_ = loading_ = metadataLoading_ = false;
    errorText_.clear();
    metadataError_.clear();
    overviewGroups_.clear();
    advancedGroups_.clear();
    warnings_.clear();
    resetHistograms();
    emit stateChanged();
}

void ImagePropertiesController::loadPath(const QString& requestedPath, bool deferred) {
    clear();
    if (requestedPath.isEmpty()) return;
    const QFileInfo info(requestedPath);
    path_ = info.absoluteFilePath();
    fileName_ = info.fileName().isEmpty() ? QDir::toNativeSeparators(path_) : info.fileName();
    directory_ = info.isDir();
    loading_ = !directory_;
    rebuildFields();
    emit stateChanged();
    if (!directory_) loadTimer_.start(deferred ? 150 : 0);
}

void ImagePropertiesController::reload() {
    const QString current = path_;
    const bool displayRequested = !displayHistogram_.isEmpty() || pendingHistogramSources_.contains(0);
    const bool sourceRequested = !sourceHistogram_.isEmpty() || pendingHistogramSources_.contains(1);
    loadPath(current);
    if (displayRequested) pendingHistogramSources_.insert(0);
    if (sourceRequested) pendingHistogramSources_.insert(1);
}

void ImagePropertiesController::retranslate() {
    rebuildFields();
    emit stateChanged();
}

void ImagePropertiesController::startLoading() {
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
            self->metadataLoading_ = true;
            self->setFrame(result.frame);
            self->metadataHandle_ = self->loader_->requestMetadata(generation, result.frame,
                [self, generation](quint64 id, const DecodeResult& result) {
                    if (!self || self->loadGeneration_ != generation || id != generation) return;
                    self->metadataLoading_ = false;
                    if (result.frame) self->setFrame(result.frame);
                    else {
                        self->metadataError_ = result.error;
                        self->rebuildFields();
                        emit self->stateChanged();
                    }
                });
        }, RequestOptions{LoadCategory::Metadata, 10, QStringLiteral("properties")});
}

void ImagePropertiesController::setFrame(ImageFramePtr frame) {
    // Metadata enrichment keeps the same pixels and must not invalidate cached histograms.
    frame_ = std::move(frame);
    rebuildFields();
    emit stateChanged();
    const auto requested = pendingHistogramSources_;
    pendingHistogramSources_.clear();
    for (int source : requested) requestHistogram(source);
}

void ImagePropertiesController::rebuildFields() {
    overviewGroups_.clear();
    advancedGroups_.clear();
    warnings_.clear();
    if (path_.isEmpty()) return;
    const QFileInfo info(path_);
    QVariantList files;
    if (directory_) {
        appendField(files, QStringLiteral("folder-name"), QT_TRANSLATE_NOOP("ImageProperties", "Name"), fileName_);
        appendField(files, QStringLiteral("folder-type"), QT_TRANSLATE_NOOP("ImageProperties", "Type"), text(QT_TRANSLATE_NOOP("ImageProperties", "Folder")));
    } else {
        appendField(files, QStringLiteral("file-size"), QT_TRANSLATE_NOOP("ImageProperties", "File size"), byteSizeText(info.size()), true);
    }
    appendField(files, QStringLiteral("path"), QT_TRANSLATE_NOOP("ImageProperties", "Full path"), QDir::toNativeSeparators(path_));
    appendField(files, QStringLiteral("modified"), QT_TRANSLATE_NOOP("ImageProperties", "Modified"), QLocale().toString(info.lastModified(), QLocale::ShortFormat));
    appendGroup(overviewGroups_, QStringLiteral("file"), directory_ ? QT_TRANSLATE_NOOP("ImageProperties", "Folder") : QT_TRANSLATE_NOOP("ImageProperties", "File"), files);
    if (directory_) return;
    if (!frame_) {
        if (auto parameters = loader_->rawParameters(path_))
            appendGroup(advancedGroups_, QStringLiteral("raw"), QT_TRANSLATE_NOOP("ImageProperties", "RAW / YUV parameters"), rawFieldRows(*parameters));
        return;
    }
    const auto& m = frame_->metadata;
    const auto& d = frame_->descriptor;
    const QSize size = m.sourceSize.isValid() ? m.sourceSize : d.size;
    QVariantList image;
    appendField(image, QStringLiteral("format"), QT_TRANSLATE_NOOP("ImageProperties", "Format"), m.format);
    appendField(image, QStringLiteral("dimensions"), QT_TRANSLATE_NOOP("ImageProperties", "Dimensions"), QStringLiteral("%1 × %2 px").arg(size.width()).arg(size.height()), true);
    appendField(image, QStringLiteral("bit-depth"), QT_TRANSLATE_NOOP("ImageProperties", "Bit depth"), text(QT_TRANSLATE_NOOP("ImageProperties", "%1-bit valid / %2-bit storage")).arg(d.validBits).arg(d.storageBits), true);
    appendField(image, QStringLiteral("source-color-space"), QT_TRANSLATE_NOOP("ImageProperties", "Source color space"), d.sourceColor.colorSpace);
    appendField(image, QStringLiteral("display-color-space"), QT_TRANSLATE_NOOP("ImageProperties", "Display color space"), d.displayColor.colorSpace);
    overviewGroups_.prepend(QVariantMap{{QStringLiteral("id"), QStringLiteral("image")},
        {QStringLiteral("title"), text(QT_TRANSLATE_NOOP("ImageProperties", "Image"))}, {QStringLiteral("fields"), image}});

    QVariantList color;
    appendField(color, QStringLiteral("source-primaries"), QT_TRANSLATE_NOOP("ImageProperties", "Source primaries"), d.sourceColor.primaries);
    appendField(color, QStringLiteral("source-transfer"), QT_TRANSLATE_NOOP("ImageProperties", "Source transfer"), d.sourceColor.transferFunction);
    appendField(color, QStringLiteral("source-matrix"), QT_TRANSLATE_NOOP("ImageProperties", "Source matrix"), d.sourceColor.matrixCoefficients);
    appendField(color, QStringLiteral("source-range"), QT_TRANSLATE_NOOP("ImageProperties", "Source range"), text(d.sourceColor.fullRange ? QT_TRANSLATE_NOOP("ImageProperties", "Full") : QT_TRANSLATE_NOOP("ImageProperties", "Limited")));
    appendField(color, QStringLiteral("chroma-location"), QT_TRANSLATE_NOOP("ImageProperties", "Chroma location"), d.sourceColor.chromaLocation);
    appendField(color, QStringLiteral("display-primaries"), QT_TRANSLATE_NOOP("ImageProperties", "Display primaries"), d.displayColor.primaries);
    appendField(color, QStringLiteral("display-transfer"), QT_TRANSLATE_NOOP("ImageProperties", "Display transfer"), d.displayColor.transferFunction);
    appendGroup(advancedGroups_, QStringLiteral("color"), QT_TRANSLATE_NOOP("ImageProperties", "Color"), color);
    if (m.colorProfile) {
        const auto& p = *m.colorProfile;
        QVariantList icc;
        appendField(icc, QStringLiteral("icc-description"), QT_TRANSLATE_NOOP("ImageProperties", "Source profile"), p.sourceDescription);
        appendField(icc, QStringLiteral("icc-fingerprint"), QT_TRANSLATE_NOOP("ImageProperties", "Profile fingerprint"), p.sourceFingerprint, true);
        appendField(icc, QStringLiteral("icc-destination"), QT_TRANSLATE_NOOP("ImageProperties", "Profile destination"), p.destinationColorSpace);
        appendField(icc, QStringLiteral("icc-engine"), QT_TRANSLATE_NOOP("ImageProperties", "Transform engine"), p.transformEngine);
        appendField(icc, QStringLiteral("icc-intent"), QT_TRANSLATE_NOOP("ImageProperties", "Rendering intent"), p.renderingIntent);
        appendField(icc, QStringLiteral("icc-status"), QT_TRANSLATE_NOOP("ImageProperties", "Conversion status"), text(p.converted ? QT_TRANSLATE_NOOP("ImageProperties", "Converted") : QT_TRANSLATE_NOOP("ImageProperties", "Not converted")));
        appendGroup(advancedGroups_, QStringLiteral("icc"), QT_TRANSLATE_NOOP("ImageProperties", "ICC profile"), icc);
    }
    QVariantList capture, metadata;
    if (m.camera) {
        const auto& c = *m.camera;
        appendField(capture, QStringLiteral("camera"), QT_TRANSLATE_NOOP("ImageProperties", "Camera"), (c.make + QLatin1Char(' ') + c.model).trimmed());
        appendField(capture, QStringLiteral("lens"), QT_TRANSLATE_NOOP("ImageProperties", "Lens"), c.lens);
        appendField(capture, QStringLiteral("exposure"), QT_TRANSLATE_NOOP("ImageProperties", "Exposure time"), exposureText(c.exposureSeconds), true);
        appendField(capture, QStringLiteral("aperture"), QT_TRANSLATE_NOOP("ImageProperties", "Aperture"), c.aperture > 0 ? QStringLiteral("f/%1").arg(c.aperture,0,'g',3) : QString{}, true);
        appendField(capture, QStringLiteral("iso"), QT_TRANSLATE_NOOP("ImageProperties", "ISO"), c.iso > 0 ? QString::number(c.iso) : QString{}, true);
        appendField(capture, QStringLiteral("focal-length"), QT_TRANSLATE_NOOP("ImageProperties", "Focal length"), c.focalLengthMm > 0 ? QStringLiteral("%1 mm").arg(c.focalLengthMm,0,'g',4) : QString{}, true);
        appendField(capture, QStringLiteral("captured"), QT_TRANSLATE_NOOP("ImageProperties", "Captured at"), QLocale().toString(c.capturedAt, QLocale::ShortFormat));
        appendField(metadata, QStringLiteral("make"), QT_TRANSLATE_NOOP("ImageProperties", "Make"), c.make);
        appendField(metadata, QStringLiteral("model"), QT_TRANSLATE_NOOP("ImageProperties", "Model"), c.model);
        appendField(metadata, QStringLiteral("software"), QT_TRANSLATE_NOOP("ImageProperties", "Software"), c.software);
        appendField(metadata, QStringLiteral("exposure-program"), QT_TRANSLATE_NOOP("ImageProperties", "Exposure program"), c.exposureProgram);
        appendField(metadata, QStringLiteral("metering"), QT_TRANSLATE_NOOP("ImageProperties", "Metering mode"), c.meteringMode);
        appendField(metadata, QStringLiteral("exposure-compensation"), QT_TRANSLATE_NOOP("ImageProperties", "Exposure compensation"), c.exposureCompensation);
        appendField(metadata, QStringLiteral("flash"), QT_TRANSLATE_NOOP("ImageProperties", "Flash"), c.flash);
        appendField(metadata, QStringLiteral("gps"), QT_TRANSLATE_NOOP("ImageProperties", "GPS"), c.gps);
        if (c.sensorSize.isValid()) appendField(metadata, QStringLiteral("sensor-size"), QT_TRANSLATE_NOOP("ImageProperties", "Raw sensor dimensions (px)"), QStringLiteral("%1 × %2").arg(c.sensorSize.width()).arg(c.sensorSize.height()), true);
    }
    appendGroup(overviewGroups_, QStringLiteral("capture"), QT_TRANSLATE_NOOP("ImageProperties", "Capture"), capture);
    if (m.descriptive) {
        QVariantList description;
        appendField(description, QStringLiteral("title"), QT_TRANSLATE_NOOP("ImageProperties", "Title"), m.descriptive->title);
        appendField(description, QStringLiteral("creator"), QT_TRANSLATE_NOOP("ImageProperties", "Creator"), m.descriptive->creator);
        appendField(description, QStringLiteral("description"), QT_TRANSLATE_NOOP("ImageProperties", "Description"), m.descriptive->description);
        appendField(description, QStringLiteral("copyright"), QT_TRANSLATE_NOOP("ImageProperties", "Copyright"), m.descriptive->copyright);
        appendGroup(overviewGroups_, QStringLiteral("description"), QT_TRANSLATE_NOOP("ImageProperties", "Description"), description);
    }
    appendField(metadata, QStringLiteral("orientation"), QT_TRANSLATE_NOOP("ImageProperties", "Orientation"), metadataOrientationText(m.sourceOrientation));
    appendGroup(advancedGroups_, QStringLiteral("metadata"), QT_TRANSLATE_NOOP("ImageProperties", "Metadata"), metadata);
    if (frame_->rawParameters) appendGroup(advancedGroups_, QStringLiteral("raw"), QT_TRANSLATE_NOOP("ImageProperties", "RAW / YUV parameters"), rawFieldRows(*frame_->rawParameters));
    else if (auto parameters = loader_->rawParameters(path_)) appendGroup(advancedGroups_, QStringLiteral("raw"), QT_TRANSLATE_NOOP("ImageProperties", "RAW / YUV parameters"), rawFieldRows(*parameters));
    if (!m.colorWarning.isEmpty()) warnings_.append(m.colorWarning);
    if (!m.metadataWarning.isEmpty()) warnings_.append(m.metadataWarning);
    if (!metadataError_.isEmpty()) warnings_.append(metadataError_);
}

bool ImagePropertiesController::hasHistogramSource() const {
    return frame_ &&
           (RawPlaneAccessor(*frame_).isValid() || frame_->qImage());
}

void ImagePropertiesController::requestHistogram(int source) {
    source = source == 1 ? 1 : 0;
    if (!frame_) {
        pendingHistogramSources_.insert(source);
        return;
    }
    if (source == 1 && !hasHistogramSource()) {
        sourceHistogram_ = {{QStringLiteral("valid"), false}, {QStringLiteral("loading"), false},
                            {QStringLiteral("message"),
                             QStringLiteral("Source planes are unavailable for this image")}};
        ++histogramRevision_;
        emit histogramChanged(source);
        emit histogramRevisionChanged();
        return;
    }
    const QVariantMap& existing = source == 0 ? displayHistogram_ : sourceHistogram_;
    if (existing.value(QStringLiteral("valid")).toBool() ||
        existing.value(QStringLiteral("loading")).toBool()) return;
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
    histogramHandles_[source].cancel();
    histogramHandles_[source] = loader_->requestAnalysis([frame, source](const auto& cancelled) {
        return source == 0
            ? displayHistogramMap(DisplayHistogramAnalyzer::analyze(*frame,
                DisplayHistogramAnalyzer::kDefaultMaximumSamples, cancelled))
            : RawPlaneAccessor(*frame).isValid()
                ? rawHistogramMap(RawPlaneHistogramAnalyzer::analyze(*frame,
                    RawPlaneHistogramAnalyzer::kDefaultMaximumSamplesPerChannel, cancelled))
                : nativeFileHistogramMap(frame->metadata.path, cancelled);
    }, [self, source, requestedGeneration](QVariantMap result) {
        if (self) self->setHistogram(source, requestedGeneration, std::move(result));
    });
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
    for (const auto& handle : histogramHandles_) handle.cancel();
    ++displayHistogramGeneration_;
    ++sourceHistogramGeneration_;
    displayHistogram_.clear();
    sourceHistogram_.clear();
    ++histogramRevision_;
    emit histogramRevisionChanged();
}

} // namespace mvpview
