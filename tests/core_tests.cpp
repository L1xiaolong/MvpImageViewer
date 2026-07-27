#include "core/comparison_pixel_probe.h"
#include "core/display_histogram.h"
#include "core/raw_image_parameters.h"
#include "core/raw_plane_access.h"
#include "core/raw_plane_histogram.h"
#include "core/sync_group.h"
#include "core/view_state.h"
#include "core/weighted_lru_cache.h"

#include <QTest>
#include <QtEndian>

#include <cmath>
#include <limits>

namespace ispview {

class CoreTests final : public QObject {
    Q_OBJECT

  private slots:
    void fitScalePreservesAspectRatio();
    void cursorAnchoredZoomPreservesImagePoint();
    void panUsesImageScale();
    void visibleNormalizedRectTracksZoomAndEdges();
    void synchronizationCanBeConfigured();
    void weightedCacheEvictsLeastRecentlyUsed();
    void rawFrameSizesRespectFormatAndStride();
    void rawFrameSizeRejectsInvalidGeometry();
    void rawFrameCountAndOddChromaStrideAreSafe();
    void rawDisplayTransformValidationAndCacheIdentity();
    void rawOrientationMapsCoordinatesAndCacheIdentity();
    void displayHistogramComputesChannelsAndBoundedSampling();
    void displayHistogramRestrictsNormalizedRegion();
    void rawPlaneAccessorAndHistogramPreserveEngineeringSamples();
    void comparisonPixelProbeMapsDifferentSizesAndRawOrientation();
};

void CoreTests::fitScalePreservesAspectRatio() {
    QCOMPARE(ViewTransform::fitScale({4000, 3000}, {1000, 1000}), 0.25);
    QCOMPARE(ViewTransform::fitScale({1000, 2000}, {1000, 500}), 0.25);
}

void CoreTests::visibleNormalizedRectTracksZoomAndEdges() {
    ViewState state;
    state.fitMode = FitMode::Manual;
    state.pixelsPerImagePixel = 2.0;
    state.normalizedCenter = {0.5, 0.5};

    const QRectF centered = ViewTransform::visibleNormalizedRect({400, 300}, {400, 300}, state);
    QCOMPARE(centered, QRectF(0.25, 0.25, 0.5, 0.5));

    state.normalizedCenter = {0.0, 0.0};
    const QRectF clipped = ViewTransform::visibleNormalizedRect({400, 300}, {400, 300}, state);
    QCOMPARE(clipped, QRectF(0.0, 0.0, 0.25, 0.25));

    state.pixelsPerImagePixel = 0.5;
    state.normalizedCenter = {0.5, 0.5};
    QCOMPARE(ViewTransform::visibleNormalizedRect({400, 300}, {400, 300}, state),
             QRectF(0.0, 0.0, 1.0, 1.0));
    QVERIFY(ViewTransform::visibleNormalizedRect({}, {400, 300}, state).isEmpty());
}

void CoreTests::comparisonPixelProbeMapsDifferentSizesAndRawOrientation() {
    QImage encodedImage(8, 4, QImage::Format_RGBA8888);
    encodedImage.fill(Qt::black);
    encodedImage.setPixelColor(3, 3, QColor(12, 34, 56, 78));
    ImageFrame encodedFrame;
    encodedFrame.descriptor.size = encodedImage.size();
    encodedFrame.storage = std::move(encodedImage);
    const QPointF normalized = ComparisonPixelProbe::normalizedPixelCenter({1, 1}, {4, 2});
    QCOMPARE(normalized, QPointF(0.375, 0.75));
    const ComparisonPixelSample encoded = ComparisonPixelProbe::sample(encodedFrame, normalized);
    QVERIFY(encoded.valid);
    QCOMPARE(encoded.displayPixel, QPoint(3, 3));
    QCOMPARE(encoded.displayColor, QColor(12, 34, 56, 78));
    QCOMPARE(encoded.sourceValueText(), QStringLiteral("RGBA(12,34,56,78)"));
    QVERIFY(!ComparisonPixelProbe::sample(encodedFrame, QPointF(-0.01, 0.5)).valid);

    RawImageParameters parameters;
    parameters.size = {4, 2};
    parameters.format = RawPixelFormat::Raw16;
    parameters.validBitsOverride = 12;
    parameters.bayerPattern = BayerPattern::RGGB;
    parameters.orientation = ImageOrientation::Rotate90Clockwise;
    auto storage = std::make_shared<PlaneBufferSet>();
    storage->storage.resize(16);
    for (int index = 0; index < 8; ++index) {
        qToLittleEndian<quint16>(static_cast<quint16>(index + 1),
                                 reinterpret_cast<uchar*>(storage->storage.data() + index * 2));
    }
    storage->planes = {{0, 8, 16}};
    storage->displayImage = QImage(2, 4, QImage::Format_RGBA8888);
    storage->displayImage.fill(QColor(90, 80, 70));
    ImageFrame rawFrame;
    rawFrame.descriptor.size = {2, 4};
    rawFrame.rawParameters = parameters;
    rawFrame.storage = std::shared_ptr<const PlaneBufferSet>(storage);

    const ComparisonPixelSample raw = ComparisonPixelProbe::sample(rawFrame, QPointF(0.25, 0.125));
    QVERIFY(raw.valid);
    QCOMPARE(raw.displayPixel, QPoint(0, 0));
    QCOMPARE(raw.sourcePixel, QPoint(0, 1));
    QVERIFY(raw.bayer.has_value());
    QCOMPARE(raw.bayer->value, quint16{5});
    QCOMPARE(raw.sourceValueText(), QStringLiteral("RAW(5, Gb)"));
    QCOMPARE(raw.displayValueText(), QStringLiteral("RGBA(90,80,70,255)"));
}

void CoreTests::cursorAnchoredZoomPreservesImagePoint() {
    const QSize imageSize(1000, 800);
    const QSize viewport(500, 400);
    ViewState state;
    state.fitMode = FitMode::Manual;
    state.pixelsPerImagePixel = 0.5;
    const QPointF anchor(125.0, 100.0);
    const QPointF before = ViewTransform::widgetToImage(anchor, viewport, imageSize, state);
    const ViewState zoomed = ViewTransform::zoomAt(state, 1.0, anchor, viewport, imageSize);
    const QPointF after = ViewTransform::widgetToImage(anchor, viewport, imageSize, zoomed);
    QVERIFY(QLineF(before, after).length() < 0.0001);
}

void CoreTests::panUsesImageScale() {
    ViewState state;
    state.fitMode = FitMode::Manual;
    state.pixelsPerImagePixel = 1.0;
    const ViewState panned = ViewTransform::panBy(state, {100.0, 50.0}, {1000, 500});
    QCOMPARE(panned.normalizedCenter, QPointF(0.4, 0.4));
}

void CoreTests::synchronizationCanBeConfigured() {
    SyncGroup group;
    ViewState source;
    source.pixelsPerImagePixel = 2.0;
    source.normalizedCenter = {0.2, 0.3};
    source.fitMode = FitMode::Manual;
    ViewState target;
    target.pixelsPerImagePixel = 0.5;
    source.normalizedRoi = QRectF(0.1, 0.2, 0.3, 0.4);
    target.normalizedRoi = QRectF(0.5, 0.5, 0.2, 0.2);
    QCOMPARE(group.synchronizedState(source, target), source);

    ViewState previousSource;
    previousSource.fitMode = FitMode::Manual;
    previousSource.pixelsPerImagePixel = 1.0;
    previousSource.normalizedCenter = {0.5, 0.5};
    ViewState changedSource = previousSource;
    changedSource.pixelsPerImagePixel = 1.2;
    changedSource.normalizedCenter = {0.6, 0.45};
    ViewState offsetTarget;
    offsetTarget.fitMode = FitMode::Manual;
    offsetTarget.pixelsPerImagePixel = 2.0;
    offsetTarget.normalizedCenter = {0.25, 0.7};
    const ViewState relative =
        group.relativelySynchronizedState(previousSource, changedSource, offsetTarget);
    QVERIFY(std::abs(relative.pixelsPerImagePixel - 2.4) < 0.000001);
    QVERIFY(QLineF(relative.normalizedCenter, QPointF(0.35, 0.65)).length() < 0.000001);

    group.setPanSynchronized(false);
    const ViewState zoomOnly = group.synchronizedState(source, target);
    QCOMPARE(zoomOnly.pixelsPerImagePixel, 2.0);
    QCOMPARE(zoomOnly.normalizedCenter, target.normalizedCenter);
    QCOMPARE(zoomOnly.normalizedRoi, source.normalizedRoi);

    group.setRoiSynchronized(false);
    QCOMPARE(group.synchronizedState(source, target).normalizedRoi, target.normalizedRoi);
}

void CoreTests::weightedCacheEvictsLeastRecentlyUsed() {
    WeightedLruCache<QString> cache(10);
    cache.put(QStringLiteral("a"), std::make_shared<QString>(QStringLiteral("A")), 6);
    cache.put(QStringLiteral("b"), std::make_shared<QString>(QStringLiteral("B")), 4);
    QVERIFY(cache.contains(QStringLiteral("a")));
    QVERIFY(cache.get(QStringLiteral("a")) != nullptr);
    cache.put(QStringLiteral("c"), std::make_shared<QString>(QStringLiteral("C")), 4);
    QVERIFY(cache.get(QStringLiteral("a")) != nullptr);
    QVERIFY(!cache.contains(QStringLiteral("b")));
    QVERIFY(cache.get(QStringLiteral("b")) == nullptr);
    QVERIFY(cache.get(QStringLiteral("c")) != nullptr);
}

void CoreTests::rawFrameSizesRespectFormatAndStride() {
    RawImageParameters parameters;
    parameters.size = {8, 4};

    parameters.format = RawPixelFormat::NV12;
    QCOMPARE(minimumRowStride(parameters), 8);
    QCOMPARE(frameByteSize(parameters), 48);

    parameters.format = RawPixelFormat::I420;
    QCOMPARE(frameByteSize(parameters), 48);

    parameters.format = RawPixelFormat::P010;
    QCOMPARE(minimumRowStride(parameters), 16);
    QCOMPARE(frameByteSize(parameters), 96);

    parameters.format = RawPixelFormat::MipiRaw10;
    QCOMPARE(minimumRowStride(parameters), 10);
    QCOMPARE(frameByteSize(parameters), 40);

    parameters.format = RawPixelFormat::MipiRaw12;
    QCOMPARE(minimumRowStride(parameters), 12);
    QCOMPARE(frameByteSize(parameters), 48);

    parameters.format = RawPixelFormat::Raw16;
    parameters.rowStride = 20;
    QCOMPARE(frameByteSize(parameters), 80);
}

void CoreTests::rawFrameSizeRejectsInvalidGeometry() {
    RawImageParameters parameters;
    parameters.format = RawPixelFormat::Raw16;
    QVERIFY(frameByteSize(parameters) < 0);

    parameters.size = {16, 8};
    parameters.rowStride = 12;
    QVERIFY(frameByteSize(parameters) < 0);

    parameters.size = {std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
    parameters.format = RawPixelFormat::P010;
    parameters.rowStride = 0;
    QVERIFY(frameByteSize(parameters) < 0);
}

void CoreTests::rawFrameCountAndOddChromaStrideAreSafe() {
    RawImageParameters parameters;
    parameters.size = {3, 3};
    parameters.format = RawPixelFormat::NV12;
    QCOMPARE(minimumRowStride(parameters), 3);
    QCOMPARE(minimumChromaRowStride(parameters), 4);
    QCOMPARE(frameByteSize(parameters), 17);
    QCOMPARE(availableFrameCount(2 + 17 * 3 + 4, parameters), 3);

    parameters.headerOffset = 2;
    QCOMPARE(availableFrameCount(2 + 17 * 3 + 4, parameters), 3);
    parameters.frameIndex = 2;
    QCOMPARE(availableFrameCount(2 + 17 * 3 + 4, parameters), 3);

    parameters.format = RawPixelFormat::P010;
    parameters.headerOffset = 0;
    QCOMPARE(minimumRowStride(parameters), 6);
    QCOMPARE(minimumChromaRowStride(parameters), 8);
    QCOMPARE(frameByteSize(parameters), 34);

    parameters.size = {3840, 2160};
    QCOMPARE(estimatedFullFrameBytes(parameters),
             frameByteSize(parameters) + 3840LL * 2160LL * 4LL);
}

void CoreTests::rawDisplayTransformValidationAndCacheIdentity() {
    RawImageParameters raw;
    raw.size = {4, 4};
    raw.format = RawPixelFormat::Raw16;
    QCOMPARE(raw.validBits(), 16);
    QCOMPARE(raw.maximumSampleValue(), 65535);
    QVERIFY(raw.hasValidBitLayout());
    QVERIFY(raw.hasValidDisplayTransform());
    const QString identityKey = raw.cacheKey();
    raw.validBitsOverride = 14;
    QCOMPARE(raw.validBits(), 14);
    QCOMPARE(raw.maximumSampleValue(), 16383);
    QVERIFY(raw.hasValidBitLayout());
    QVERIFY(raw.cacheKey() != identityKey);
    const QString raw14Key = raw.cacheKey();
    raw.demosaic = true;
    QVERIFY(raw.cacheKey() != raw14Key);
    const QString demosaicKey = raw.cacheKey();
    raw.whiteBalanceGains[0] = 2.0;
    QVERIFY(raw.hasValidDisplayTransform());
    QVERIFY(raw.cacheKey() != demosaicKey);
    const QString whiteBalancedKey = raw.cacheKey();
    raw.colorCorrectionMatrix[1] = -0.25;
    QVERIFY(raw.cacheKey() != whiteBalancedKey);
    raw.displayGamma = 0.0;
    QVERIFY(!raw.hasValidDisplayTransform());

    RawImageParameters yuv;
    yuv.size = {4, 4};
    yuv.format = RawPixelFormat::NV12;
    yuv.validBitsOverride = 14;
    QVERIFY(!yuv.hasValidBitLayout());
    yuv.validBitsOverride = 0;
    const QString yuvKey = yuv.cacheKey();
    yuv.whiteBalanceGains[0] = 2.0;
    QCOMPARE(yuv.cacheKey(), yuvKey);
}

void CoreTests::rawOrientationMapsCoordinatesAndCacheIdentity() {
    const QSize sourceSize(3, 2);
    QCOMPARE(orientedImageSize(sourceSize, ImageOrientation::Normal), QSize(3, 2));
    QCOMPARE(orientedImageSize(sourceSize, ImageOrientation::Rotate90Clockwise), QSize(2, 3));
    QCOMPARE(orientedImageSize(sourceSize, ImageOrientation::Rotate180), QSize(3, 2));
    QCOMPARE(orientedImageSize(sourceSize, ImageOrientation::Rotate270Clockwise), QSize(2, 3));

    QCOMPARE(displayToSourcePixel({0, 0}, sourceSize, ImageOrientation::Rotate90Clockwise),
             QPoint(0, 1));
    QCOMPARE(displayToSourcePixel({1, 2}, sourceSize, ImageOrientation::Rotate90Clockwise),
             QPoint(2, 0));
    QCOMPARE(displayToSourcePixel({0, 0}, sourceSize, ImageOrientation::Rotate180), QPoint(2, 1));
    QCOMPARE(displayToSourcePixel({0, 0}, sourceSize, ImageOrientation::Rotate270Clockwise),
             QPoint(2, 0));
    QCOMPARE(displayToSourcePixel({1, 2}, sourceSize, ImageOrientation::Rotate270Clockwise),
             QPoint(0, 1));

    RawImageParameters parameters;
    parameters.size = sourceSize;
    const QString normalKey = parameters.cacheKey();
    parameters.orientation = ImageOrientation::Rotate180;
    QVERIFY(parameters.hasValidOrientation());
    QVERIFY(parameters.cacheKey() != normalKey);
    parameters.orientation = static_cast<ImageOrientation>(999);
    QVERIFY(!parameters.hasValidOrientation());
}

void CoreTests::displayHistogramComputesChannelsAndBoundedSampling() {
    QImage image(2, 2, QImage::Format_RGBA8888);
    image.setPixelColor(0, 0, QColor(255, 0, 0));
    image.setPixelColor(1, 0, QColor(0, 255, 0));
    image.setPixelColor(0, 1, QColor(0, 0, 255));
    image.setPixelColor(1, 1, QColor(255, 255, 255));
    ImageFrame frame;
    frame.descriptor.size = image.size();
    frame.storage = image;

    const DisplayHistogram exact = DisplayHistogramAnalyzer::analyze(frame);
    QVERIFY(exact.isValid());
    QVERIFY(!exact.usesDisplayProxy());
    QVERIFY(!exact.isSubsampled());
    QCOMPARE(exact.availablePixelCount, 4);
    QCOMPARE(exact.sampledPixelCount, 4);
    QCOMPARE(exact.red.bins[0], 2);
    QCOMPARE(exact.red.bins[255], 2);
    QCOMPARE(exact.green.bins[0], 2);
    QCOMPARE(exact.green.bins[255], 2);
    QCOMPARE(exact.blue.bins[0], 2);
    QCOMPARE(exact.blue.bins[255], 2);
    QCOMPARE(exact.red.minimum, 0);
    QCOMPARE(exact.red.maximum, 255);
    QCOMPARE(exact.red.mean, 127.5);
    QCOMPARE(exact.red.standardDeviation, 127.5);
    QCOMPARE(exact.luma.bins[19], 1);
    QCOMPARE(exact.luma.bins[54], 1);
    QCOMPARE(exact.luma.bins[182], 1);
    QCOMPARE(exact.luma.bins[255], 1);

    frame.descriptor.size = {4, 4};
    const DisplayHistogram bounded = DisplayHistogramAnalyzer::analyze(frame, 2);
    QVERIFY(bounded.isValid());
    QVERIFY(bounded.usesDisplayProxy());
    QVERIFY(bounded.isSubsampled());
    QCOMPARE(bounded.analyzedSize, QSize(2, 2));
    QCOMPARE(bounded.logicalSize, QSize(4, 4));
    QCOMPARE(bounded.availablePixelCount, 4);
    QCOMPARE(bounded.sampledPixelCount, 2);

    QImage tallImage(1, 9, QImage::Format_RGBA8888);
    tallImage.fill(Qt::black);
    frame.descriptor.size = tallImage.size();
    frame.storage = tallImage;
    const DisplayHistogram tallBounded = DisplayHistogramAnalyzer::analyze(frame, 3);
    QCOMPARE(tallBounded.availablePixelCount, 9);
    QCOMPARE(tallBounded.sampledPixelCount, 3);

    QCOMPARE(DisplayHistogramAnalyzer::analyze(frame, 0).sampledPixelCount, 0);
}

void CoreTests::displayHistogramRestrictsNormalizedRegion() {
    QImage image(4, 2, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    for (int y = 0; y < image.height(); ++y) {
        image.setPixelColor(2, y, Qt::white);
        image.setPixelColor(3, y, Qt::white);
    }
    ImageFrame frame;
    frame.descriptor.size = image.size();
    frame.storage = image;

    const DisplayHistogram left =
        DisplayHistogramAnalyzer::analyzeRegion(frame, QRectF(0.0, 0.0, 0.5, 1.0));
    QVERIFY(left.isValid());
    QVERIFY(left.isRegionLimited());
    QCOMPARE(left.logicalRegion, QRect(0, 0, 2, 2));
    QCOMPARE(left.analyzedRegion, QRect(0, 0, 2, 2));
    QCOMPARE(left.sampledPixelCount, 4);
    QCOMPARE(left.red.mean, 0.0);
    QCOMPARE(left.red.standardDeviation, 0.0);

    const DisplayHistogram right =
        DisplayHistogramAnalyzer::analyzeRegion(frame, QRectF(0.5, 0.0, 1.0, 1.0));
    QCOMPARE(right.logicalRegion, QRect(2, 0, 2, 2));
    QCOMPARE(right.red.mean, 255.0);
    QCOMPARE(right.red.standardDeviation, 0.0);
    QVERIFY(!DisplayHistogramAnalyzer::analyzeRegion(frame, QRectF(2.0, 2.0, 1.0, 1.0)).isValid());

    QCOMPARE(ViewTransform::clampedNormalizedRoi(QRectF(-0.2, 0.25, 0.7, 1.0)),
             std::optional<QRectF>(QRectF(0.0, 0.25, 0.5, 0.75)));
    QVERIFY(!ViewTransform::clampedNormalizedRoi(QRectF(2.0, 2.0, 1.0, 1.0)));
}

void CoreTests::rawPlaneAccessorAndHistogramPreserveEngineeringSamples() {
    RawImageParameters yuvParameters;
    yuvParameters.size = {4, 2};
    yuvParameters.format = RawPixelFormat::NV12;
    auto yuvStorage = std::make_shared<PlaneBufferSet>();
    yuvStorage->storage =
        QByteArray::fromRawData("\x0A\x14\x1E\x28\x32\x3C\x46\x50\x64\x96\x6E\xA0", 12);
    yuvStorage->planes = {{0, 4, 8}, {8, 4, 4}};
    ImageFrame yuvFrame;
    yuvFrame.descriptor.size = yuvParameters.size;
    yuvFrame.rawParameters = yuvParameters;
    yuvFrame.storage = std::shared_ptr<const PlaneBufferSet>(yuvStorage);

    RawPlaneAccessor yuvAccessor(yuvFrame);
    QVERIFY(yuvAccessor.isValid());
    const auto lastYuv = yuvAccessor.yuvAtSourcePixel({3, 1});
    QVERIFY(lastYuv.has_value());
    QCOMPARE(lastYuv->y, quint16{80});
    QCOMPARE(lastYuv->u, quint16{110});
    QCOMPARE(lastYuv->v, quint16{160});
    QCOMPARE(yuvAccessor.pixelDescriptionAtDisplayPixel({3, 1}), QStringLiteral("YUV(80,110,160)"));

    const RawPlaneHistogram yuvHistogram = RawPlaneHistogramAnalyzer::analyze(yuvFrame);
    QVERIFY(yuvHistogram.isValid());
    QCOMPARE(yuvHistogram.channels.size(), 3);
    QCOMPARE(yuvHistogram.channels.at(0).sampledSampleCount, 8);
    QCOMPARE(yuvHistogram.channels.at(0).mean, 45.0);
    QVERIFY(std::abs(yuvHistogram.channels.at(0).standardDeviation - std::sqrt(525.0)) < 0.0001);
    QCOMPARE(yuvHistogram.channels.at(1).sampledSampleCount, 2);
    QCOMPARE(yuvHistogram.channels.at(1).mean, 105.0);
    QCOMPARE(yuvHistogram.channels.at(2).mean, 155.0);
    const RawPlaneHistogram boundedYuv = RawPlaneHistogramAnalyzer::analyze(yuvFrame, 1);
    QCOMPARE(boundedYuv.channels.at(0).sampledSampleCount, 1);
    QCOMPARE(boundedYuv.channels.at(1).sampledSampleCount, 1);
    QVERIFY(boundedYuv.channels.at(0).isSubsampled());

    RawImageParameters tallYuvParameters = yuvParameters;
    tallYuvParameters.size = {1, 8};
    auto tallYuvStorage = std::make_shared<PlaneBufferSet>();
    tallYuvStorage->storage = QByteArray(16, '\0');
    tallYuvStorage->planes = {{0, 1, 8}, {8, 2, 8}};
    ImageFrame tallYuvFrame;
    tallYuvFrame.descriptor.size = tallYuvParameters.size;
    tallYuvFrame.rawParameters = tallYuvParameters;
    tallYuvFrame.storage = std::shared_ptr<const PlaneBufferSet>(tallYuvStorage);
    const RawPlaneHistogram tallBoundedYuv =
        RawPlaneHistogramAnalyzer::analyze(tallYuvFrame, 3);
    QCOMPARE(tallBoundedYuv.channels.at(0).availableSampleCount, 8);
    QCOMPARE(tallBoundedYuv.channels.at(0).sampledSampleCount, 3);

    const RawPlaneHistogram yuvLeft =
        RawPlaneHistogramAnalyzer::analyzeRegion(yuvFrame, QRectF(0.0, 0.0, 0.5, 1.0));
    QCOMPARE(yuvLeft.logicalRegion, QRect(0, 0, 2, 2));
    QCOMPARE(yuvLeft.sourceRegion, QRect(0, 0, 2, 2));
    QCOMPARE(yuvLeft.channels.at(0).sampledSampleCount, 4);
    QCOMPARE(yuvLeft.channels.at(0).mean, 35.0);
    QCOMPARE(yuvLeft.channels.at(1).sampledSampleCount, 1);
    QCOMPARE(yuvLeft.channels.at(1).minimum, 100);
    QCOMPARE(yuvLeft.channels.at(2).minimum, 150);

    yuvParameters.orientation = ImageOrientation::Rotate90Clockwise;
    yuvFrame.rawParameters = yuvParameters;
    yuvFrame.descriptor.size = orientedImageSize(yuvParameters.size, yuvParameters.orientation);
    RawPlaneAccessor rotatedYuvAccessor(yuvFrame);
    QCOMPARE(rotatedYuvAccessor.displaySize(), QSize(2, 4));
    const auto rotatedTopLeft = rotatedYuvAccessor.yuvAtDisplayPixel({0, 0});
    QVERIFY(rotatedTopLeft.has_value());
    QCOMPARE(rotatedTopLeft->sourcePixel, QPoint(0, 1));
    QCOMPARE(rotatedTopLeft->y, quint16{50});
    const RawPlaneHistogram rotatedLeft =
        RawPlaneHistogramAnalyzer::analyzeRegion(yuvFrame, QRectF(0.0, 0.0, 0.5, 1.0));
    QCOMPARE(rotatedLeft.logicalRegion, QRect(0, 0, 1, 4));
    QCOMPARE(rotatedLeft.sourceRegion, QRect(0, 1, 4, 1));
    QCOMPARE(rotatedLeft.channels.at(0).mean, 65.0);

    RawImageParameters bayerParameters;
    bayerParameters.size = {4, 2};
    bayerParameters.format = RawPixelFormat::Raw16;
    bayerParameters.validBitsOverride = 12;
    bayerParameters.bayerPattern = BayerPattern::RGGB;
    auto bayerStorage = std::make_shared<PlaneBufferSet>();
    bayerStorage->storage.resize(16);
    for (int index = 0; index < 8; ++index) {
        qToLittleEndian<quint16>(
            static_cast<quint16>(index + 1),
            reinterpret_cast<uchar*>(bayerStorage->storage.data() + index * 2));
    }
    bayerStorage->planes = {{0, 8, 16}};
    ImageFrame bayerFrame;
    bayerFrame.descriptor.size = bayerParameters.size;
    bayerFrame.rawParameters = bayerParameters;
    bayerFrame.storage = std::shared_ptr<const PlaneBufferSet>(bayerStorage);

    RawPlaneAccessor bayerAccessor(bayerFrame);
    QVERIFY(bayerAccessor.isValid());
    const auto greenBlue = bayerAccessor.bayerAtSourcePixel({2, 1});
    QVERIFY(greenBlue.has_value());
    QCOMPARE(greenBlue->value, quint16{7});
    QCOMPARE(greenBlue->channel, BayerSampleChannel::GreenBlueRow);
    QCOMPARE(bayerAccessor.pixelDescriptionAtDisplayPixel({2, 1}), QStringLiteral("RAW(7, Gb)"));
    const RawPlaneHistogram bayerHistogram = RawPlaneHistogramAnalyzer::analyze(bayerFrame);
    QVERIFY(bayerHistogram.isValid());
    QCOMPARE(bayerHistogram.channels.size(), 4);
    QCOMPARE(bayerHistogram.channels.at(0).mean, 2.0);
    QCOMPARE(bayerHistogram.channels.at(1).mean, 3.0);
    QCOMPARE(bayerHistogram.channels.at(2).mean, 6.0);
    QCOMPARE(bayerHistogram.channels.at(3).mean, 7.0);
    QCOMPARE(bayerHistogram.channels.at(0).bins.at(1), quint64{1});
    QCOMPARE(bayerHistogram.channels.at(3).bins.at(8), quint64{1});

    bayerParameters.whiteLevel = 6;
    bayerFrame.rawParameters = bayerParameters;
    const RawPlaneHistogram whiteLevelHistogram = RawPlaneHistogramAnalyzer::analyze(bayerFrame);
    QCOMPARE(whiteLevelHistogram.maximumValue, 6);
    QCOMPARE(whiteLevelHistogram.channels.at(3).bins.size(), 7);
    QCOMPARE(whiteLevelHistogram.channels.at(3).bins.at(6), quint64{2});
    bayerParameters.whiteLevel = 0;
    bayerFrame.rawParameters = bayerParameters;
    const RawPlaneHistogram boundedBayer = RawPlaneHistogramAnalyzer::analyze(bayerFrame, 1);
    for (const RawHistogramChannel& channel : boundedBayer.channels) {
        QCOMPARE(channel.sampledSampleCount, 1);
        QVERIFY(channel.isSubsampled());
    }

    const RawPlaneHistogram bayerLeft =
        RawPlaneHistogramAnalyzer::analyzeRegion(bayerFrame, QRectF(0.0, 0.0, 0.5, 1.0));
    QCOMPARE(bayerLeft.logicalRegion, QRect(0, 0, 2, 2));
    QCOMPARE(bayerLeft.channels.at(0).sampledSampleCount, 1);
    QCOMPARE(bayerLeft.channels.at(0).mean, 1.0);
    QCOMPARE(bayerLeft.channels.at(1).mean, 2.0);
    QCOMPARE(bayerLeft.channels.at(2).mean, 5.0);
    QCOMPARE(bayerLeft.channels.at(3).mean, 6.0);

    auto truncated = std::make_shared<PlaneBufferSet>(*bayerStorage);
    truncated->storage.chop(1);
    bayerFrame.storage = std::shared_ptr<const PlaneBufferSet>(truncated);
    QVERIFY(!RawPlaneAccessor(bayerFrame).isValid());
    QVERIFY(!RawPlaneHistogramAnalyzer::analyze(bayerFrame).isValid());
    QVERIFY(
        !RawPlaneHistogramAnalyzer::analyzeRegion(yuvFrame, QRectF(2.0, 2.0, 1.0, 1.0)).isValid());
}

} // namespace ispview

QTEST_GUILESS_MAIN(ispview::CoreTests)
#include "core_tests.moc"
