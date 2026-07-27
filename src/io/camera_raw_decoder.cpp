#include "io/camera_raw_decoder.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>

#if ISPVIEW_HAS_LIBRAW
#include <libraw.h>
#endif

namespace ispview {
namespace {

QStringList cameraRawSuffixes() {
    return {QStringLiteral("dng"), QStringLiteral("cr2"), QStringLiteral("cr3"),
            QStringLiteral("crw"), QStringLiteral("nef"), QStringLiteral("nrw"),
            QStringLiteral("arw"), QStringLiteral("sr2"), QStringLiteral("srf"),
            QStringLiteral("raf"), QStringLiteral("rw2"), QStringLiteral("orf"),
            QStringLiteral("pef"), QStringLiteral("srw"), QStringLiteral("x3f"),
            QStringLiteral("rwl")};
}

#if ISPVIEW_HAS_LIBRAW

struct ProcessedImageDeleter {
    void operator()(libraw_processed_image_t* image) const {
        if (image) {
            LibRaw::dcraw_clear_mem(image);
        }
    }
};

using ProcessedImage = std::unique_ptr<libraw_processed_image_t, ProcessedImageDeleter>;

QString libRawError(int code) {
    const char* message = libraw_strerror(code);
    return message ? QString::fromLocal8Bit(message) : QStringLiteral("Unknown LibRaw error");
}

int openFile(LibRaw& processor, const QString& path) {
#if defined(Q_OS_WIN)
    return processor.open_file(reinterpret_cast<const wchar_t*>(path.utf16()));
#else
    const QByteArray nativePath = QFile::encodeName(path);
    return processor.open_file(nativePath.constData());
#endif
}

QImage processedImageToQImage(const libraw_processed_image_t& processed, QString* error) {
    if (processed.type == LIBRAW_IMAGE_JPEG) {
        if (processed.data_size > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
            if (error) {
                *error = QStringLiteral("LibRaw embedded JPEG is too large");
            }
            return {};
        }
        QByteArray encoded = QByteArray::fromRawData(
            reinterpret_cast<const char*>(processed.data), static_cast<int>(processed.data_size));
        QBuffer buffer(&encoded);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "JPEG");
        reader.setAutoTransform(true);
        QImage image = reader.read();
        if (image.isNull() && error) {
            *error = QStringLiteral("Invalid LibRaw embedded JPEG preview: %1")
                         .arg(reader.errorString());
        }
        return image;
    }
    if (processed.type != LIBRAW_IMAGE_BITMAP || processed.bits != 8 ||
        (processed.colors != 3 && processed.colors != 4)) {
        if (error) {
            *error = QStringLiteral("Unsupported LibRaw output: type %1, %2-bit, %3 channels")
                         .arg(static_cast<int>(processed.type))
                         .arg(processed.bits)
                         .arg(processed.colors);
        }
        return {};
    }

    const qsizetype bytesPerPixel = processed.colors;
    const qsizetype expected = static_cast<qsizetype>(processed.width) * processed.height *
                               bytesPerPixel;
    if (expected <= 0 || expected > processed.data_size) {
        if (error) {
            *error = QStringLiteral("LibRaw returned a truncated bitmap");
        }
        return {};
    }

    const QImage::Format sourceFormat = processed.colors == 3 ? QImage::Format_RGB888
                                                               : QImage::Format_RGBA8888;
    const qsizetype sourceStride = static_cast<qsizetype>(processed.width) * bytesPerPixel;
    const QImage source(processed.data, processed.width, processed.height,
                        static_cast<qsizetype>(sourceStride), sourceFormat);
    return source.convertToFormat(QImage::Format_RGBA8888);
}

ImageMetadata metadataFor(const QString& path, const LibRaw& processor) {
    const QFileInfo fileInfo(path);
    ImageMetadata metadata;
    metadata.path = fileInfo.absoluteFilePath();
    metadata.fileName = fileInfo.fileName();
    metadata.format = fileInfo.suffix().toUpper();
    metadata.fileSize = fileInfo.size();
    metadata.modifiedAt = fileInfo.lastModified();
    metadata.decoderName = QStringLiteral("LibRaw");
    metadata.decoderVersion = QString::fromLatin1(libraw_version());

    ImageMetadata::Camera camera;
    camera.make = QString::fromLocal8Bit(processor.imgdata.idata.make).trimmed();
    camera.model = QString::fromLocal8Bit(processor.imgdata.idata.model).trimmed();
    camera.lens = QString::fromLocal8Bit(processor.imgdata.lens.Lens).trimmed();
    if (camera.lens.isEmpty()) {
        camera.lens = QString::fromLocal8Bit(processor.imgdata.lens.makernotes.Lens).trimmed();
    }
    camera.exposureSeconds = processor.imgdata.other.shutter;
    camera.aperture = processor.imgdata.other.aperture;
    camera.focalLengthMm = processor.imgdata.other.focal_len;
    const double iso = processor.imgdata.other.iso_speed;
    if (std::isfinite(iso) && iso > 0.0 && iso <= std::numeric_limits<int>::max()) {
        camera.iso = static_cast<int>(std::lround(iso));
    }
    if (processor.imgdata.other.timestamp > 0) {
        camera.capturedAt = QDateTime::fromSecsSinceEpoch(processor.imgdata.other.timestamp);
    }
    camera.sensorSize = {processor.imgdata.sizes.raw_width, processor.imgdata.sizes.raw_height};
    metadata.camera = std::move(camera);
    const int imageWidth = processor.imgdata.sizes.iwidth > 0 ? processor.imgdata.sizes.iwidth
                                                               : processor.imgdata.sizes.width;
    const int imageHeight = processor.imgdata.sizes.iheight > 0
                                ? processor.imgdata.sizes.iheight
                                : processor.imgdata.sizes.height;
    metadata.sourceSize = {imageWidth, imageHeight};
    return metadata;
}

std::optional<RawImageParameters> rawParametersFor(LibRaw& processor) {
    const auto& sizes = processor.imgdata.sizes;
    const int width = sizes.iwidth > 0 ? sizes.iwidth : sizes.width;
    const int height = sizes.iheight > 0 ? sizes.iheight : sizes.height;
    if (width <= 0 || height <= 0 || processor.imgdata.idata.filters == 0) {
        return std::nullopt;
    }

    const int cfa[4] = {processor.COLOR(0, 0), processor.COLOR(0, 1),
                        processor.COLOR(1, 0), processor.COLOR(1, 1)};
    BayerPattern pattern;
    if (cfa[0] == 0 && cfa[1] == 1 && cfa[2] == 3 && cfa[3] == 2) {
        pattern = BayerPattern::RGGB;
    } else if (cfa[0] == 1 && cfa[1] == 0 && cfa[2] == 2 && cfa[3] == 3) {
        pattern = BayerPattern::GRBG;
    } else if (cfa[0] == 3 && cfa[1] == 2 && cfa[2] == 0 && cfa[3] == 1) {
        pattern = BayerPattern::GBRG;
    } else if (cfa[0] == 2 && cfa[1] == 3 && cfa[2] == 1 && cfa[3] == 0) {
        pattern = BayerPattern::BGGR;
    } else {
        return std::nullopt;
    }

    RawImageParameters parameters;
    parameters.size = {width, height};
    parameters.format = RawPixelFormat::Raw16;
    parameters.rowStride = sizes.raw_pitch > 0
                               ? sizes.raw_pitch
                               : static_cast<qsizetype>(width) * sizeof(quint16);
    parameters.bayerPattern = pattern;
    parameters.demosaic = true;
    parameters.orientation = ImageOrientation::Normal;
    switch (sizes.flip) {
    case 3:
        parameters.orientation = ImageOrientation::Rotate180;
        break;
    case 5:
        parameters.orientation = ImageOrientation::Rotate90Clockwise;
        break;
    case 6:
        parameters.orientation = ImageOrientation::Rotate270Clockwise;
        break;
    default:
        break;
    }

    const unsigned maximum = processor.imgdata.color.maximum;
    if (maximum > 0 && maximum <= 65535) {
        parameters.whiteLevel = static_cast<int>(maximum);
        int validBits = 0;
        while (validBits < 16 && ((1u << validBits) - 1u) < maximum) {
            ++validBits;
        }
        parameters.validBitsOverride = validBits;
    }
    parameters.blackLevel = static_cast<int>(std::min<unsigned>(processor.imgdata.color.black,
                                                                 65534u));
    if (parameters.whiteLevel > parameters.blackLevel) {
        parameters.whiteLevel = std::max(parameters.whiteLevel, parameters.blackLevel + 1);
    }

    bool validWhiteBalance = true;
    for (int channel = 0; channel < 3; ++channel) {
        const float gain = processor.imgdata.color.cam_mul[channel];
        validWhiteBalance = validWhiteBalance && std::isfinite(gain) && gain > 0.0F;
        parameters.whiteBalanceGains[static_cast<std::size_t>(channel)] = gain;
    }
    if (!validWhiteBalance) {
        parameters.whiteBalanceGains = {1.0, 1.0, 1.0};
    }

    bool validMatrix = true;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const float value = processor.imgdata.color.rgb_cam[row][column];
            validMatrix = validMatrix && std::isfinite(value);
            parameters.colorCorrectionMatrix[static_cast<std::size_t>(row * 3 + column)] = value;
        }
    }
    if (!validMatrix) {
        parameters.colorCorrectionMatrix = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0,
                                            0.0, 0.0, 1.0};
    }
    return parameters;
}

DecodeResult frameFromImage(QImage image, ImageMetadata metadata,
                            std::optional<RawImageParameters> rawParameters,
                            const QSize& maximumSize) {
    if (image.isNull()) {
        return {{}, QStringLiteral("LibRaw produced an empty image")};
    }
    if (!maximumSize.isEmpty() &&
        (image.width() > maximumSize.width() || image.height() > maximumSize.height())) {
        image = image.scaled(maximumSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (image.format() != QImage::Format_RGBA8888) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
    }

    auto frame = std::make_shared<ImageFrame>();
    frame->descriptor.size = image.size();
    frame->descriptor.layout = PixelLayout::Interleaved;
    frame->descriptor.sampleType = SampleType::UInt;
    frame->descriptor.channelOrder = ChannelOrder::RGBA;
    frame->descriptor.storageBits = 8;
    frame->descriptor.validBits = 8;
    frame->metadata = std::move(metadata);
    frame->rawParameters = std::move(rawParameters);
    frame->storage = std::move(image);
    return {std::move(frame), {}};
}

DecodeResult decodeWithLibRaw(const DecodeRequest& request) {
    // LibRaw is a large object (roughly 800 KiB in supported releases). Keep it
    // off the smaller platform worker-thread stack while retaining RAII cleanup.
    auto processor = std::make_unique<LibRaw>();
    int code = openFile(*processor, request.path);
    if (code != LIBRAW_SUCCESS) {
        return {{}, QStringLiteral("LibRaw could not open %1: %2")
                        .arg(QFileInfo(request.path).fileName(), libRawError(code))};
    }
    ImageMetadata metadata = metadataFor(request.path, *processor);
    std::optional<RawImageParameters> rawParameters = rawParametersFor(*processor);

    if (request.purpose != DecodePurpose::Full) {
        code = processor->unpack_thumb();
        if (code == LIBRAW_SUCCESS) {
            int imageError = LIBRAW_SUCCESS;
            ProcessedImage processed(processor->dcraw_make_mem_thumb(&imageError));
            if (processed && imageError == LIBRAW_SUCCESS) {
                QString conversionError;
                QImage image = processedImageToQImage(*processed, &conversionError);
                if (!image.isNull()) {
                    return frameFromImage(std::move(image), std::move(metadata),
                                          std::move(rawParameters),
                                          request.maximumSize);
                }
            }
        }

        // A RAW without an embedded preview still remains viewable. Reopen because
        // failed thumbnail extraction can leave the processor in a progressed state.
        processor->recycle();
        code = openFile(*processor, request.path);
        if (code != LIBRAW_SUCCESS) {
            return {{}, QStringLiteral("LibRaw could not reopen the RAW file: %1")
                            .arg(libRawError(code))};
        }
        metadata = metadataFor(request.path, *processor);
        rawParameters = rawParametersFor(*processor);
        processor->imgdata.params.half_size = 1;
    }

    processor->imgdata.params.output_bps = 8;
    processor->imgdata.params.output_color = 1;
    processor->imgdata.params.use_camera_wb = 1;
    code = processor->unpack();
    if (code == LIBRAW_SUCCESS) {
        code = processor->dcraw_process();
    }
    if (code != LIBRAW_SUCCESS) {
        return {{}, QStringLiteral("LibRaw decode failed for %1: %2")
                        .arg(QFileInfo(request.path).fileName(), libRawError(code))};
    }

    int imageError = LIBRAW_SUCCESS;
    ProcessedImage processed(processor->dcraw_make_mem_image(&imageError));
    if (!processed || imageError != LIBRAW_SUCCESS) {
        return {{}, QStringLiteral("LibRaw image conversion failed: %1")
                        .arg(libRawError(imageError))};
    }
    QString conversionError;
    QImage image = processedImageToQImage(*processed, &conversionError);
    if (image.isNull()) {
        return {{}, conversionError};
    }
    return frameFromImage(std::move(image), std::move(metadata), std::move(rawParameters),
                          request.maximumSize);
}

#endif

} // namespace

bool CameraRawDecoder::isAvailable() {
#if ISPVIEW_HAS_LIBRAW
    return true;
#else
    return false;
#endif
}

QStringList CameraRawDecoder::supportedSuffixes() {
    return isAvailable() ? cameraRawSuffixes() : QStringList{};
}

QString CameraRawDecoder::cacheIdentity() const {
#if ISPVIEW_HAS_LIBRAW
    return QStringLiteral("camera-raw-v2|libraw-%1")
        .arg(QString::fromLatin1(libraw_version()));
#else
    return QStringLiteral("camera-raw-disabled");
#endif
}

DecodeExecutionMode CameraRawDecoder::executionMode(const QString& path) const {
    return canDecode(path) ? DecodeExecutionMode::Serialized : DecodeExecutionMode::Parallel;
}

bool CameraRawDecoder::canDecode(const QString& path) const {
    return isAvailable() && cameraRawSuffixes().contains(QFileInfo(path).suffix().toLower());
}

DecodeResult CameraRawDecoder::decode(const DecodeRequest& request) const {
    if (!canDecode(request.path)) {
        return {{},
                QStringLiteral("Camera RAW support is unavailable or the format is unsupported")};
    }
#if ISPVIEW_HAS_LIBRAW
    // Even the reentrant LibRaw build can delegate to optional codec dependencies
    // with shared process state. Serializing this adapter also bounds peak memory
    // when thumbnail, preview, and full requests for a large RAW overlap.
    static std::mutex decoderMutex;
    const std::scoped_lock lock(decoderMutex);
    try {
        return decodeWithLibRaw(request);
    } catch (const std::bad_alloc&) {
        return {{}, QStringLiteral("LibRaw could not allocate enough memory")};
    } catch (const std::exception& exception) {
        return {{}, QStringLiteral("LibRaw exception: %1")
                        .arg(QString::fromLocal8Bit(exception.what()))};
    } catch (...) {
        return {{}, QStringLiteral("Unknown LibRaw exception")};
    }
#else
    return {{}, QStringLiteral("This build does not include LibRaw")};
#endif
}

} // namespace ispview
