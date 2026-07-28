#include "io/camera_raw_decoder.h"
#include "io/default_image_decoder.h"
#include "io/directory_scanner.h"
#include "io/drop_copy_operation.h"
#include "io/encoded_color_management.h"
#include "io/image_decoder_registry.h"
#include "io/image_loader.h"
#include "io/image_transformer.h"
#include "io/metadata_reader.h"
#include "io/qt_image_decoder.h"
#include "io/raw_image_decoder.h"
#include "io/raw_preset_store.h"
#include "io/single_file_rename.h"
#include "io/supported_image_formats.h"
#include "io/thumbnail_disk_cache.h"

#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QtEndian>
#include <QtGui/qrgbafloat.h>

#if ISPVIEW_HAS_EXIV2
#include <exiv2/exiv2.hpp>
#endif

#include <array>
#include <atomic>
#include <limits>
#include <thread>
#include <vector>

namespace ispview {
namespace {

class PrefetchRecordingDecoder final : public IImageDecoder {
  public:
    [[nodiscard]] bool canDecode(const QString&) const override { return true; }

    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override {
        const int frame = request.rawParameters ? request.rawParameters->frameIndex : 0;
        const int purpose = static_cast<int>(request.purpose);
        calls.fetch_or(1U << (frame * 3 + purpose), std::memory_order_relaxed);
        QImage image(2, 2, QImage::Format_RGBA8888);
        image.fill(Qt::black);
        auto result = std::make_shared<ImageFrame>();
        result->descriptor.size = image.size();
        result->metadata.path = request.path;
        result->rawParameters = request.rawParameters;
        result->storage = std::move(image);
        return {std::move(result), {}};
    }

    mutable std::atomic<quint32> calls{0};
};

class DelayedCountingDecoder final : public IImageDecoder {
  public:
    [[nodiscard]] bool canDecode(const QString&) const override { return true; }

    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override {
        calls.fetch_add(1, std::memory_order_relaxed);
        QThread::msleep(100);
        QImage image(4, 4, QImage::Format_RGBA8888);
        image.fill(Qt::black);
        auto result = std::make_shared<ImageFrame>();
        result->descriptor.size = image.size();
        result->metadata.path = request.path;
        result->storage = std::move(image);
        return {std::move(result), {}};
    }

    mutable std::atomic<int> calls{0};
};

class SerializedRecordingDecoder final : public IImageDecoder {
  public:
    [[nodiscard]] DecodeExecutionMode executionMode(const QString& path) const override {
        return path.endsWith(QStringLiteral(".raw")) ? DecodeExecutionMode::Serialized
                                                     : DecodeExecutionMode::Parallel;
    }
    [[nodiscard]] bool canDecode(const QString&) const override { return true; }

    [[nodiscard]] DecodeResult decode(const DecodeRequest& request) const override {
        const bool serialized = request.path.endsWith(QStringLiteral(".raw"));
        if (serialized) {
            const int active =
                activeSerializedCalls.fetch_add(1, std::memory_order_relaxed) + 1;
            int observed = maximumSerializedActive.load(std::memory_order_relaxed);
            while (observed < active &&
                   !maximumSerializedActive.compare_exchange_weak(
                       observed, active, std::memory_order_relaxed)) {
            }
            {
                const QMutexLocker lock(&mutex);
                order.append(QFileInfo(request.path).fileName());
            }
            QThread::msleep(120);
            activeSerializedCalls.fetch_sub(1, std::memory_order_relaxed);
        } else {
            QThread::msleep(10);
        }
        QImage image(4, 4, QImage::Format_RGBA8888);
        image.fill(Qt::black);
        auto frame = std::make_shared<ImageFrame>();
        frame->descriptor.size = image.size();
        frame->metadata.path = request.path;
        frame->storage = std::move(image);
        return {std::move(frame), {}};
    }

    [[nodiscard]] QStringList recordedOrder() const {
        const QMutexLocker lock(&mutex);
        return order;
    }

    mutable QMutex mutex;
    mutable QStringList order;
    mutable std::atomic<int> activeSerializedCalls{0};
    mutable std::atomic<int> maximumSerializedActive{0};
};

constexpr quint32 prefetchCallBit(int frame, DecodePurpose purpose) {
    return 1U << (frame * 3 + static_cast<int>(purpose));
}

#if ISPVIEW_HAS_EXIV2
bool writeSyntheticMetadata(const QString& path) {
    try {
        auto image = Exiv2::ImageFactory::open(path.toStdString());
        if (!image) {
            return false;
        }
        image->readMetadata();
        Exiv2::ExifData& exif = image->exifData();
        exif["Exif.Image.Make"] = "ISPView";
        exif["Exif.Image.Model"] = "Synthetic Camera";
        exif["Exif.Image.Software"] = "Synthetic ISP 1.0";
        exif["Exif.Image.Orientation"] = static_cast<uint16_t>(6);
        exif["Exif.Photo.ExposureTime"] = Exiv2::URational(1, 125);
        exif["Exif.Photo.FNumber"] = Exiv2::URational(28, 10);
        exif["Exif.Photo.ISOSpeedRatings"] = static_cast<uint16_t>(200);
        exif["Exif.Photo.DateTimeOriginal"] = "2026:07:15 09:10:11";
        exif["Exif.Photo.FocalLength"] = Exiv2::URational(106, 10);
        exif["Exif.Photo.LensModel"] = "Synthetic Prime";
        exif["Exif.Photo.ExposureProgram"] = static_cast<uint16_t>(1);
        exif["Exif.Photo.MeteringMode"] = static_cast<uint16_t>(5);
        exif["Exif.Photo.ExposureBiasValue"] = Exiv2::Rational(-1, 3);
        exif["Exif.Photo.Flash"] = static_cast<uint16_t>(0);
        exif["Exif.Image.ImageDescription"] = "EXIF description";
        exif["Exif.Image.Artist"] = "EXIF creator";
        exif["Exif.Image.Copyright"] = "EXIF copyright";
        exif["Exif.GPSInfo.GPSLatitudeRef"] = "N";
        exif["Exif.GPSInfo.GPSLatitude"] = "31/1 12/1 0/1";
        exif["Exif.GPSInfo.GPSLongitudeRef"] = "E";
        exif["Exif.GPSInfo.GPSLongitude"] = "121/1 30/1 0/1";

        Exiv2::IptcData& iptc = image->iptcData();
        iptc["Iptc.Application2.ObjectName"] = "IPTC title";
        iptc["Iptc.Application2.Keywords"] = "calibration";

        Exiv2::XmpData& xmp = image->xmpData();
        xmp["Xmp.dc.title"] = "XMP title";
        xmp["Xmp.dc.creator"] = "XMP creator";
        xmp["Xmp.xmp.Rating"] = static_cast<int32_t>(4);
        xmp["Xmp.dc.subject"] = "sharpness";
        image->writeMetadata();
        return true;
    } catch (...) {
        return false;
    }
}
#endif

} // namespace

class IoTests final : public QObject {
    Q_OBJECT

  private slots:
    void decoderScalesPreviewAndKeepsMetadata();
    void qtDecoderAcceptsOnlyMvpEncodedFormats();
    void singleFileRenameMovesSidecarAndRejectsConflicts();
    void dropCopyCopiesFilesAndFoldersWithoutOverwriting();
    void scannerFiltersAndNaturallySortsFiles();
    void scannerExcludesHiddenFilesAndDirectoryTrees();
    void directoryScannerPublishesIncrementalBatches();
    void recursiveImageFolderScanFindsOnlyBranchesContainingImages();
    void decoderRejectsCorruptInput();
    void metadataCapabilityMatchesBuildFeature();
    void thumbnailDecodeSkipsOptionalMetadata();
    void decoderMapsTypedExifIptcAndXmpMetadata();
    void decoderReadsRealCameraMetadataWithoutCoordinates();
    void metadataFailureDoesNotReplaceSuccessfulPixelDecode();
    void colorManagementCapabilityMatchesBuildFeature();
    void decoderConvertsEmbeddedLinearIccToSrgb();
    void colorManagementRejectsNonRgbProfileWithoutChangingPixels();
    void colorManagementCachedTransformIsThreadSafe();
    void thumbnailDiskCacheRoundTripsImage();
    void imageLoaderDiskCacheKeepsSourceDimensions();
    void nv12LimitedRangeProducesReferencePixels();
    void mipiRawPackingReturnsExactSensorValues();
    void bayerRawDefaultsToMosaicAndDemosaicIsOptIn();
    void decoderRegistryRoutesByFormat();
    void defaultDecoderAndFormatCatalogStayConsistent();
    void cameraRawCapabilityMatchesBuildFeature();
    void cameraRawDecodesLocalDngWhenAvailable();
    void rawFileNameInferenceExtractsCommonParameters();
    void yuvLayoutsProduceEquivalentReferencePixels();
    void p010AndRaw16PreserveHighBitDepthValues();
    void rawOrientationRotatesDisplayWithoutChangingSourcePlane();
    void rawDecoderRejectsTruncatedFramesAndSelectsFrameIndex();
    void rawSidecarRoundTripsParameters();
    void rawPreviewIsBoundedAndDoesNotRetainFullPlanes();
    void rawPreviewDirectlySamplesYuvAndBayerSources();
    void fullRawFrameUsesBoundedFallbackWithoutChangingLogicalSize();
    void bayerDisplayTransformAppliesWhiteBalanceCcmAndGamma();
    void imageLoaderPrefetchesOnlyAdjacentRawFrames();
    void imageLoaderSkipsFullPrefetchWhenFramesExceedBudget();
    void namedRawPresetsRoundTripOverwriteAndDelete();
    void filenameRulesApplyOrderedPresetAndCapturedOverrides();
    void imageLoaderCoalescesIdenticalInFlightRequests();
    void imageLoaderCancellationSuppressesStaleCallbacks();
    void imageLoaderSerializedQueuePrioritizesAndCancelsWithoutBlockingParallelWork();
    void imageLoaderEnforcesCombinedMemoryBudget();
    void imageTransformerRotatesResizesAndRestoresEncodedImage();
    void imageTransformerRotatesAndRestoresNv12Data();
};

void IoTests::decoderScalesPreviewAndKeepsMetadata() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("sample.png"));
    QImage source(640, 480, QImage::Format_RGBA8888);
    source.fill(QColor(12, 34, 56, 200));
    QVERIFY(source.save(path));

    QtImageDecoder decoder;
    const DecodeResult result = decoder.decode({path, DecodePurpose::Preview, QSize(320, 240)});
    QVERIFY(result.succeeded());
    QCOMPARE(result.frame->descriptor.size, QSize(320, 240));
    QCOMPARE(result.frame->metadata.fileName, QStringLiteral("sample.png"));
    QVERIFY(result.frame->qImage() != nullptr);
}

void IoTests::qtDecoderAcceptsOnlyMvpEncodedFormats() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QtImageDecoder decoder;

    QVERIFY(decoder.canDecode(QStringLiteral("image.jpg")));
    QVERIFY(decoder.canDecode(QStringLiteral("image.JPEG")));
    QVERIFY(decoder.canDecode(QStringLiteral("image.png")));
    for (const QString& path :
         {QStringLiteral("image.tif"), QStringLiteral("image.tiff"), QStringLiteral("image.webp"),
          QStringLiteral("image.exr"), QStringLiteral("image.gif"), QStringLiteral("image.heic")}) {
        QVERIFY2(!decoder.canDecode(path), qPrintable(path));
    }
}

void IoTests::singleFileRenameMovesSidecarAndRejectsConflicts() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("capture.raw"));
    const QString destination = directory.filePath(QStringLiteral("renamed.raw"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArrayLiteral("pixels")), 6);
    file.close();
    QFile sidecar(RawPresetStore::sidecarPath(source));
    QVERIFY(sidecar.open(QIODevice::WriteOnly));
    QCOMPARE(sidecar.write(QByteArrayLiteral("{}")), 2);
    sidecar.close();
    QFile backup(ImageTransformer::backupPath(source));
    QVERIFY(backup.open(QIODevice::WriteOnly));
    QCOMPARE(backup.write(QByteArrayLiteral("original")), 8);
    backup.close();
    QFile manifest(ImageTransformer::backupManifestPath(source));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    QCOMPARE(manifest.write(QByteArrayLiteral("{}")), 2);
    manifest.close();

    QString error;
    QVERIFY2(SingleFileRename::execute(source, destination, &error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(source));
    QVERIFY(QFileInfo::exists(destination));
    QVERIFY(QFileInfo::exists(RawPresetStore::sidecarPath(destination)));
    QVERIFY(QFileInfo::exists(ImageTransformer::backupPath(destination)));
    QVERIFY(QFileInfo::exists(ImageTransformer::backupManifestPath(destination)));

    const QString occupied = directory.filePath(QStringLiteral("occupied.raw"));
    QFile occupiedFile(occupied);
    QVERIFY(occupiedFile.open(QIODevice::WriteOnly));
    occupiedFile.close();
    QVERIFY(!SingleFileRename::execute(destination, occupied, &error));
    QVERIFY(QFileInfo::exists(destination));
    QVERIFY(!SingleFileRename::execute(destination, directory.filePath(QStringLiteral("CON.raw")),
                                       &error));
    QVERIFY(QFileInfo::exists(destination));

    const QString folder = directory.filePath(QStringLiteral("album"));
    const QString renamedFolder = directory.filePath(QStringLiteral("renamed-album"));
    QVERIFY(QDir().mkdir(folder));
    QVERIFY2(SingleFileRename::execute(folder, renamedFolder, &error), qPrintable(error));
    QVERIFY(!QFileInfo::exists(folder));
    QVERIFY(QFileInfo(renamedFolder).isDir());
}

void IoTests::dropCopyCopiesFilesAndFoldersWithoutOverwriting() {
    QTemporaryDir sourceRoot;
    QTemporaryDir targetRoot;
    QVERIFY(sourceRoot.isValid());
    QVERIFY(targetRoot.isValid());

    const QString sourceFile = sourceRoot.filePath(QStringLiteral("scene.png"));
    QFile file(sourceFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArrayLiteral("pixels")), 6);
    file.close();

    DropCopyResult first = DropCopyOperation::execute({sourceFile}, targetRoot.path());
    QCOMPARE(first.errors, QStringList{});
    QCOMPARE(first.destinationPaths, QStringList{targetRoot.filePath(QStringLiteral("scene.png"))});
    QCOMPARE(QFile(first.destinationPaths.first()).size(), 6);

    DropCopyResult duplicate = DropCopyOperation::execute({sourceFile}, targetRoot.path());
    QCOMPARE(duplicate.errors, QStringList{});
    QCOMPARE(duplicate.destinationPaths,
             QStringList{targetRoot.filePath(QStringLiteral("scene copy.png"))});

    const QString sourceFolder = sourceRoot.filePath(QStringLiteral("set"));
    QVERIFY(QDir().mkpath(QDir(sourceFolder).filePath(QStringLiteral("nested"))));
    QFile nested(QDir(sourceFolder).filePath(QStringLiteral("nested/frame.jpg")));
    QVERIFY(nested.open(QIODevice::WriteOnly));
    QCOMPARE(nested.write(QByteArrayLiteral("image")), 5);
    nested.close();
    DropCopyResult folder = DropCopyOperation::execute({sourceFolder}, targetRoot.path());
    QCOMPARE(folder.errors, QStringList{});
    QCOMPARE(folder.destinationPaths, QStringList{targetRoot.filePath(QStringLiteral("set"))});
    QVERIFY(QFileInfo::exists(targetRoot.filePath(QStringLiteral("set/nested/frame.jpg"))));

    const QString childTarget = QDir(sourceFolder).filePath(QStringLiteral("nested"));
    DropCopyResult recursive = DropCopyOperation::execute({sourceFolder}, childTarget);
    QVERIFY(recursive.destinationPaths.isEmpty());
    QCOMPARE(recursive.errors.size(), 1);
    QVERIFY(recursive.errors.first().contains(QStringLiteral("inside itself")));

    const QString moveSource = sourceRoot.filePath(QStringLiteral("move.png"));
    QFile moveFile(moveSource);
    QVERIFY(moveFile.open(QIODevice::WriteOnly));
    QCOMPARE(moveFile.write(QByteArrayLiteral("move")), 4);
    moveFile.close();
    DropCopyResult moved =
        FileTransferOperation::execute({moveSource}, targetRoot.path(), FileTransferMode::Move);
    QCOMPARE(moved.errors, QStringList{});
    QCOMPARE(moved.destinationPaths.size(), 1);
    QVERIFY(!QFileInfo::exists(moveSource));
    QCOMPARE(QFile(moved.destinationPaths.first()).size(), 4);

    DropCopyResult sameDirectory = FileTransferOperation::execute(
        {moved.destinationPaths.first()}, targetRoot.path(), FileTransferMode::Move);
    QCOMPARE(sameDirectory.errors, QStringList{});
    QCOMPARE(sameDirectory.destinationPaths, moved.destinationPaths);
    QVERIFY(QFileInfo::exists(moved.destinationPaths.first()));

    DropCopyResult movedFolder =
        FileTransferOperation::execute({sourceFolder}, targetRoot.path(), FileTransferMode::Move);
    QCOMPARE(movedFolder.errors, QStringList{});
    QCOMPARE(movedFolder.destinationPaths.size(), 1);
    QVERIFY(!QFileInfo::exists(sourceFolder));
    QVERIFY(
        QFileInfo(
            QDir(movedFolder.destinationPaths.first()).filePath(QStringLiteral("nested/frame.jpg")))
            .isFile());
}

void IoTests::metadataCapabilityMatchesBuildFeature() {
    QCOMPARE(MetadataReader::isAvailable(), static_cast<bool>(ISPVIEW_HAS_EXIV2));
#if ISPVIEW_HAS_EXIV2
    QVERIFY(!MetadataReader::version().isEmpty());
#else
    QVERIFY(MetadataReader::version().isEmpty());
#endif
}

void IoTests::thumbnailDecodeSkipsOptionalMetadata() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("thumbnail.png"));
    QImage source(16, 16, QImage::Format_RGBA8888);
    source.fill(Qt::cyan);
    QVERIFY(source.save(path));

    QtImageDecoder decoder;
    const DecodeResult result = decoder.decode({path, DecodePurpose::Thumbnail, QSize(8, 8)});
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.frame->descriptor.size, QSize(8, 8));
    QVERIFY(result.frame->metadata.metadataReaderName.isEmpty());
    QVERIFY(!result.frame->metadata.camera.has_value());
}

void IoTests::decoderMapsTypedExifIptcAndXmpMetadata() {
#if ISPVIEW_HAS_EXIV2
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("typed-metadata.jpg"));
    QImage source(2, 1, QImage::Format_RGB32);
    source.setPixelColor(0, 0, Qt::red);
    source.setPixelColor(1, 0, Qt::blue);
    QVERIFY(source.save(path, "JPG", 95));
    QVERIFY(writeSyntheticMetadata(path));

    QtImageDecoder decoder;
    const DecodeResult result = decoder.decode({path, DecodePurpose::Full, {}});
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.frame->descriptor.size, QSize(1, 2));
    QCOMPARE(result.frame->metadata.metadataReaderName, QStringLiteral("Exiv2"));
    QCOMPARE(result.frame->metadata.sourceOrientation, ImageMetadata::Orientation::Rotate90);
    QVERIFY(result.frame->metadata.gpsMetadataPresent);
    QVERIFY2(result.frame->metadata.camera.has_value(),
             qPrintable(result.frame->metadata.metadataWarning));
    QCOMPARE(result.frame->metadata.camera->make, QStringLiteral("ISPView"));
    QCOMPARE(result.frame->metadata.camera->model, QStringLiteral("Synthetic Camera"));
    QCOMPARE(result.frame->metadata.camera->software, QStringLiteral("Synthetic ISP 1.0"));
    QCOMPARE(result.frame->metadata.camera->lens, QStringLiteral("Synthetic Prime"));
    QCOMPARE(result.frame->metadata.camera->iso, 200);
    QVERIFY(std::abs(result.frame->metadata.camera->exposureSeconds - 1.0 / 125.0) < 0.00001);
    QVERIFY(std::abs(result.frame->metadata.camera->aperture - 2.8) < 0.001);
    QVERIFY(result.frame->metadata.camera->capturedAt.isValid());
    QCOMPARE(result.frame->metadata.camera->exposureProgram, QStringLiteral("Manual"));
    QCOMPARE(result.frame->metadata.camera->meteringMode, QStringLiteral("Pattern"));
    QCOMPARE(result.frame->metadata.camera->exposureCompensation, QStringLiteral("-0.3333 EV"));
    QCOMPARE(result.frame->metadata.camera->flash, QStringLiteral("Did not fire"));
    QVERIFY(result.frame->metadata.camera->gps.contains(QStringLiteral("31/1")));
    QVERIFY(result.frame->metadata.camera->gps.contains(QStringLiteral("121/1")));
    QVERIFY(result.frame->metadata.descriptive.has_value());
    QCOMPARE(result.frame->metadata.descriptive->title, QStringLiteral("XMP title"));
    QCOMPARE(result.frame->metadata.descriptive->creator, QStringLiteral("XMP creator"));
    QCOMPARE(result.frame->metadata.descriptive->description, QStringLiteral("EXIF description"));
#else
    QSKIP("This build does not include Exiv2");
#endif
}

void IoTests::decoderReadsRealCameraMetadataWithoutCoordinates() {
#if ISPVIEW_HAS_EXIV2
    const QString path = QFINDTESTDATA("../test_images/XAG040_0001 2.JPG");
    if (path.isEmpty()) {
        QSKIP("Optional local XAG JPEG sample is not available");
    }
    QtImageDecoder decoder;
    const DecodeResult result = decoder.decode({path, DecodePurpose::Preview, QSize(640, 640)});
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QVERIFY2(result.frame->metadata.camera.has_value(),
             qPrintable(result.frame->metadata.metadataWarning));
    QCOMPARE(result.frame->metadata.camera->make, QStringLiteral("XAG"));
    QCOMPARE(result.frame->metadata.camera->model, QStringLiteral("Xcam12DC02"));
    QCOMPARE(result.frame->metadata.camera->iso, 50);
    QVERIFY(std::abs(result.frame->metadata.camera->aperture - 2.8) < 0.001);
    QVERIFY(std::abs(result.frame->metadata.camera->focalLengthMm - 10.6) < 0.001);
    QVERIFY(result.frame->metadata.gpsMetadataPresent);
    QVERIFY(result.frame->metadata.descriptive.has_value());
    QCOMPARE(result.frame->metadata.descriptive->description, QStringLiteral("XAG Geography"));
#else
    QSKIP("This build does not include Exiv2");
#endif
}

void IoTests::metadataFailureDoesNotReplaceSuccessfulPixelDecode() {
#if ISPVIEW_HAS_EXIV2
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("broken-metadata.jpg"));
    QImage source(8, 8, QImage::Format_RGB32);
    source.fill(Qt::green);
    QVERIFY(source.save(path, "JPG", 95));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QByteArray bytes = file.readAll();
    QVERIFY(bytes.startsWith(QByteArray::fromHex("ffd8")));
    const QByteArray malformedApp1 = QByteArray::fromHex("ffe1000c45786966000042414421");
    bytes.insert(2, malformedApp1);
    QVERIFY(file.resize(0));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();

    QtImageDecoder decoder;
    const DecodeResult result = decoder.decode({path, DecodePurpose::Full, {}});
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.frame->descriptor.size, QSize(8, 8));
    QCOMPARE(result.frame->metadata.metadataReaderName, QStringLiteral("Exiv2"));
    QVERIFY(!result.frame->metadata.metadataWarning.isEmpty());
#else
    QSKIP("This build does not include Exiv2");
#endif
}

void IoTests::colorManagementCapabilityMatchesBuildFeature() {
    QCOMPARE(EncodedColorManagement::isAvailable(), static_cast<bool>(ISPVIEW_HAS_LCMS2));
#if ISPVIEW_HAS_LCMS2
    QVERIFY(!EncodedColorManagement::version().isEmpty());
#else
    QVERIFY(EncodedColorManagement::version().isEmpty());
#endif
    const QString identity = QtImageDecoder().cacheIdentity();
    QVERIFY(identity.contains(QStringLiteral("qt-image-v3")));
    QVERIFY(ImageLoader::cacheKey({QStringLiteral("/tmp/a.png"), DecodePurpose::Thumbnail, {}}) !=
            ImageLoader::cacheKey({QStringLiteral("/tmp/a.png"), DecodePurpose::Thumbnail, {}},
                                  identity));
}

void IoTests::decoderConvertsEmbeddedLinearIccToSrgb() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage source(16, 16, QImage::Format_RGBA8888);
    source.fill(QColor(128, 64, 32, 123));
    const QColorSpace linearSrgb(QColorSpace::SRgbLinear);
    QVERIFY(linearSrgb.isValid());
    QVERIFY(!linearSrgb.iccProfile().isEmpty());
    source.setColorSpace(linearSrgb);
    for (const QByteArray& format : {QByteArray("PNG"), QByteArray("JPG")}) {
        const QString path = directory.filePath(
            QStringLiteral("linear-profile.%1").arg(QString::fromLatin1(format).toLower()));
        QVERIFY2(source.save(path, format.constData(), 100), format.constData());
        QImageReader baselineReader(path);
        baselineReader.setAutoTransform(true);
        QImage baseline = baselineReader.read().convertToFormat(QImage::Format_RGBA8888);
        QVERIFY2(!baseline.isNull(), qPrintable(baselineReader.errorString()));
        QVERIFY(baseline.colorSpace().isValid());

        QtImageDecoder decoder;
        const DecodeResult result = decoder.decode({path, DecodePurpose::Full, {}});
        QVERIFY2(result.succeeded(), qPrintable(result.error));
        QVERIFY(result.frame->metadata.colorProfile.has_value());
        QCOMPARE(result.frame->metadata.colorProfile->sourceFingerprint.size(), 16);
#if ISPVIEW_HAS_LCMS2
        QCOMPARE(result.frame->metadata.colorProfile->renderingIntent,
                 QStringLiteral("Relative colorimetric"));
        QVERIFY2(result.frame->metadata.colorWarning.isEmpty(),
                 qPrintable(result.frame->metadata.colorWarning));
        QVERIFY(result.frame->metadata.colorProfile->converted);
        QCOMPARE(result.frame->metadata.colorProfile->destinationColorSpace,
                 QStringLiteral("sRGB"));
        QVERIFY(result.frame->metadata.colorProfile->transformEngine.startsWith(
            QStringLiteral("LittleCMS ")));
        QCOMPARE(result.frame->descriptor.color.colorSpace, QStringLiteral("sRGB"));
        QCOMPARE(result.frame->descriptor.color.transferFunction, QStringLiteral("sRGB"));
        QCOMPARE(result.frame->qImage()->colorSpace(), QColorSpace(QColorSpace::SRgb));
        const QImage expected =
            baseline.convertedToColorSpace(QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888);
        const QColor actualPixel = result.frame->qImage()->pixelColor(8, 8);
        const QColor expectedPixel = expected.pixelColor(8, 8);
        QVERIFY(std::abs(actualPixel.red() - expectedPixel.red()) <= 2);
        QVERIFY(std::abs(actualPixel.green() - expectedPixel.green()) <= 2);
        QVERIFY(std::abs(actualPixel.blue() - expectedPixel.blue()) <= 2);
        QCOMPARE(actualPixel.alpha(), expectedPixel.alpha());
#else
        QVERIFY(result.frame->metadata.colorProfile->renderingIntent.isEmpty());
        QVERIFY(!result.frame->metadata.colorProfile->converted);
        QCOMPARE(result.frame->metadata.colorProfile->destinationColorSpace,
                 QStringLiteral("Unchanged"));
        QCOMPARE(result.frame->descriptor.color.colorSpace,
                 result.frame->metadata.colorProfile->sourceDescription);
        QCOMPARE(result.frame->descriptor.color.transferFunction, QStringLiteral("ICC"));
        QCOMPARE(result.frame->qImage()->pixelColor(8, 8), baseline.pixelColor(8, 8));
#endif
    }
}

void IoTests::colorManagementRejectsNonRgbProfileWithoutChangingPixels() {
    QImage image(2, 2, QImage::Format_Grayscale8);
    image.fill(64);
    const QColor original = image.pixelColor(0, 0);
    const QColorSpace grayProfile(QPointF(0.3127, 0.3290), QColorSpace::TransferFunction::SRgb);
    QVERIFY(grayProfile.isValid());
    QVERIFY(grayProfile.colorModel() == QColorSpace::ColorModel::Gray);
    QVERIFY(!grayProfile.iccProfile().isEmpty());
    image.setColorSpace(grayProfile);
    QVERIFY(image.colorSpace().isValid());
    ImageMetadata metadata;

    EncodedColorManagement::normalizeToSrgb(image, metadata);
    QVERIFY(metadata.colorProfile.has_value());
    QVERIFY(!metadata.colorProfile->converted);
    QCOMPARE(image.pixelColor(0, 0), original);
#if ISPVIEW_HAS_LCMS2
    QCOMPARE(metadata.colorWarning, QStringLiteral("Only embedded RGB ICC profiles are supported"));
#else
    QVERIFY(metadata.colorWarning.isEmpty());
#endif
}

void IoTests::colorManagementCachedTransformIsThreadSafe() {
    if (!EncodedColorManagement::isAvailable()) {
        QSKIP("LittleCMS is not available in this build");
    }
    const QColorSpace linearSrgb(QColorSpace::SRgbLinear);
    QVERIFY(linearSrgb.isValid());
    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([linearSrgb, &failures] {
            for (int iteration = 0; iteration < 3; ++iteration) {
                QImage image(512, 384, QImage::Format_RGBA8888);
                image.fill(QColor(128, 64, 32, 192));
                image.setColorSpace(linearSrgb);
                ImageMetadata metadata;
                EncodedColorManagement::normalizeToSrgb(image, metadata);
                if (!metadata.colorProfile || !metadata.colorProfile->converted ||
                    !metadata.colorWarning.isEmpty() ||
                    image.pixelColor(0, 0).alpha() != 192) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    QCOMPARE(failures.load(std::memory_order_relaxed), 0);
}

void IoTests::scannerFiltersAndNaturallySortsFiles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imageChild = directory.filePath(QStringLiteral("album2"));
    const QString emptyChild = directory.filePath(QStringLiteral("empty"));
    const QString unsupportedChild = directory.filePath(QStringLiteral("documents"));
    const QString deepImageChild = directory.filePath(QStringLiteral("container/deep-album"));
    QVERIFY(QDir().mkpath(imageChild));
    QVERIFY(QDir().mkpath(emptyChild));
    QVERIFY(QDir().mkpath(unsupportedChild));
    QVERIFY(QDir().mkpath(deepImageChild));
    {
        QFile childImage(QDir(imageChild).filePath(QStringLiteral("inside.png")));
        QVERIFY(childImage.open(QIODevice::WriteOnly));
        childImage.write("fixture");
    }
    {
        QFile deepImage(QDir(deepImageChild).filePath(QStringLiteral("deep.jpg")));
        QVERIFY(deepImage.open(QIODevice::WriteOnly));
        deepImage.write("fixture");
    }
    {
        QFile note(QDir(unsupportedChild).filePath(QStringLiteral("notes.txt")));
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.write("fixture");
    }
    for (const QString& name :
         {QStringLiteral("image10.jpg"), QStringLiteral("image2.JPG"), QStringLiteral("notes.txt"),
          QStringLiteral("image1.png"), QStringLiteral("image3.tiff"),
          QStringLiteral("image4.webp"), QStringLiteral("image5.exr")}) {
        QFile file(directory.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("fixture");
    }

    const auto files = DirectoryScanner::scan(directory.path());
    QStringList expected{QStringLiteral("album2"),     QStringLiteral("container"),
                         QStringLiteral("documents"),  QStringLiteral("empty"),
                         QStringLiteral("image1.png"), QStringLiteral("image2.JPG"),
                         QStringLiteral("image10.jpg")};
    QCOMPARE(files.size(), expected.size());
    for (qsizetype index = 0; index < expected.size(); ++index) {
        QCOMPARE(files.at(index).fileName, expected.at(index));
    }
    QVERIFY(files.constFirst().isDirectory);
    QVERIFY(files.at(1).isDirectory);
    QVERIFY(!files.at(4).isDirectory);
    QVERIFY(
        DirectoryScanner::isSupportedImageFile(directory.filePath(QStringLiteral("image1.png"))));
    QVERIFY(!DirectoryScanner::isSupportedImageFile(
        QDir(unsupportedChild).filePath(QStringLiteral("notes.txt"))));
}

void IoTests::scannerExcludesHiddenFilesAndDirectoryTrees() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage image(4, 3, QImage::Format_RGBA8888);
    image.fill(Qt::blue);
    const QString visiblePath = directory.filePath(QStringLiteral("visible.png"));
    const QString hiddenPath = directory.filePath(QStringLiteral(".hidden.png"));
    const QString hiddenDirectory = directory.filePath(QStringLiteral(".hidden-album"));
    QVERIFY(image.save(visiblePath));
    QVERIFY(image.save(hiddenPath));
    QVERIFY(QDir().mkpath(hiddenDirectory));
    QVERIFY(image.save(QDir(hiddenDirectory).filePath(QStringLiteral("inside.jpg"))));

    const QVector<ImageFileRecord> files = DirectoryScanner::scan(directory.path());
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.constFirst().path, visiblePath);
    QVERIFY(!DirectoryScanner::isSupportedImageFile(hiddenPath));

    const QVector<ImageFileRecord> recursive =
        DirectoryScanner::scanImageFoldersRecursively(directory.path());
    QCOMPARE(recursive.size(), 1);
    QCOMPARE(recursive.constFirst().path, visiblePath);
}

void IoTests::directoryScannerPublishesIncrementalBatches() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (int index = 0; index < 450; ++index) {
        QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("folder-%1").arg(index))));
    }
    DirectoryScanner scanner;
    int batchCount = 0;
    int publishedItems = 0;
    bool finished = false;
    quint64 expectedGeneration = 0;
    connect(&scanner, &DirectoryScanner::scanBatchReady, this,
            [&](const QString&, const QVector<ImageFileRecord>& files, quint64 generation) {
                if (generation != expectedGeneration) return;
                ++batchCount;
                publishedItems += static_cast<int>(files.size());
            });
    connect(&scanner, &DirectoryScanner::scanFinished, this,
            [&](const QString&, const QVector<ImageFileRecord>& files, quint64 generation) {
                if (generation != expectedGeneration) return;
                QCOMPARE(files.size(), 450);
                finished = true;
            });
    expectedGeneration = scanner.scanAsync(directory.path());
    QTRY_VERIFY_WITH_TIMEOUT(finished, 5000);
    QCOMPARE(batchCount, 3);
    QCOMPARE(publishedItems, 450);
}

void IoTests::recursiveImageFolderScanFindsOnlyBranchesContainingImages() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString directFolder = directory.filePath(QStringLiteral("album2"));
    const QString deepFolder = directory.filePath(QStringLiteral("container/album10"));
    const QString emptyFolder = directory.filePath(QStringLiteral("empty/deeper"));
    const QString documentsFolder = directory.filePath(QStringLiteral("documents"));
    QVERIFY(QDir().mkpath(directFolder));
    QVERIFY(QDir().mkpath(deepFolder));
    QVERIFY(QDir().mkpath(emptyFolder));
    QVERIFY(QDir().mkpath(documentsFolder));

    QImage image(4, 3, QImage::Format_RGBA8888);
    image.fill(Qt::green);
    QVERIFY(image.save(directory.filePath(QStringLiteral("root.png"))));
    QVERIFY(image.save(QDir(directFolder).filePath(QStringLiteral("inside.jpg"))));
    QVERIFY(image.save(QDir(deepFolder).filePath(QStringLiteral("deep.png"))));
    QFile note(QDir(documentsFolder).filePath(QStringLiteral("notes.txt")));
    QVERIFY(note.open(QIODevice::WriteOnly));
    note.write("not an image");
    note.close();

    const QVector<ImageFileRecord> files =
        DirectoryScanner::scanImageFoldersRecursively(directory.path());
    QCOMPARE(files.size(), 4);
    QCOMPARE(files.at(0).fileName, QStringLiteral("album2"));
    QCOMPARE(files.at(1).fileName, QStringLiteral("container"));
    QCOMPARE(files.at(2).fileName, QStringLiteral("container/album10"));
    QCOMPARE(files.at(3).fileName, QStringLiteral("root.png"));
    QVERIFY(files.at(0).isDirectory);
    QVERIFY(files.at(1).isDirectory);
    QVERIFY(files.at(2).isDirectory);
    QVERIFY(!files.at(3).isDirectory);
}

void IoTests::decoderRejectsCorruptInput() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("broken.png"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a png");
    file.close();

    QtImageDecoder decoder;
    const DecodeResult result = decoder.decode({path, DecodePurpose::Full, {}});
    QVERIFY(!result.succeeded());
    QVERIFY(!result.error.isEmpty());
}

void IoTests::thumbnailDiskCacheRoundTripsImage() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ThumbnailDiskCache cache(directory.path());
    QImage source(32, 24, QImage::Format_RGBA8888);
    source.fill(QColor(90, 80, 70, 123));

    QVERIFY(cache.store(QStringLiteral("stable-key"), source, QSize(640, 480)));
    const QImage restored = cache.load(QStringLiteral("stable-key"));
    QCOMPARE(restored.size(), source.size());
    QCOMPARE(restored.pixelColor(4, 5), source.pixelColor(4, 5));
    QCOMPARE(restored.text(QStringLiteral("ispview.sourceWidth")), QStringLiteral("640"));
    QCOMPARE(restored.text(QStringLiteral("ispview.sourceHeight")), QStringLiteral("480"));
    QVERIFY(cache.load(QStringLiteral("missing-key")).isNull());
}

void IoTests::imageLoaderDiskCacheKeepsSourceDimensions() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("source-size.png"));
    QImage source(640, 360, QImage::Format_RGBA8888);
    source.fill(Qt::cyan);
    QVERIFY(source.save(path));

    const auto decoder = std::make_shared<QtImageDecoder>();
    QSize firstSourceSize;
    {
        ImageLoader loader(decoder);
        loader.request(1, {path, DecodePurpose::Thumbnail, QSize(160, 120)},
                       [&](quint64, const DecodeResult& result) {
                           QVERIFY(result.frame != nullptr);
                           firstSourceSize = result.frame->metadata.sourceSize;
                       });
        QTRY_COMPARE_WITH_TIMEOUT(firstSourceSize, QSize(640, 360), 5000);
    }

    QSize cachedSourceSize;
    QSize cachedPixelSize;
    {
        ImageLoader loader(decoder);
        loader.request(2, {path, DecodePurpose::Thumbnail, QSize(160, 120)},
                       [&](quint64, const DecodeResult& result) {
                           QVERIFY(result.frame != nullptr);
                           cachedSourceSize = result.frame->metadata.sourceSize;
                           cachedPixelSize = result.frame->descriptor.size;
                       });
        QTRY_COMPARE_WITH_TIMEOUT(cachedSourceSize, QSize(640, 360), 5000);
    }
    QCOMPARE(cachedPixelSize, QSize(160, 90));
}

void IoTests::nv12LimitedRangeProducesReferencePixels() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("frame.yuv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray::fromRawData("\x10\xEB\x51\x91\x80\x80", 6));
    file.close();

    RawImageParameters parameters;
    parameters.size = {2, 2};
    parameters.format = RawPixelFormat::NV12;
    parameters.range = QuantizationRange::Limited;
    parameters.yuvMatrix = YuvMatrix::BT709;
    RawImageDecoder decoder;
    const DecodeResult result = decoder.decode({path, DecodePurpose::Full, {}, parameters});
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QVERIFY(result.frame->qImage() != nullptr);
    const QImage& image = *result.frame->qImage();
    QCOMPARE(image.pixelColor(0, 0), QColor(0, 0, 0, 255));
    QCOMPARE(image.pixelColor(1, 0), QColor(255, 255, 255, 255));
    QCOMPARE(RawImageDecoder::pixelDescription(*result.frame, 0, 0),
             QStringLiteral("YUV(16,128,128)"));
}

void IoTests::mipiRawPackingReturnsExactSensorValues() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString raw10Path = directory.filePath(QStringLiteral("raw10.raw"));
    QFile raw10(raw10Path);
    QVERIFY(raw10.open(QIODevice::WriteOnly));
    raw10.write(QByteArray::fromRawData("\x00\x00\xFF\xFF\xE4", 5));
    raw10.close();

    RawImageParameters raw10Parameters;
    raw10Parameters.size = {4, 1};
    raw10Parameters.format = RawPixelFormat::MipiRaw10;
    RawImageDecoder decoder;
    const DecodeResult raw10Result =
        decoder.decode({raw10Path, DecodePurpose::Full, {}, raw10Parameters});
    QVERIFY2(raw10Result.succeeded(), qPrintable(raw10Result.error));
    QCOMPARE(RawImageDecoder::bayerValueAt(*raw10Result.frame, 0, 0), std::optional<quint16>(0));
    QCOMPARE(RawImageDecoder::bayerValueAt(*raw10Result.frame, 1, 0), std::optional<quint16>(1));
    QCOMPARE(RawImageDecoder::bayerValueAt(*raw10Result.frame, 2, 0), std::optional<quint16>(1022));
    QCOMPARE(RawImageDecoder::bayerValueAt(*raw10Result.frame, 3, 0), std::optional<quint16>(1023));
    QCOMPARE(RawImageDecoder::pixelDescription(*raw10Result.frame, 3, 0),
             QStringLiteral("RAW(1023, Gr)"));

    const QString raw12Path = directory.filePath(QStringLiteral("raw12.raw"));
    QFile raw12(raw12Path);
    QVERIFY(raw12.open(QIODevice::WriteOnly));
    raw12.write(QByteArray::fromRawData("\x12\xAB\xC3", 3));
    raw12.close();
    RawImageParameters raw12Parameters;
    raw12Parameters.size = {2, 1};
    raw12Parameters.format = RawPixelFormat::MipiRaw12;
    const DecodeResult raw12Result =
        decoder.decode({raw12Path, DecodePurpose::Full, {}, raw12Parameters});
    QVERIFY2(raw12Result.succeeded(), qPrintable(raw12Result.error));
    QCOMPARE(RawImageDecoder::bayerValueAt(*raw12Result.frame, 0, 0),
             std::optional<quint16>(0x123));
    QCOMPARE(RawImageDecoder::bayerValueAt(*raw12Result.frame, 1, 0),
             std::optional<quint16>(0xABC));
}

void IoTests::bayerRawDefaultsToMosaicAndDemosaicIsOptIn() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("raw16.raw"));
    QByteArray bytes(8, Qt::Uninitialized);
    qToLittleEndian<quint16>(65535, reinterpret_cast<uchar*>(bytes.data()));
    qToLittleEndian<quint16>(0, reinterpret_cast<uchar*>(bytes.data() + 2));
    qToLittleEndian<quint16>(0, reinterpret_cast<uchar*>(bytes.data() + 4));
    qToLittleEndian<quint16>(0, reinterpret_cast<uchar*>(bytes.data() + 6));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();

    RawImageParameters parameters;
    parameters.size = {2, 2};
    parameters.format = RawPixelFormat::Raw16;
    parameters.displayGamma = 1.0;
    RawImageDecoder decoder;

    const DecodeResult mosaic = decoder.decode({path, DecodePurpose::Preview, {}, parameters});
    QVERIFY2(mosaic.succeeded(), qPrintable(mosaic.error));
    QCOMPARE(mosaic.frame->rawParameters->demosaic, false);
    QCOMPARE(mosaic.frame->qImage()->pixelColor(0, 0), QColor(255, 255, 255, 255));
    QCOMPARE(mosaic.frame->qImage()->pixelColor(1, 0), QColor(0, 0, 0, 255));

    parameters.demosaic = true;
    const DecodeResult demosaiced = decoder.decode({path, DecodePurpose::Preview, {}, parameters});
    QVERIFY2(demosaiced.succeeded(), qPrintable(demosaiced.error));
    const QColor color = demosaiced.frame->qImage()->pixelColor(0, 0);
    QVERIFY(color.red() > color.green());
    QVERIFY(color.red() > color.blue());
}

void IoTests::decoderRegistryRoutesByFormat() {
    ImageDecoderRegistry registry;
    registry.add(std::make_shared<QtImageDecoder>());
    registry.add(std::make_shared<CameraRawDecoder>());
    registry.add(std::make_shared<RawImageDecoder>());
    QVERIFY(registry.canDecode(QStringLiteral("image.PNG")));
    QVERIFY(registry.canDecode(QStringLiteral("capture.yuv")));
    QVERIFY(!registry.canDecode(QStringLiteral("frame.exr")));
    QVERIFY(!registry.canDecode(QStringLiteral("frame.tiff")));
    QVERIFY(!registry.canDecode(QStringLiteral("frame.webp")));
    QVERIFY(!registry.canDecode(QStringLiteral("notes.txt")));
    QVERIFY(registry.cacheIdentity().contains(QStringLiteral("qt-image-v3")));
}

void IoTests::defaultDecoderAndFormatCatalogStayConsistent() {
    const auto decoder = createDefaultImageDecoder();
    QVERIFY(decoder);
    for (const QString& suffix :
         {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
          QStringLiteral("raw"), QStringLiteral("yuv")}) {
        const QString path = QStringLiteral("sample.%1").arg(suffix);
        QVERIFY2(hasSupportedImageSuffix(path), qPrintable(path));
        QVERIFY2(decoder->canDecode(path), qPrintable(path));
    }
    QVERIFY(!hasSupportedImageSuffix(QStringLiteral("sample.tiff")));
    QVERIFY(!decoder->canDecode(QStringLiteral("sample.tiff")));

    for (const QString& suffix : CameraRawDecoder::supportedSuffixes()) {
        const QString path = QStringLiteral("camera.%1").arg(suffix);
        QVERIFY2(hasSupportedImageSuffix(path), qPrintable(path));
        QVERIFY2(decoder->canDecode(path), qPrintable(path));
    }
}

void IoTests::cameraRawCapabilityMatchesBuildFeature() {
    CameraRawDecoder decoder;
#if ISPVIEW_HAS_LIBRAW
    QVERIFY(CameraRawDecoder::isAvailable());
    QVERIFY(decoder.canDecode(QStringLiteral("capture.DNG")));
    QVERIFY(decoder.canDecode(QStringLiteral("capture.CR3")));
    QVERIFY(!decoder.canDecode(QStringLiteral("capture.raw")));
#else
    QVERIFY(!CameraRawDecoder::isAvailable());
    QVERIFY(!decoder.canDecode(QStringLiteral("capture.dng")));
#endif
}

void IoTests::cameraRawDecodesLocalDngWhenAvailable() {
#if ISPVIEW_HAS_LIBRAW
    const QString path = QFINDTESTDATA("../test_images/img.dng");
    if (path.isEmpty()) {
        QSKIP("Optional local test_images/img.dng is not available");
    }
    CameraRawDecoder decoder;
    const DecodeResult thumbnail = decoder.decode({path, DecodePurpose::Thumbnail, QSize(160, 120)});
    QVERIFY2(thumbnail.succeeded(), qPrintable(thumbnail.error));
    QVERIFY(thumbnail.frame->descriptor.size.width() <= 160);
    QVERIFY(thumbnail.frame->descriptor.size.height() <= 120);
    QCOMPARE(thumbnail.frame->metadata.sourceSize, QSize(5464, 3070));
    QVERIFY(thumbnail.frame->rawParameters.has_value());
    QCOMPARE(thumbnail.frame->rawParameters->size, QSize(5464, 3070));
    QCOMPARE(thumbnail.frame->rawParameters->format, RawPixelFormat::Raw16);
    QCOMPARE(thumbnail.frame->rawParameters->validBitsOverride, 12);
    QCOMPARE(thumbnail.frame->rawParameters->bayerPattern, BayerPattern::RGGB);

    const DecodeResult preview = decoder.decode({path, DecodePurpose::Preview, QSize(1920, 1200)});
    QVERIFY2(preview.succeeded(), qPrintable(preview.error));
    QVERIFY(preview.frame->descriptor.size.width() <= 1920);
    QVERIFY(preview.frame->descriptor.size.height() <= 1200);
    QCOMPARE(preview.frame->metadata.format, QStringLiteral("DNG"));
    QCOMPARE(preview.frame->metadata.decoderName, QStringLiteral("LibRaw"));
    QVERIFY(!preview.frame->metadata.decoderVersion.isEmpty());
    QCOMPARE(preview.frame->metadata.sourceSize, QSize(5464, 3070));
    QVERIFY(preview.frame->rawParameters.has_value());
    QVERIFY(preview.frame->metadata.camera.has_value());
    QVERIFY(preview.frame->metadata.camera->sensorSize.isValid());

    const DecodeResult full = decoder.decode({path, DecodePurpose::Full, {}});
    QVERIFY2(full.succeeded(), qPrintable(full.error));
    QVERIFY(full.frame->descriptor.size.width() > preview.frame->descriptor.size.width());
    QVERIFY(full.frame->descriptor.size.height() > preview.frame->descriptor.size.height());
    QCOMPARE(full.frame->metadata.camera->sensorSize, preview.frame->metadata.camera->sensorSize);
#else
    QSKIP("This build does not include LibRaw");
#endif
}

void IoTests::rawFileNameInferenceExtractsCommonParameters() {
    const RawImageParameters yuv =
        RawPresetStore::inferFromFileName(QStringLiteral("scene_3840x2160_p010.yuv"));
    QCOMPARE(yuv.size, QSize(3840, 2160));
    QCOMPARE(yuv.format, RawPixelFormat::P010);
    QVERIFY(yuv.msbAligned);

    const RawImageParameters raw =
        RawPresetStore::inferFromFileName(QStringLiteral("capture_4000_3000_raw12_bggr.raw"));
    QCOMPARE(raw.size, QSize(4000, 3000));
    QCOMPARE(raw.format, RawPixelFormat::MipiRaw12);
    QCOMPARE(raw.bayerPattern, BayerPattern::BGGR);

    const RawImageParameters raw14 =
        RawPresetStore::inferFromFileName(QStringLiteral("capture_6236x4178_raw14_rggb.raw"));
    QCOMPARE(raw14.size, QSize(6236, 4178));
    QCOMPARE(raw14.format, RawPixelFormat::Raw16);
    QCOMPARE(raw14.validBitsOverride, 14);
}

void IoTests::yuvLayoutsProduceEquivalentReferencePixels() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray yPlane = QByteArray::fromRawData("\x80\x80\x80\x80", 4);
    const QList<QPair<RawPixelFormat, QByteArray>> fixtures{
        {RawPixelFormat::NV12, yPlane + QByteArray::fromRawData("\x00\xFF", 2)},
        {RawPixelFormat::NV21, yPlane + QByteArray::fromRawData("\xFF\x00", 2)},
        {RawPixelFormat::I420, yPlane + QByteArray::fromRawData("\x00\xFF", 2)},
    };
    RawImageDecoder decoder;
    QColor reference;
    for (int index = 0; index < fixtures.size(); ++index) {
        const QString path = directory.filePath(QStringLiteral("layout%1.yuv").arg(index));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(fixtures.at(index).second), fixtures.at(index).second.size());
        file.close();
        RawImageParameters parameters;
        parameters.size = {2, 2};
        parameters.format = fixtures.at(index).first;
        parameters.range = QuantizationRange::Full;
        const DecodeResult result = decoder.decode({path, DecodePurpose::Full, {}, parameters});
        QVERIFY2(result.succeeded(), qPrintable(result.error));
        const QColor pixel = result.frame->qImage()->pixelColor(0, 0);
        if (index == 0) {
            reference = pixel;
        } else {
            QCOMPARE(pixel, reference);
        }
        QCOMPARE(RawImageDecoder::pixelDescription(*result.frame, 0, 0),
                 QStringLiteral("YUV(128,0,255)"));
    }
}

void IoTests::p010AndRaw16PreserveHighBitDepthValues() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString p010Path = directory.filePath(QStringLiteral("p010.yuv"));
    QByteArray p010(12, Qt::Uninitialized);
    const quint16 samples[]{64, 940, 64, 940, 512, 512};
    for (int index = 0; index < 6; ++index) {
        qToLittleEndian<quint16>(static_cast<quint16>(samples[index] << 6),
                                 reinterpret_cast<uchar*>(p010.data() + index * 2));
    }
    QFile p010File(p010Path);
    QVERIFY(p010File.open(QIODevice::WriteOnly));
    QCOMPARE(p010File.write(p010), p010.size());
    p010File.close();
    RawImageParameters p010Parameters;
    p010Parameters.size = {2, 2};
    p010Parameters.format = RawPixelFormat::P010;
    p010Parameters.msbAligned = true;
    RawImageDecoder decoder;
    const DecodeResult p010Result =
        decoder.decode({p010Path, DecodePurpose::Full, {}, p010Parameters});
    QVERIFY2(p010Result.succeeded(), qPrintable(p010Result.error));
    QCOMPARE(p010Result.frame->qImage()->pixelColor(0, 0), QColor(0, 0, 0, 255));
    QCOMPARE(p010Result.frame->qImage()->pixelColor(1, 0), QColor(255, 255, 255, 255));
    QCOMPARE(RawImageDecoder::pixelDescription(*p010Result.frame, 1, 0),
             QStringLiteral("YUV(940,512,512)"));

    const QString p010BigEndianPath = directory.filePath(QStringLiteral("p010-be.yuv"));
    QByteArray p010BigEndian(12, Qt::Uninitialized);
    for (int index = 0; index < 6; ++index) {
        qToBigEndian<quint16>(static_cast<quint16>(samples[index] << 6),
                              reinterpret_cast<uchar*>(p010BigEndian.data() + index * 2));
    }
    QFile p010BigEndianFile(p010BigEndianPath);
    QVERIFY(p010BigEndianFile.open(QIODevice::WriteOnly));
    QCOMPARE(p010BigEndianFile.write(p010BigEndian), p010BigEndian.size());
    p010BigEndianFile.close();
    p010Parameters.littleEndian = false;
    const DecodeResult p010BigEndianResult =
        decoder.decode({p010BigEndianPath, DecodePurpose::Full, {}, p010Parameters});
    QVERIFY2(p010BigEndianResult.succeeded(), qPrintable(p010BigEndianResult.error));
    QCOMPARE(*p010BigEndianResult.frame->qImage(), *p010Result.frame->qImage());
    QCOMPARE(RawImageDecoder::pixelDescription(*p010BigEndianResult.frame, 1, 0),
             QStringLiteral("YUV(940,512,512)"));

    const QString raw16Path = directory.filePath(QStringLiteral("raw16.raw"));
    QByteArray raw16(8, Qt::Uninitialized);
    const quint16 rawSamples[]{0, 0x1234, 0xABCD, 0xFFFF};
    for (int index = 0; index < 4; ++index) {
        qToLittleEndian<quint16>(rawSamples[index],
                                 reinterpret_cast<uchar*>(raw16.data() + index * 2));
    }
    QFile raw16File(raw16Path);
    QVERIFY(raw16File.open(QIODevice::WriteOnly));
    QCOMPARE(raw16File.write(raw16), raw16.size());
    raw16File.close();
    RawImageParameters raw16Parameters;
    raw16Parameters.size = {2, 2};
    raw16Parameters.format = RawPixelFormat::Raw16;
    const DecodeResult raw16Result =
        decoder.decode({raw16Path, DecodePurpose::Full, {}, raw16Parameters});
    QVERIFY2(raw16Result.succeeded(), qPrintable(raw16Result.error));
    QCOMPARE(RawImageDecoder::bayerValueAt(*raw16Result.frame, 1, 0),
             std::optional<quint16>(0x1234));
    QCOMPARE(RawImageDecoder::bayerValueAt(*raw16Result.frame, 0, 1),
             std::optional<quint16>(0xABCD));

    RawImageParameters rotatedRaw16Parameters = raw16Parameters;
    rotatedRaw16Parameters.orientation = ImageOrientation::Rotate180;
    const DecodeResult rotatedRaw16 =
        decoder.decode({raw16Path, DecodePurpose::Full, {}, rotatedRaw16Parameters});
    QVERIFY2(rotatedRaw16.succeeded(), qPrintable(rotatedRaw16.error));
    QCOMPARE(rotatedRaw16.frame->qImage()->pixelColor(0, 0),
             raw16Result.frame->qImage()->pixelColor(1, 1));
    QCOMPARE(RawImageDecoder::pixelDescription(*rotatedRaw16.frame, 0, 0),
             QStringLiteral("RAW(65535, B)"));
    QCOMPARE(RawImageDecoder::bayerValueAt(*rotatedRaw16.frame, 0, 0), std::optional<quint16>(0));

    raw16Parameters.validBitsOverride = 14;
    raw16Parameters.whiteLevel = 16383;
    const DecodeResult raw14RightAligned =
        decoder.decode({raw16Path, DecodePurpose::Full, {}, raw16Parameters});
    QVERIFY2(raw14RightAligned.succeeded(), qPrintable(raw14RightAligned.error));
    QCOMPARE(raw14RightAligned.frame->descriptor.storageBits, 16);
    QCOMPARE(raw14RightAligned.frame->descriptor.validBits, 14);
    QCOMPARE(RawImageDecoder::bayerValueAt(*raw14RightAligned.frame, 1, 0),
             std::optional<quint16>(0x1234));
    QCOMPARE(RawImageDecoder::bayerValueAt(*raw14RightAligned.frame, 0, 1),
             std::optional<quint16>(0x2BCD));

    const QString raw14MsbPath = directory.filePath(QStringLiteral("raw14-msb.raw"));
    QByteArray raw14Msb(8, Qt::Uninitialized);
    for (int index = 0; index < 4; ++index) {
        const quint16 value = static_cast<quint16>((rawSamples[index] & 0x3FFF) << 2);
        qToLittleEndian(value, reinterpret_cast<uchar*>(raw14Msb.data() + index * 2));
    }
    QFile raw14MsbFile(raw14MsbPath);
    QVERIFY(raw14MsbFile.open(QIODevice::WriteOnly));
    QCOMPARE(raw14MsbFile.write(raw14Msb), raw14Msb.size());
    raw14MsbFile.close();
    raw16Parameters.msbAligned = true;
    const DecodeResult raw14MsbAligned =
        decoder.decode({raw14MsbPath, DecodePurpose::Full, {}, raw16Parameters});
    QVERIFY2(raw14MsbAligned.succeeded(), qPrintable(raw14MsbAligned.error));
    QCOMPARE(RawImageDecoder::bayerValueAt(*raw14MsbAligned.frame, 1, 0),
             std::optional<quint16>(0x1234));
    QCOMPARE(RawImageDecoder::bayerValueAt(*raw14MsbAligned.frame, 0, 1),
             std::optional<quint16>(0x2BCD));

    raw16Parameters.validBitsOverride = 17;
    const DecodeResult invalidBits =
        decoder.decode({raw16Path, DecodePurpose::Full, {}, raw16Parameters});
    QVERIFY(!invalidBits.succeeded());
}

void IoTests::rawOrientationRotatesDisplayWithoutChangingSourcePlane() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("orientation_3x2_nv12.yuv"));
    const QByteArray bytes =
        QByteArray::fromRawData("\x10\x30\x50\x70\x90\xB0\x80\x80\x80\x80", 10);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();

    RawImageParameters parameters;
    parameters.size = {3, 2};
    parameters.format = RawPixelFormat::NV12;
    parameters.range = QuantizationRange::Full;
    RawImageDecoder decoder;
    const DecodeResult normal = decoder.decode({path, DecodePurpose::Full, {}, parameters});
    QVERIFY2(normal.succeeded(), qPrintable(normal.error));

    parameters.orientation = ImageOrientation::Rotate90Clockwise;
    const DecodeResult rotated = decoder.decode({path, DecodePurpose::Full, {}, parameters});
    QVERIFY2(rotated.succeeded(), qPrintable(rotated.error));
    QCOMPARE(rotated.frame->descriptor.size, QSize(2, 3));
    QCOMPARE(rotated.frame->qImage()->size(), QSize(2, 3));
    for (int y = 0; y < rotated.frame->qImage()->height(); ++y) {
        for (int x = 0; x < rotated.frame->qImage()->width(); ++x) {
            const QPoint source =
                displayToSourcePixel({x, y}, parameters.size, parameters.orientation);
            QCOMPARE(rotated.frame->qImage()->pixelColor(x, y),
                     normal.frame->qImage()->pixelColor(source));
        }
    }
    QCOMPARE(RawImageDecoder::pixelDescription(*rotated.frame, 0, 0),
             QStringLiteral("YUV(112,128,128)"));
    const auto* storage =
        std::get_if<std::shared_ptr<const PlaneBufferSet>>(&rotated.frame->storage);
    QVERIFY(storage != nullptr && *storage != nullptr);
    QCOMPARE((*storage)->storage, bytes);

    const DecodeResult preview =
        decoder.decode({path, DecodePurpose::Preview, QSize(2, 2), parameters});
    QVERIFY2(preview.succeeded(), qPrintable(preview.error));
    QCOMPARE(preview.frame->descriptor.size, QSize(1, 2));
    QCOMPARE(preview.frame->qImage()->size(), QSize(1, 2));

    parameters.orientation = static_cast<ImageOrientation>(999);
    const DecodeResult invalid = decoder.decode({path, DecodePurpose::Full, {}, parameters});
    QVERIFY(!invalid.succeeded());
}

void IoTests::rawDecoderRejectsTruncatedFramesAndSelectsFrameIndex() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("sequence.yuv"));
    const QByteArray black = QByteArray::fromRawData("\x10\x10\x10\x10\x80\x80", 6);
    const QByteArray white = QByteArray::fromRawData("\xEB\xEB\xEB\xEB\x80\x80", 6);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(black + white), 12);
    file.close();
    RawImageParameters parameters;
    parameters.size = {2, 2};
    parameters.format = RawPixelFormat::NV12;
    parameters.frameIndex = 1;
    RawImageDecoder decoder;
    const DecodeResult second = decoder.decode({path, DecodePurpose::Full, {}, parameters});
    QVERIFY2(second.succeeded(), qPrintable(second.error));
    QCOMPARE(second.frame->qImage()->pixelColor(0, 0), QColor(255, 255, 255, 255));
    QCOMPARE(availableFrameCount(12, parameters), 2);

    parameters.frameIndex = 2;
    const DecodeResult beyondEnd = decoder.decode({path, DecodePurpose::Full, {}, parameters});
    QVERIFY(!beyondEnd.succeeded());

    const QString truncatedPath = directory.filePath(QStringLiteral("truncated.yuv"));
    QFile truncated(truncatedPath);
    QVERIFY(truncated.open(QIODevice::WriteOnly));
    QCOMPARE(truncated.write(black.first(5)), 5);
    truncated.close();
    parameters.frameIndex = 0;
    const DecodeResult truncatedResult =
        decoder.decode({truncatedPath, DecodePurpose::Full, {}, parameters});
    QVERIFY(!truncatedResult.succeeded());
}

void IoTests::rawSidecarRoundTripsParameters() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("capture.raw"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(64, '\0'));
    file.close();
    RawImageParameters parameters;
    parameters.size = {4, 4};
    parameters.format = RawPixelFormat::Raw16;
    parameters.validBitsOverride = 14;
    parameters.frameIndex = 1;
    parameters.bayerPattern = BayerPattern::BGGR;
    parameters.blackLevel = 64;
    parameters.whiteLevel = 4095;
    parameters.whiteBalanceGains = {2.0, 1.0, 1.5};
    parameters.colorCorrectionMatrix = {1.1, -0.1, 0.0, 0.0, 1.0, 0.0, 0.0, -0.2, 1.2};
    parameters.displayGamma = 2.4;
    parameters.orientation = ImageOrientation::Rotate180;
    QString error;
    QVERIFY2(RawPresetStore::saveSidecar(path, parameters, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(RawPresetStore::sidecarPath(path)));
    const auto restored = RawPresetStore::loadForFile(path);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->cacheKey(), parameters.cacheKey());

    QFile sidecar(RawPresetStore::sidecarPath(path));
    QVERIFY(sidecar.open(QIODevice::ReadOnly));
    QJsonObject legacy = QJsonDocument::fromJson(sidecar.readAll()).object();
    sidecar.close();
    for (const QString& key :
         {QStringLiteral("validBits"), QStringLiteral("orientation"), QStringLiteral("wbRed"),
          QStringLiteral("wbGreen"), QStringLiteral("wbBlue"), QStringLiteral("displayGamma")}) {
        legacy.remove(key);
    }
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            legacy.remove(QStringLiteral("ccm%1%2").arg(row).arg(column));
        }
    }
    QVERIFY(sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(sidecar.write(QJsonDocument(legacy).toJson()) > 0);
    sidecar.close();
    const auto restoredLegacy = RawPresetStore::loadForFile(path);
    QVERIFY(restoredLegacy.has_value());
    QCOMPARE(restoredLegacy->validBitsOverride, 0);
    QCOMPARE(restoredLegacy->validBits(), 16);
    const std::array<double, 3> identityGains{1.0, 1.0, 1.0};
    const std::array<double, 9> identityMatrix{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    QCOMPARE(restoredLegacy->whiteBalanceGains, identityGains);
    QCOMPARE(restoredLegacy->colorCorrectionMatrix, identityMatrix);
    QCOMPARE(restoredLegacy->displayGamma, 2.2);
    QCOMPARE(restoredLegacy->orientation, ImageOrientation::Normal);
}

void IoTests::rawPreviewIsBoundedAndDoesNotRetainFullPlanes() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("preview_8x4_nv12.yuv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArray(48, static_cast<char>(0x80))), 48);
    file.close();

    RawImageParameters parameters;
    parameters.size = {8, 4};
    parameters.format = RawPixelFormat::NV12;
    RawImageDecoder decoder;
    const DecodeResult preview =
        decoder.decode({path, DecodePurpose::Preview, QSize(4, 4), parameters});
    QVERIFY2(preview.succeeded(), qPrintable(preview.error));
    QCOMPARE(preview.frame->descriptor.size, QSize(4, 2));
    QVERIFY(std::holds_alternative<QImage>(preview.frame->storage));
    QCOMPARE(preview.frame->rawParameters->size, QSize(8, 4));

    const DecodeResult full = decoder.decode({path, DecodePurpose::Full, {}, parameters});
    QVERIFY2(full.succeeded(), qPrintable(full.error));
    QVERIFY(std::holds_alternative<std::shared_ptr<const PlaneBufferSet>>(full.frame->storage));
}

void IoTests::rawPreviewDirectlySamplesYuvAndBayerSources() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString yuvPath = directory.filePath(QStringLiteral("gradient_nv12.yuv"));
    const QByteArray yRow = QByteArray::fromRawData("\x00\x40\x80\xFF", 4);
    QFile yuvFile(yuvPath);
    QVERIFY(yuvFile.open(QIODevice::WriteOnly));
    QCOMPARE(yuvFile.write(yRow + yRow + QByteArray(4, static_cast<char>(0x80))), 12);
    yuvFile.close();
    RawImageParameters yuv;
    yuv.size = {4, 2};
    yuv.format = RawPixelFormat::NV12;
    yuv.range = QuantizationRange::Full;
    RawImageDecoder decoder;
    const DecodeResult yuvPreview =
        decoder.decode({yuvPath, DecodePurpose::Preview, QSize(2, 1), yuv});
    QVERIFY2(yuvPreview.succeeded(), qPrintable(yuvPreview.error));
    QCOMPARE(yuvPreview.frame->descriptor.size, QSize(2, 1));
    QCOMPARE(yuvPreview.frame->qImage()->pixelColor(0, 0), QColor(32, 32, 32, 255));
    QCOMPARE(yuvPreview.frame->qImage()->pixelColor(1, 0), QColor(192, 192, 192, 255));

    const QString rawPath = directory.filePath(QStringLiteral("white_raw16.raw"));
    QFile rawFile(rawPath);
    QVERIFY(rawFile.open(QIODevice::WriteOnly));
    QCOMPARE(rawFile.write(QByteArray(32, static_cast<char>(0xFF))), 32);
    rawFile.close();
    RawImageParameters raw;
    raw.size = {4, 4};
    raw.format = RawPixelFormat::Raw16;
    const DecodeResult rawPreview =
        decoder.decode({rawPath, DecodePurpose::Preview, QSize(2, 2), raw});
    QVERIFY2(rawPreview.succeeded(), qPrintable(rawPreview.error));
    QCOMPARE(rawPreview.frame->descriptor.size, QSize(2, 2));
    QCOMPARE(rawPreview.frame->qImage()->pixelColor(0, 0), QColor(255, 255, 255, 255));
}

void IoTests::fullRawFrameUsesBoundedFallbackWithoutChangingLogicalSize() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("large_raw16.raw"));
    RawImageParameters parameters;
    parameters.size = {4000, 3000};
    parameters.format = RawPixelFormat::Raw16;
    parameters.validBitsOverride = 14;
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(frameByteSize(parameters)));
    file.close();

    RawImageDecoder decoder;
    const DecodeResult result = decoder.decode({path, DecodePurpose::Full, {}, parameters});
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.frame->descriptor.size, parameters.size);
    QVERIFY(result.frame->qImage() != nullptr);
    QCOMPARE(result.frame->qImage()->size(), QSize(960, 720));
    QVERIFY(std::holds_alternative<std::shared_ptr<const PlaneBufferSet>>(result.frame->storage));
    QCOMPARE(RawImageDecoder::bayerValueAt(*result.frame, 3999, 2999), std::optional<quint16>(0));
}

void IoTests::bayerDisplayTransformAppliesWhiteBalanceCcmAndGamma() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("color_raw16.raw"));
    QByteArray bytes(4 * 4 * 2, Qt::Uninitialized);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const bool red = (x % 2 == 0) && (y % 2 == 0);
            const bool blue = (x % 2 == 1) && (y % 2 == 1);
            const quint16 value = red ? 16384 : (blue ? 49151 : 32768);
            qToLittleEndian(value, reinterpret_cast<uchar*>(bytes.data() + (y * 4 + x) * 2));
        }
    }
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();

    RawImageParameters parameters;
    parameters.size = {4, 4};
    parameters.format = RawPixelFormat::Raw16;
    parameters.bayerPattern = BayerPattern::RGGB;
    parameters.demosaic = true;
    parameters.whiteBalanceGains = {2.0, 1.0, 0.5};
    parameters.colorCorrectionMatrix = {0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0};
    parameters.displayGamma = 1.0;
    RawImageDecoder decoder;
    auto result = decoder.decode({path, DecodePurpose::Full, {}, parameters});
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.frame->qImage()->pixelColor(1, 1), QColor(96, 128, 128, 255));
    QCOMPARE(RawImageDecoder::bayerValueAt(*result.frame, 0, 0), std::optional<quint16>(16384));

    parameters.displayGamma = 2.0;
    result = decoder.decode({path, DecodePurpose::Preview, QSize(4, 4), parameters});
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(result.frame->qImage()->pixelColor(1, 1), QColor(156, 180, 180, 255));

    parameters.displayGamma = 0.0;
    result = decoder.decode({path, DecodePurpose::Full, {}, parameters});
    QVERIFY(!result.succeeded());
}

void IoTests::imageLoaderPrefetchesOnlyAdjacentRawFrames() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("three_frames.yuv"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArray(18, '\0')), 18);
    file.close();

    auto decoder = std::make_shared<PrefetchRecordingDecoder>();
    ImageLoader loader(decoder);
    RawImageParameters parameters;
    parameters.size = {2, 2};
    parameters.format = RawPixelFormat::NV12;
    parameters.frameIndex = 1;
    loader.prefetchAdjacentRawFrames(path, parameters, {320, 240});

    const quint32 expected =
        prefetchCallBit(0, DecodePurpose::Preview) | prefetchCallBit(0, DecodePurpose::Full) |
        prefetchCallBit(2, DecodePurpose::Preview) | prefetchCallBit(2, DecodePurpose::Full);
    QTRY_COMPARE_WITH_TIMEOUT(decoder->calls.load(std::memory_order_relaxed), expected, 2000);
    const quint32 currentFrameMask = prefetchCallBit(1, DecodePurpose::Thumbnail) |
                                     prefetchCallBit(1, DecodePurpose::Preview) |
                                     prefetchCallBit(1, DecodePurpose::Full);
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed) & currentFrameMask, 0U);
}

void IoTests::imageLoaderSkipsFullPrefetchWhenFramesExceedBudget() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("three_12mp_frames.yuv"));
    RawImageParameters parameters;
    parameters.size = {4000, 3000};
    parameters.format = RawPixelFormat::P010;
    parameters.frameIndex = 1;
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(frameByteSize(parameters) * 3));
    file.close();

    auto decoder = std::make_shared<PrefetchRecordingDecoder>();
    ImageLoader loader(decoder);
    loader.setMemoryBudget(64LL * 1024LL * 1024LL);
    loader.prefetchAdjacentRawFrames(path, parameters, {960, 720});
    const quint32 expected =
        prefetchCallBit(0, DecodePurpose::Preview) | prefetchCallBit(2, DecodePurpose::Preview);
    QTRY_COMPARE_WITH_TIMEOUT(decoder->calls.load(std::memory_order_relaxed), expected, 2000);
    const quint32 fullMask =
        prefetchCallBit(0, DecodePurpose::Full) | prefetchCallBit(2, DecodePurpose::Full);
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed) & fullMask, 0U);
}

void IoTests::namedRawPresetsRoundTripOverwriteAndDelete() {
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("ISPViewTests"));
    QCoreApplication::setApplicationName(QStringLiteral("IoPresetTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    const QString name = QStringLiteral("io-test-p010-4k");
    (void)RawPresetStore::removeNamedPreset(name);
    RawImageParameters parameters;
    parameters.size = {3840, 2160};
    parameters.format = RawPixelFormat::P010;
    parameters.rowStride = 8192;
    parameters.chromaStride = 8192;
    parameters.frameIndex = 7;
    parameters.msbAligned = true;
    parameters.whiteBalanceGains = {1.75, 1.0, 1.25};
    parameters.colorCorrectionMatrix[1] = -0.125;
    parameters.displayGamma = 2.4;
    parameters.orientation = ImageOrientation::Rotate180;
    QVERIFY(RawPresetStore::saveNamedPreset(name, parameters));
    QVERIFY(RawPresetStore::namedPresetNames().contains(name));
    auto restored = RawPresetStore::loadNamedPreset(name);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->size, QSize(3840, 2160));
    QCOMPARE(restored->format, RawPixelFormat::P010);
    QCOMPARE(restored->frameIndex, 0);
    QVERIFY(restored->msbAligned);
    QCOMPARE(restored->whiteBalanceGains, parameters.whiteBalanceGains);
    QCOMPARE(restored->colorCorrectionMatrix, parameters.colorCorrectionMatrix);
    QCOMPARE(restored->displayGamma, 2.4);
    QCOMPARE(restored->orientation, ImageOrientation::Rotate180);

    parameters.size = {1920, 1080};
    parameters.format = RawPixelFormat::NV21;
    parameters.rowStride = 0;
    parameters.chromaStride = 0;
    QVERIFY(RawPresetStore::saveNamedPreset(name, parameters));
    restored = RawPresetStore::loadNamedPreset(name);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->size, QSize(1920, 1080));
    QCOMPARE(restored->format, RawPixelFormat::NV21);

    RawImageParameters invalid = parameters;
    invalid.yuvMatrix = static_cast<YuvMatrix>(999);
    QVERIFY(!RawPresetStore::saveNamedPreset(QStringLiteral("invalid"), invalid));
    invalid = parameters;
    invalid.displayGamma = 0.0;
    QVERIFY(!RawPresetStore::saveNamedPreset(QStringLiteral("invalid-gamma"), invalid));
    invalid = parameters;
    invalid.orientation = static_cast<ImageOrientation>(999);
    QVERIFY(!RawPresetStore::saveNamedPreset(QStringLiteral("invalid-orientation"), invalid));
    invalid = parameters;
    invalid.validBitsOverride = 14;
    QVERIFY(!RawPresetStore::saveNamedPreset(QStringLiteral("invalid-yuv-bits"), invalid));
    invalid.format = RawPixelFormat::Raw16;
    invalid.validBitsOverride = 17;
    invalid.rowStride = 0;
    invalid.chromaStride = 0;
    QVERIFY(!RawPresetStore::saveNamedPreset(QStringLiteral("invalid-raw-bits"), invalid));
    QVERIFY(!RawPresetStore::saveNamedPreset(QString{}, parameters));
    QVERIFY(RawPresetStore::removeNamedPreset(name));
    QVERIFY(!RawPresetStore::loadNamedPreset(name).has_value());
    QVERIFY(!RawPresetStore::removeNamedPreset(name));
}

void IoTests::filenameRulesApplyOrderedPresetAndCapturedOverrides() {
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("ISPViewTests"));
    QCoreApplication::setApplicationName(QStringLiteral("IoFilenameRuleTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());

    const QString firstPreset = QStringLiteral("rule-first-nv21");
    const QString secondPreset = QStringLiteral("rule-second-p010");
    RawImageParameters first;
    first.size = {640, 480};
    first.format = RawPixelFormat::NV21;
    RawImageParameters second = first;
    second.format = RawPixelFormat::P010;
    second.msbAligned = true;
    QVERIFY(RawPresetStore::saveNamedPreset(firstPreset, first));
    QVERIFY(RawPresetStore::saveNamedPreset(secondPreset, second));

    const QString pattern =
        QStringLiteral(R"((?i)scene_(?<width>\d+)x(?<height>\d+)_ys(?<row_stride>\d+))") +
        QStringLiteral(R"(_cs(?<chroma_stride>\d+)_o(?<offset>\d+)_f(?<frame>\d+)\.yuv)");
    QVector<RawFilenameRule> rules{{QStringLiteral("First"), pattern, firstPreset, true},
                                   {QStringLiteral("Second"), pattern, secondPreset, true}};
    QVERIFY(RawPresetStore::saveFilenameRules(rules));
    QCOMPARE(RawPresetStore::filenameRules().size(), 2);

    const QString path = QStringLiteral("/tmp/SCENE_1920x1080_ys4096_cs4096_o64_f3.YUV");
    auto inferred = RawPresetStore::inferFromFileName(path);
    QCOMPARE(inferred.format, RawPixelFormat::NV21);
    QCOMPARE(inferred.size, QSize(1920, 1080));
    QCOMPARE(inferred.rowStride, 4096);
    QCOMPARE(inferred.chromaStride, 4096);
    QCOMPARE(inferred.headerOffset, 64);
    QCOMPARE(inferred.frameIndex, 3);

    rules[0].enabled = false;
    QVERIFY(RawPresetStore::saveFilenameRules(rules));
    inferred = RawPresetStore::inferFromFileName(path);
    QCOMPARE(inferred.format, RawPixelFormat::P010);

    // Rules are applied to the entire filename, so a trailing suffix must not match.
    const auto partial = RawPresetStore::inferFromFileName(path + QStringLiteral(".bak"));
    QCOMPARE(partial.format, RawPixelFormat::NV12);
    QCOMPARE(partial.rowStride, 0);

    QVERIFY(!RawPresetStore::saveFilenameRules(
        {{QStringLiteral("Broken"), QStringLiteral("["), firstPreset, true}}));
    QVERIFY(!RawPresetStore::saveFilenameRules(
        {{QStringLiteral("Duplicate"), QStringLiteral(".*"), firstPreset, true},
         {QStringLiteral("duplicate"), QStringLiteral(".*"), secondPreset, true}}));
    QVERIFY(
        !RawPresetStore::saveFilenameRules({{QStringLiteral("Missing preset"), QStringLiteral(".*"),
                                             QStringLiteral("does-not-exist"), true}}));

    QVERIFY(RawPresetStore::saveFilenameRules({}));
    const auto fallback =
        RawPresetStore::inferFromFileName(QStringLiteral("capture_1280x720_nv21.yuv"));
    QCOMPARE(fallback.size, QSize(1280, 720));
    QCOMPARE(fallback.format, RawPixelFormat::NV21);

    const QString rawPreset = QStringLiteral("rule-raw16-container");
    RawImageParameters raw16;
    raw16.size = {4, 4};
    raw16.format = RawPixelFormat::Raw16;
    QVERIFY(RawPresetStore::saveNamedPreset(rawPreset, raw16));
    const QString rawPattern =
        QStringLiteral(R"(capture_(?<width>\d+)x(?<height>\d+)_raw(?<valid_bits>\d+)\.raw)");
    QVERIFY(RawPresetStore::saveFilenameRules(
        {{QStringLiteral("RAW container"), rawPattern, rawPreset, true}}));
    const auto raw14 =
        RawPresetStore::inferFromFileName(QStringLiteral("/tmp/capture_6236x4178_raw14.raw"));
    QCOMPARE(raw14.format, RawPixelFormat::Raw16);
    QCOMPARE(raw14.size, QSize(6236, 4178));
    QCOMPARE(raw14.validBitsOverride, 14);
    QVERIFY(RawPresetStore::saveFilenameRules({}));
    QVERIFY(RawPresetStore::removeNamedPreset(rawPreset));
    QVERIFY(RawPresetStore::removeNamedPreset(firstPreset));
    QVERIFY(RawPresetStore::removeNamedPreset(secondPreset));
}

void IoTests::imageLoaderCoalescesIdenticalInFlightRequests() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("coalesce.png"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("fixture"), 7);
    file.close();

    auto decoder = std::make_shared<DelayedCountingDecoder>();
    ImageLoader loader(decoder);
    const DecodeRequest request(path, DecodePurpose::Full, {});
    QVector<quint64> completed;
    const auto callback = [&completed](quint64 requestId, const DecodeResult& result) {
        QVERIFY(result.succeeded());
        completed.push_back(requestId);
    };
    loader.request(10, request, callback);
    loader.request(20, request, callback);
    QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 2000);
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed), 1);
    QCOMPARE(completed, QVector<quint64>({10, 20}));
    QVERIFY(loader.isCached(request));

    bool cacheHitCalledSynchronously = false;
    loader.request(30, request, [&](quint64 requestId, const DecodeResult& result) {
        QCOMPARE(requestId, 30);
        QVERIFY(result.succeeded());
        cacheHitCalledSynchronously = true;
    });
    QVERIFY(cacheHitCalledSynchronously);
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed), 1);
}

void IoTests::imageLoaderCancellationSuppressesStaleCallbacks() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("cancel.png"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("fixture"), 7);
    file.close();

    auto decoder = std::make_shared<DelayedCountingDecoder>();
    ImageLoader loader(decoder);
    int cancelledCallbacks = 0;
    int liveCallbacks = 0;
    LoadHandle cancelled = loader.request(
        1, {path, DecodePurpose::Full, {}},
        [&cancelledCallbacks](quint64, const DecodeResult&) { ++cancelledCallbacks; });
    loader.request(2, {path, DecodePurpose::Full, {}},
                   [&liveCallbacks](quint64, const DecodeResult& result) {
                       QVERIFY(result.succeeded());
                       ++liveCallbacks;
                   });
    cancelled.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(liveCallbacks, 1, 2000);
    QCOMPARE(cancelledCallbacks, 0);
    QCOMPARE(decoder->calls.load(std::memory_order_relaxed), 1);
}

void IoTests::imageLoaderSerializedQueuePrioritizesAndCancelsWithoutBlockingParallelWork() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto createFixture = [&directory](const QString& name) {
        const QString path = directory.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write("fixture") != 7) {
            return QString{};
        }
        return path;
    };
    const QString first = createFixture(QStringLiteral("first.raw"));
    const QString cancelledPath = createFixture(QStringLiteral("cancelled.raw"));
    const QString interactive = createFixture(QStringLiteral("interactive.raw"));
    const QString parallel = createFixture(QStringLiteral("parallel.png"));
    QVERIFY(!first.isEmpty() && !cancelledPath.isEmpty() && !interactive.isEmpty() &&
            !parallel.isEmpty());

    auto decoder = std::make_shared<SerializedRecordingDecoder>();
    ImageLoader loader(decoder);
    int completed = 0;
    loader.request(1, {first, DecodePurpose::Full, {}},
                   [&completed](quint64, const DecodeResult& result) {
                       QVERIFY(result.succeeded());
                       ++completed;
                   },
                   RequestOptions{LoadCategory::Background, 0,
                                  QStringLiteral("serialized-first")});
    QTRY_COMPARE_WITH_TIMEOUT(decoder->recordedOrder().size(), 1, 1000);
    LoadHandle cancelled = loader.request(
        2, {cancelledPath, DecodePurpose::Full, {}},
        [](quint64, const DecodeResult&) { QFAIL("Cancelled queued decode completed"); },
        RequestOptions{LoadCategory::Background, 0, QStringLiteral("serialized-cancelled")});
    loader.request(3, {interactive, DecodePurpose::Full, {}},
                   [&completed](quint64, const DecodeResult& result) {
                       QVERIFY(result.succeeded());
                       ++completed;
                   },
                   RequestOptions{LoadCategory::Interactive, 0,
                                  QStringLiteral("serialized-interactive")});
    bool parallelCompleted = false;
    loader.request(4, {parallel, DecodePurpose::Full, {}},
                   [&parallelCompleted](quint64, const DecodeResult& result) {
                       QVERIFY(result.succeeded());
                       parallelCompleted = true;
                   },
                   RequestOptions{LoadCategory::Interactive, 0,
                                  QStringLiteral("parallel-during-serialized")});
    cancelled.cancel();

    QTRY_VERIFY_WITH_TIMEOUT(parallelCompleted, 1000);
    QTRY_COMPARE_WITH_TIMEOUT(completed, 2, 3000);
    QCOMPARE(decoder->recordedOrder(),
             QStringList({QStringLiteral("first.raw"), QStringLiteral("interactive.raw")}));
    QCOMPARE(decoder->maximumSerializedActive.load(std::memory_order_relaxed), 1);
}

void IoTests::imageLoaderEnforcesCombinedMemoryBudget() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto decoder = std::make_shared<DelayedCountingDecoder>();
    ImageLoader loader(decoder);
    loader.setMemoryBudget(100);

    int completed = 0;
    for (int index = 0; index < 3; ++index) {
        const QString path = directory.filePath(QStringLiteral("%1.png").arg(index));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"), 7);
        file.close();
        loader.request(static_cast<quint64>(index),
                       {path, static_cast<DecodePurpose>(index), {}},
                       [&completed](quint64, const DecodeResult& result) {
                           QVERIFY(result.succeeded());
                           ++completed;
                       });
    }
    QTRY_COMPARE_WITH_TIMEOUT(completed, 3, 3000);
    QVERIFY(loader.cachedBytes() <= loader.memoryBudget());

    auto small = std::make_shared<ImageFrame>();
    small->metadata.sourceSize = QSize(2000, 1500);
    small->descriptor.storageBits = 8;
    auto large = std::make_shared<ImageFrame>();
    large->metadata.sourceSize = QSize(12000, 10000);
    large->descriptor.storageBits = 8;
    ImageLoader defaultBudgetLoader(decoder);
    QVERIFY(defaultBudgetLoader.canAutomaticallyLoadFull({small, small, small, small}));
    QVERIFY(!defaultBudgetLoader.canAutomaticallyLoadFull({large}));
    defaultBudgetLoader.setMemoryBudget(8LL * 1024LL * 1024LL);
    QVERIFY(!defaultBudgetLoader.canAutomaticallyLoadFull({small}));
    defaultBudgetLoader.setMemoryBudget(384LL * 1024LL * 1024LL);
    QVERIFY(defaultBudgetLoader.canAutomaticallyLoadFull({small}));
}

void IoTests::imageTransformerRotatesResizesAndRestoresEncodedImage() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("editable.png"));
    QImage original(3, 2, QImage::Format_RGBA8888);
    original.fill(Qt::transparent);
    original.setPixelColor(0, 0, Qt::red);
    original.setPixelColor(2, 1, Qt::blue);
    QVERIFY(original.save(path));
    QFile originalFile(path);
    QVERIFY(originalFile.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = originalFile.readAll();
    originalFile.close();

    QCOMPARE(ImageTransformer::rotate(path, QuarterTurn::Clockwise), QString{});
    QVERIFY(ImageTransformer::canRestore(path));
    QCOMPARE(QImageReader(path).size(), QSize(2, 3));
    QCOMPARE(ImageTransformer::resize(path, QSize(8, 6)), QString{});
    QCOMPARE(QImageReader(path).size(), QSize(8, 6));

    QCOMPARE(ImageTransformer::restore(path), QString{});
    QVERIFY(!ImageTransformer::canRestore(path));
    QFile restored(path);
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), originalBytes);
}

void IoTests::imageTransformerRotatesAndRestoresNv12Data() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("frame_4x2_nv12.yuv"));
    const QByteArray original = QByteArray::fromRawData(
        "\x01\x02\x03\x04\x05\x06\x07\x08\x0a\x1e\x14\x28", 12);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(original), original.size());
    file.close();
    RawImageParameters parameters;
    parameters.size = {4, 2};
    parameters.format = RawPixelFormat::NV12;

    QCOMPARE(ImageTransformer::rotate(path, QuarterTurn::Clockwise, parameters), QString{});
    const auto rotatedParameters = RawPresetStore::loadForFile(path);
    QVERIFY(rotatedParameters.has_value());
    QCOMPARE(rotatedParameters->size, QSize(2, 4));
    QFile rotated(path);
    QVERIFY(rotated.open(QIODevice::ReadOnly));
    const QByteArray expected = QByteArray::fromRawData(
        "\x05\x01\x06\x02\x07\x03\x08\x04\x0a\x1e\x14\x28", 12);
    QCOMPARE(rotated.readAll(), expected);
    rotated.close();

    QCOMPARE(ImageTransformer::restore(path), QString{});
    QFile restored(path);
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), original);
    const auto restoredParameters = RawPresetStore::loadForFile(path);
    QVERIFY(restoredParameters.has_value());
    QCOMPARE(restoredParameters->size, QSize(4, 2));
}

} // namespace ispview

QTEST_GUILESS_MAIN(ispview::IoTests)
#include "io_tests.moc"
