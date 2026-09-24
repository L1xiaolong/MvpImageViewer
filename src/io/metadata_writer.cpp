#include "io/metadata_writer.h"

#include "io/metadata_exiv2_lock.h"

#include <QFile>

#include <exiv2/exiv2.hpp>

#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace ispview {
namespace {

struct OpenedImage {
    std::shared_ptr<QByteArray> backing;
    Exiv2::Image::UniquePtr image;
};

OpenedImage openSource(const QString& path) {
    const QByteArray nativePath = QFile::encodeName(path);
#if defined(Q_OS_WIN)
    if (QFile::decodeName(nativePath) != path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            throw std::runtime_error("could not open source image metadata");
        }
        auto bytes = std::make_shared<QByteArray>(file.readAll());
        auto image = Exiv2::ImageFactory::open(
            reinterpret_cast<const Exiv2::byte*>(bytes->constData()),
            static_cast<std::size_t>(bytes->size()));
        return {std::move(bytes), std::move(image)};
    }
#endif
    return {{}, Exiv2::ImageFactory::open(nativePath.toStdString())};
}

void updateDimensions(Exiv2::ExifData& exif, Exiv2::XmpData& xmp, const QSize& size) {
    Exiv2::ExifThumb(exif).erase();
    exif["Exif.Image.Orientation"] = static_cast<uint16_t>(1);
    exif["Exif.Image.ImageWidth"] = static_cast<uint32_t>(size.width());
    exif["Exif.Image.ImageLength"] = static_cast<uint32_t>(size.height());
    exif["Exif.Photo.PixelXDimension"] = static_cast<uint32_t>(size.width());
    exif["Exif.Photo.PixelYDimension"] = static_cast<uint32_t>(size.height());

    xmp["Xmp.tiff.Orientation"] = static_cast<int32_t>(1);
    xmp["Xmp.tiff.ImageWidth"] = static_cast<int32_t>(size.width());
    xmp["Xmp.tiff.ImageLength"] = static_cast<int32_t>(size.height());
    xmp["Xmp.exif.PixelXDimension"] = static_cast<int32_t>(size.width());
    xmp["Xmp.exif.PixelYDimension"] = static_cast<int32_t>(size.height());
}

} // namespace

QString MetadataWriter::copyForImageTransform(const QString& sourcePath,
                                              QByteArray& encodedImage,
                                              const QSize& pixelSize) {
    const std::scoped_lock lock(exiv2MetadataMutex());
    try {
        OpenedImage source = openSource(sourcePath);
        if (!source.image) {
            return QStringLiteral("Exiv2 could not open the source metadata.");
        }
        source.image->readMetadata();
        Exiv2::ExifData exif = source.image->exifData();
        Exiv2::IptcData iptc = source.image->iptcData();
        Exiv2::XmpData xmp = source.image->xmpData();
        const std::string comment = source.image->comment();
        if (exif.empty() && iptc.empty() && xmp.empty() && comment.empty()) {
            return {};
        }

        updateDimensions(exif, xmp, pixelSize);
        auto output = Exiv2::ImageFactory::open(
            reinterpret_cast<const Exiv2::byte*>(encodedImage.constData()),
            static_cast<std::size_t>(encodedImage.size()));
        if (!output) {
            return QStringLiteral("Exiv2 could not open the transformed image.");
        }
        output->readMetadata();
        output->setExifData(exif);
        output->setIptcData(iptc);
        output->setXmpData(xmp);
        output->setComment(comment);
        output->writeMetadata();

        Exiv2::BasicIo& io = output->io();
        const std::size_t outputSize = io.size();
        if (outputSize > static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
            return QStringLiteral("The transformed image is too large to save.");
        }
        const Exiv2::byte* outputBytes = io.mmap();
        if (!outputBytes && outputSize != 0) {
            return QStringLiteral("Exiv2 could not return the transformed image data.");
        }
        encodedImage = QByteArray(reinterpret_cast<const char*>(outputBytes),
                                  static_cast<qsizetype>(outputSize));
        io.munmap();
        return {};
    } catch (const Exiv2::Error& error) {
        return QStringLiteral("Could not preserve image metadata: %1")
            .arg(QString::fromUtf8(error.what()));
    } catch (const std::exception& error) {
        return QStringLiteral("Could not preserve image metadata: %1")
            .arg(QString::fromUtf8(error.what()));
    } catch (...) {
        return QStringLiteral("Could not preserve image metadata.");
    }
}

} // namespace ispview
