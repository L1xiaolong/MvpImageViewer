#include "core/comparison_pixel_probe.h"
#include "core/display_histogram.h"
#include "core/raw_image_parameters.h"
#include "core/raw_plane_access.h"
#include "core/raw_plane_histogram.h"
#include "core/sync_group.h"
#include "core/view_state.h"
#include "core/weighted_lru_cache.h"

#include <QTest>
#include <QtCore/qfloat16.h>
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
    void unifiedHistogramUsesExactPixelsAndBayerColors();
    void rawPlaneAccessorAndHistogramPreserveEngineeringSamples();
    void quadBayerMapsFourByFourBlocksAndDemosaics();
    void rawMosaicHelpersPreserveGeometryAndPhase();
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

void CoreTests::rawMosaicHelpersPreserveGeometryAndPhase() {
    // Cropping a mosaic off its origin moves the CFA phase one step in x and the opposite step
    // in y; even offsets leave the phase untouched.
    QCOMPARE(shiftedBayerPattern(BayerPattern::RGGB, 0, 0), BayerPattern::RGGB);
    QCOMPARE(shiftedBayerPattern(BayerPattern::RGGB, 1, 0), BayerPattern::GRBG);
    QCOMPARE(shiftedBayerPattern(BayerPattern::RGGB, 0, 1), BayerPattern::GBRG);
    QCOMPARE(shiftedBayerPattern(BayerPattern::RGGB, 1, 1), BayerPattern::BGGR);
    QCOMPARE(shiftedBayerPattern(BayerPattern::RGGB, 2, 4), BayerPattern::RGGB);
    QCOMPARE(shiftedBayerPattern(BayerPattern::BGGR, -1, 0), BayerPattern::GBRG);

    // A 4x3 mosaic holding 100..111, cropped at (1,1).
    std::array<quint16, 12> mosaic{};
    for (std::size_t index = 0; index < mosaic.size(); ++index) {
        mosaic[index] = static_cast<quint16>(100 + index);
    }
    const QByteArray plane =
        packedMosaicPlane(mosaic.data(), 4, QSize(4, 3), QRect(1, 1, 2, 2), true, 12);
    QCOMPARE(plane.size(), qsizetype(8));
    const auto sampleAt = [&plane](qsizetype index) {
        return qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar*>(plane.constData() + index * 2));
    };
    QCOMPARE(sampleAt(0), quint16{105});
    QCOMPARE(sampleAt(1), quint16{106});
    QCOMPARE(sampleAt(2), quint16{109});
    QCOMPARE(sampleAt(3), quint16{110});
    // Crops outside the array or wider than the stride are rejected instead of reading past it.
    QVERIFY(packedMosaicPlane(mosaic.data(), 4, QSize(4, 3), QRect(3, 2, 2, 2), true, 12).isEmpty());
    QVERIFY(packedMosaicPlane(mosaic.data(), 2, QSize(4, 3), QRect(2, 0, 2, 1), true, 12).isEmpty());
    QVERIFY(packedMosaicPlane(nullptr, 4, QSize(4, 3), QRect(0, 0, 1, 1), true, 12).isEmpty());
    // Every sample is masked to the container's valid-bit range.
    const std::array<quint16, 1> deep{0xFFFF};
    const QByteArray masked =
        packedMosaicPlane(deep.data(), 1, QSize(1, 1), QRect(0, 0, 1, 1), true, 12);
    QCOMPARE(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(masked.constData())),
             quint16{0x0FFF});

    // An un-demosaiced frame renders CFA false colour: each original sample is linearly
    // normalized by its bit-depth maximum and written only to its filter channel.
    RawImageParameters mosaicParameters;
    mosaicParameters.size = {2, 2};
    mosaicParameters.format = RawPixelFormat::Raw16;
    mosaicParameters.validBitsOverride = 12;
    mosaicParameters.blackLevel = 100;
    mosaicParameters.whiteLevel = 4095;
    mosaicParameters.displayGamma = 2.2;
    const QByteArray mosaicPlane =
        packedMosaicPlane(mosaic.data(), 4, QSize(4, 3), QRect(1, 1, 2, 2), true, 16);
    const QImage falseColour = cfaMosaicImage(mosaicPlane, mosaicParameters);
    QCOMPARE(falseColour.size(), QSize(2, 2));
    const auto expectedLevel = [&mosaicParameters](int value) {
        return static_cast<int>(std::lround(
            value / static_cast<double>(mosaicParameters.maximumSampleValue()) * 255.0));
    };
    QCOMPARE(falseColour.pixelColor(0, 0), QColor(expectedLevel(105), 0, 0));
    QCOMPARE(falseColour.pixelColor(1, 0), QColor(0, expectedLevel(106), 0));
    QCOMPARE(falseColour.pixelColor(0, 1), QColor(0, expectedLevel(109), 0));
    QCOMPARE(falseColour.pixelColor(1, 1), QColor(0, 0, expectedLevel(110)));
    // The transform keeps the RAW-depth anchors: zero maps to 0 and the bit-depth maximum to 255.
    RawImageParameters onePixel = mosaicParameters;
    onePixel.size = {1, 1};
    const auto redOf = [&onePixel](quint16 value) {
        return cfaMosaicImage(
                   packedMosaicPlane(&value, 1, QSize(1, 1), QRect(0, 0, 1, 1), true, 16),
                   onePixel)
            .pixelColor(0, 0);
    };
    QCOMPARE(redOf(0), QColor(0, 0, 0));
    QCOMPARE(redOf(4095), QColor(255, 0, 0));
    QVERIFY(redOf(2048).red() > redOf(1024).red());
    // A bounded output samples the mosaic with nearest neighbours, centring on the source grid.
    const QImage thumb = cfaMosaicImage(mosaicPlane, mosaicParameters, QSize(1, 1));
    QCOMPARE(thumb.size(), QSize(1, 1));
    QCOMPARE(thumb.pixelColor(0, 0), falseColour.pixelColor(1, 1));
    // Sizes that do not match the plane are rejected instead of reading past it.
    RawImageParameters mismatched = mosaicParameters;
    mismatched.size = {4, 4};
    QVERIFY(cfaMosaicImage(mosaicPlane, mismatched).isNull());

    // Orientation uses the same mapping the plane reads rely on.
    QImage source(3, 2, QImage::Format_RGBA8888);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            source.setPixelColor(x, y, QColor(y * 3 + x, 0, 0));
        }
    }
    const QImage rotatedClockwise =
        orientedImage(source, ImageOrientation::Rotate90Clockwise);
    QCOMPARE(rotatedClockwise.size(), QSize(2, 3));
    QCOMPARE(rotatedClockwise.pixelColor(0, 0), QColor(3, 0, 0));
    QCOMPARE(rotatedClockwise.pixelColor(1, 0), QColor(0, 0, 0));
    QCOMPARE(rotatedClockwise.pixelColor(0, 2), QColor(5, 0, 0));
    const QImage rotatedHalf = orientedImage(source, ImageOrientation::Rotate180);
    QCOMPARE(rotatedHalf.size(), source.size());
    QCOMPARE(rotatedHalf.pixelColor(0, 0), QColor(5, 0, 0));
    const QImage rotatedCounter =
        orientedImage(source, ImageOrientation::Rotate270Clockwise);
    QCOMPARE(rotatedCounter.size(), QSize(2, 3));
    QCOMPARE(rotatedCounter.pixelColor(0, 0), QColor(2, 0, 0));
    QCOMPARE(orientedImage(source, ImageOrientation::Normal).pixelColor(2, 1), QColor(5, 0, 0));
}

void CoreTests::quadBayerMapsFourByFourBlocksAndDemosaics() {
    RawImageParameters parameters;
    parameters.size = {4, 4};
    parameters.format = RawPixelFormat::Raw16;
    parameters.validBitsOverride = 12;
    parameters.bayerPattern = BayerPattern::RGGB;
    parameters.bayerSampling = BayerSampling::QuadBayer4x4;
    parameters.demosaic = true;

    auto storage = std::make_shared<PlaneBufferSet>();
    storage->storage.resize(4 * 4 * 2);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const BayerSampleChannel channel = RawPlaneAccessor::channelAtSourcePixel(
                parameters.bayerPattern, {x, y}, parameters.bayerSampling);
            const quint16 value = channel == BayerSampleChannel::Red
                                      ? 4095
                                  : channel == BayerSampleChannel::Blue ? 0 : 2048;
            qToLittleEndian<quint16>(
                value, reinterpret_cast<uchar*>(storage->storage.data() + (y * 4 + x) * 2));
        }
    }
    storage->planes = {{0, 8, 32}};
    ImageFrame frame;
    frame.descriptor.size = parameters.size;
    frame.rawParameters = parameters;
    frame.storage = std::shared_ptr<const PlaneBufferSet>(storage);

    RawPlaneAccessor accessor(frame);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            QCOMPARE(accessor.bayerAtSourcePixel({x, y})->channel, BayerSampleChannel::Red);
    QCOMPARE(accessor.bayerAtSourcePixel({2, 0})->channel,
             BayerSampleChannel::GreenRedRow);
    QCOMPARE(accessor.bayerAtSourcePixel({0, 2})->channel,
             BayerSampleChannel::GreenBlueRow);
    QCOMPARE(accessor.bayerAtSourcePixel({3, 3})->channel, BayerSampleChannel::Blue);

    const ComparisonPixelSample sample =
        ComparisonPixelProbe::sampleAtDisplayPixel(frame, {0, 0});
    QVERIFY(sample.valid);
    QCOMPARE(sample.sourceValueText(), QStringLiteral("RGB(4095,2048,0)"));

    const RawPlaneHistogram histogram = RawPlaneHistogramAnalyzer::analyze(frame);
    QCOMPARE(histogram.channels.size(), 4);
    for (const RawHistogramChannel& channel : histogram.channels)
        QCOMPARE(channel.sampledSampleCount, 4);
    QCOMPARE(histogram.channels.at(0).mean, 4095.0);
    QCOMPARE(histogram.channels.at(1).mean, 2048.0);
    QCOMPARE(histogram.channels.at(2).mean, 2048.0);
    QCOMPARE(histogram.channels.at(3).mean, 0.0);
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
    QCOMPARE(encoded.sourceValueText(), QStringLiteral("RGB(12,34,56)"));
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

    // A demosaiced RAW frame reports the pipeline RGB in the RAW container's value range
    // instead of an 8-bit display triple. RGGB positions carry 12-bit red, green, and blue
    // samples of 4095, 2048, and 0.
    RawImageParameters demosaicParameters;
    demosaicParameters.size = {4, 4};
    demosaicParameters.format = RawPixelFormat::Raw16;
    demosaicParameters.validBitsOverride = 12;
    demosaicParameters.bayerPattern = BayerPattern::RGGB;
    demosaicParameters.demosaic = true;
    auto demosaicStorage = std::make_shared<PlaneBufferSet>();
    demosaicStorage->storage.resize(4 * 4 * 2);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const bool red = (y % 2 == 0) && (x % 2 == 0);
            const bool blue = (y % 2 == 1) && (x % 2 == 1);
            const quint16 value = red ? 4095 : blue ? 0 : 2048;
            qToLittleEndian<quint16>(
                value, reinterpret_cast<uchar*>(demosaicStorage->storage.data() +
                                               (y * 4 + x) * 2));
        }
    }
    demosaicStorage->planes = {{0, 8, 32}};
    ImageFrame demosaicFrame;
    demosaicFrame.descriptor.size = demosaicParameters.size;
    demosaicFrame.rawParameters = demosaicParameters;
    demosaicFrame.storage = std::shared_ptr<const PlaneBufferSet>(demosaicStorage);
    const auto demosaicSample =
        ComparisonPixelProbe::sampleAtDisplayPixel(demosaicFrame, {1, 1});
    QVERIFY(demosaicSample.valid);
    QVERIFY(demosaicSample.bayer.has_value());
    QCOMPARE(demosaicSample.bayer->value, quint16{0});
    QCOMPARE(demosaicSample.sourceValueText(), QStringLiteral("RGB(4095,2048,0)"));
    QCOMPARE(demosaicSample.displayValueText(), QStringLiteral("RGB(255,186,0)"));
    QCOMPARE(raw.sourceValueText(), QStringLiteral("RGB(0,5,0)"));
    QCOMPARE(raw.displayValueText(), QStringLiteral("RGB(0,0,0)"));

    RawImageParameters yuvParameters;
    yuvParameters.size = {2, 2};
    yuvParameters.format = RawPixelFormat::NV12;
    yuvParameters.range = QuantizationRange::Full;
    auto yuvStorage = std::make_shared<PlaneBufferSet>();
    yuvStorage->storage = QByteArray(6, '\0');
    yuvStorage->storage[0] = char(0);
    yuvStorage->storage[1] = char(255);
    yuvStorage->storage[2] = char(64);
    yuvStorage->storage[3] = char(128);
    yuvStorage->storage[4] = char(128);
    yuvStorage->storage[5] = char(128);
    yuvStorage->planes = {{0, 2, 4}, {4, 2, 2}};
    yuvStorage->displayImage = QImage(1, 1, QImage::Format_RGBA8888);
    yuvStorage->displayImage.fill(Qt::red);
    ImageFrame yuvFrame;
    yuvFrame.descriptor.size = yuvParameters.size;
    yuvFrame.rawParameters = yuvParameters;
    yuvFrame.storage = std::shared_ptr<const PlaneBufferSet>(yuvStorage);

    const auto darkYuv = ComparisonPixelProbe::sample(
        yuvFrame, ComparisonPixelProbe::normalizedPixelCenter({0, 0}, yuvParameters.size));
    const auto brightYuv = ComparisonPixelProbe::sample(
        yuvFrame, ComparisonPixelProbe::normalizedPixelCenter({1, 0}, yuvParameters.size));
    QCOMPARE(darkYuv.displayPixel, QPoint(0, 0));
    QCOMPARE(darkYuv.displayColor, QColor(0, 0, 0));
    QCOMPARE(darkYuv.sourceValueText(), QStringLiteral("YUV(0,128,128) · RGB(0,0,0)"));
    QCOMPARE(brightYuv.displayPixel, QPoint(1, 0));
    QCOMPARE(brightYuv.displayColor, QColor(255, 255, 255));
    QCOMPARE(brightYuv.sourceValueText(), QStringLiteral("YUV(255,128,128) · RGB(255,255,255)"));

    RawImageParameters p010Parameters;
    p010Parameters.size = {2, 2};
    p010Parameters.format = RawPixelFormat::P010;
    p010Parameters.msbAligned = true;
    auto p010Storage = std::make_shared<PlaneBufferSet>();
    p010Storage->storage.resize(12);
    const quint16 p010Values[]{64, 940, 64, 940, 512, 512};
    for (int index = 0; index < 6; ++index) {
        qToLittleEndian<quint16>(
            static_cast<quint16>(p010Values[index] << 6),
            reinterpret_cast<uchar*>(p010Storage->storage.data() + index * 2));
    }
    p010Storage->planes = {{0, 4, 8}, {8, 4, 4}};
    ImageFrame p010Frame;
    p010Frame.descriptor.size = p010Parameters.size;
    p010Frame.rawParameters = p010Parameters;
    p010Frame.storage = std::shared_ptr<const PlaneBufferSet>(p010Storage);
    const auto p010Sample = ComparisonPixelProbe::sampleAtDisplayPixel(p010Frame, {1, 0});
    QVERIFY(p010Sample.valid);
    QCOMPARE(p010Sample.sourceValueText(),
             QStringLiteral("YUV(940,512,512) · RGB(1023,1023,1023)"));

    QImage highDepth(1, 1, QImage::Format_RGBA64);
    highDepth.setPixelColor(0, 0, QColor::fromRgba64(1024, 32768, 65535, 4567));
    ImageFrame highDepthFrame;
    highDepthFrame.descriptor.size = highDepth.size();
    highDepthFrame.descriptor.validBits = 16;
    highDepthFrame.storage = std::move(highDepth);
    const auto highDepthSample =
        ComparisonPixelProbe::sampleAtDisplayPixel(highDepthFrame, {0, 0});
    QCOMPARE(highDepthSample.sourceValueText(), QStringLiteral("RGB(1024,32768,65535)"));

    QImage premultipliedDepth(1, 1, QImage::Format_RGBA64_Premultiplied);
    premultipliedDepth.setPixelColor(0, 0, QColor::fromRgba64(512, 1024, 2048, 32768));
    ImageFrame premultipliedDepthFrame;
    premultipliedDepthFrame.descriptor.size = premultipliedDepth.size();
    premultipliedDepthFrame.storage = std::move(premultipliedDepth);
    const auto premultipliedDepthSample =
        ComparisonPixelProbe::sampleAtDisplayPixel(premultipliedDepthFrame, {0, 0});
    QCOMPARE(premultipliedDepthSample.sourceValueText(), QStringLiteral("RGB(512,1024,2048)"));

    // A twelve-bit sensor sample above the 8-bit range must survive verbatim.
    RawImageParameters deepParameters = parameters;
    deepParameters.size = {1, 1};
    deepParameters.orientation = ImageOrientation::Normal;
    auto deepStorage = std::make_shared<PlaneBufferSet>();
    deepStorage->storage.resize(sizeof(quint16));
    qToLittleEndian<quint16>(4095,
                             reinterpret_cast<uchar*>(deepStorage->storage.data()));
    deepStorage->planes = {{0, 2, 2}};
    ImageFrame deepFrame;
    deepFrame.descriptor.size = deepParameters.size;
    deepFrame.rawParameters = deepParameters;
    deepFrame.storage = std::shared_ptr<const PlaneBufferSet>(deepStorage);
    const auto deepRaw = ComparisonPixelProbe::sampleAtDisplayPixel(deepFrame, {0, 0});
    QVERIFY(deepRaw.valid);
    QCOMPARE(deepRaw.sourceValueText(), QStringLiteral("RGB(4095,0,0)"));

    // Floating point frames keep the stored sample instead of a quantized QColor round trip.
    QImage halfFloat(1, 1, QImage::Format_RGBA16FPx4);
    auto* halfLine = reinterpret_cast<qfloat16*>(halfFloat.scanLine(0));
    halfLine[0] = qfloat16(0.5F);
    halfLine[1] = qfloat16(0.25F);
    halfLine[2] = qfloat16(0.125F);
    halfLine[3] = qfloat16(1.0F);
    ImageFrame halfFloatFrame;
    halfFloatFrame.descriptor.size = halfFloat.size();
    halfFloatFrame.storage = std::move(halfFloat);
    const auto halfFloatSample =
        ComparisonPixelProbe::sampleAtDisplayPixel(halfFloatFrame, {0, 0});
    QCOMPARE(halfFloatSample.sourceValueText(), QStringLiteral("RGB(0.5,0.25,0.125)"));

    QImage fullFloat(1, 1, QImage::Format_RGBA32FPx4);
    auto* floatLine = reinterpret_cast<float*>(fullFloat.scanLine(0));
    floatLine[0] = 2.5F;
    floatLine[1] = 0.001F;
    floatLine[2] = -0.25F;
    floatLine[3] = 1.0F;
    ImageFrame fullFloatFrame;
    fullFloatFrame.descriptor.size = fullFloat.size();
    fullFloatFrame.storage = std::move(fullFloat);
    const auto fullFloatSample =
        ComparisonPixelProbe::sampleAtDisplayPixel(fullFloatFrame, {0, 0});
    QCOMPARE(fullFloatSample.sourceValueText(), QStringLiteral("RGB(2.5,0.001,-0.25)"));

    // A bounded RAW/YUV preview renders through the full source size, so hover coordinates in
    // that space must still resolve against the decoded proxy.
    RawImageParameters previewParameters;
    previewParameters.size = {8, 8};
    previewParameters.format = RawPixelFormat::Raw16;
    QImage previewImage(4, 4, QImage::Format_RGBA8888);
    previewImage.fill(QColor(40, 50, 60));
    previewImage.setPixelColor(3, 3, QColor(70, 80, 90));
    ImageFrame previewFrame;
    previewFrame.descriptor.size = previewImage.size();
    previewFrame.rawParameters = previewParameters;
    previewFrame.storage = std::move(previewImage);
    QCOMPARE(ComparisonPixelProbe::logicalFrameSize(previewFrame), QSize(8, 8));
    const auto previewSample = ComparisonPixelProbe::sampleAtLogicalPixel(previewFrame, {7, 7},
                                                                         QSize(8, 8));
    QVERIFY(previewSample.valid);
    QCOMPARE(previewSample.displayPixel, QPoint(7, 7));
    QCOMPARE(previewSample.displayColor, QColor(70, 80, 90));
    QCOMPARE(previewSample.sourceValueText(), QStringLiteral("RGB(70,80,90)"));
    const auto edgeSample = ComparisonPixelProbe::sampleAtLogicalPixel(previewFrame, {0, 0},
                                                                      QSize(8, 8));
    QCOMPARE(edgeSample.displayColor, QColor(40, 50, 60));

    // A RAW/YUV proxy whose source planes still need a full decode reports that state instead
    // of passing off proxy pixels as source samples.
    RawImageParameters pendingParameters;
    pendingParameters.size = {8, 8};
    pendingParameters.format = RawPixelFormat::Raw16;
    pendingParameters.demosaic = false;
    QImage pendingImage(4, 4, QImage::Format_RGBA8888);
    pendingImage.fill(QColor(40, 50, 60));
    ImageFrame pendingFrame;
    pendingFrame.descriptor.size = pendingImage.size();
    pendingFrame.rawParameters = pendingParameters;
    pendingFrame.sourceSamplesPending = true;
    pendingFrame.storage = std::move(pendingImage);
    const auto pendingSample =
        ComparisonPixelProbe::sampleAtLogicalPixel(pendingFrame, {7, 7}, QSize(8, 8));
    QVERIFY(pendingSample.valid);
    QVERIFY(pendingSample.sourceSamplesPending);
    QCOMPARE(pendingSample.sourceValueText(), QStringLiteral("Loading pixel data…"));

    // Once the full decode arrives, the same position reports real samples.
    pendingFrame.sourceSamplesPending = false;
    QCOMPARE(ComparisonPixelProbe::sampleAtLogicalPixel(pendingFrame, {7, 7}, QSize(8, 8))
                 .sourceValueText(),
             QStringLiteral("RGB(40,50,60)"));
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

    ViewState magnified;
    magnified.fitMode = FitMode::Manual;
    magnified.pixelsPerImagePixel = 64.0;
    magnified.normalizedCenter = {0.43125, 0.6175};
    const QSizeF fractionalViewport(499.5, 333.25);
    const QPoint expectedPixel(432, 123);
    const QPointF pixelCenter = ViewTransform::imageToWidget(
        QPointF(expectedPixel.x() + 0.5, expectedPixel.y() + 0.5), fractionalViewport,
        imageSize, magnified);
    QCOMPARE(ViewTransform::imagePixelAtWidgetPoint(pixelCenter, fractionalViewport, imageSize,
                                                     magnified),
             std::optional<QPoint>(expectedPixel));
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
    raw.bayerSampling = BayerSampling::QuadBayer4x4;
    QVERIFY(raw.hasValidBayerSampling());
    QVERIFY(raw.cacheKey() != raw14Key);
    const QString quadBayerKey = raw.cacheKey();
    raw.demosaic = true;
    QVERIFY(raw.cacheKey() != quadBayerKey);
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
    QCOMPARE(exact.luma.bins[18], 1);
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

    QImage highDepth(2, 1, QImage::Format_RGBA64);
    highDepth.setPixelColor(0, 0, QColor::fromRgba64(0x1234, 0x5678, 0x9ABC, 0xFFFF));
    highDepth.setPixelColor(1, 0, QColor::fromRgba64(0xFFFF, 0x0000, 0x8000, 0xFFFF));
    frame.descriptor.size = highDepth.size();
    frame.descriptor.storageBits = 16;
    frame.descriptor.validBits = 16;
    frame.storage = highDepth;
    const DisplayHistogram normalized = DisplayHistogramAnalyzer::analyze(frame);
    QCOMPARE(normalized.maximumValue, 255);
    QCOMPARE(normalized.red.bins.at(18), quint64{1});
    QCOMPARE(normalized.red.bins.at(255), quint64{1});
    QCOMPARE(normalized.red.mean, 136.5);
    const DisplayHistogram highDepthHistogram = DisplayHistogramAnalyzer::analyzeNativeRgb(frame);
    QCOMPARE(highDepthHistogram.maximumValue, 65535);
    QCOMPARE(highDepthHistogram.red.bins.size(), 65536);
    QCOMPARE(highDepthHistogram.red.bins.at(0x1234), quint64{1});
    QCOMPARE(highDepthHistogram.green.bins.at(0x5678), quint64{1});
    QCOMPARE(highDepthHistogram.blue.bins.at(0x9ABC), quint64{1});
    QCOMPARE(highDepthHistogram.red.mean, 35097.5);
    QCOMPARE(highDepthHistogram.red.standardDeviation, 30437.5);
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
    yuvStorage->displayImage = QImage(1, 1, QImage::Format_RGBA8888);
    yuvStorage->displayImage.fill(Qt::red);
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

    const DisplayHistogram yuvDisplayHistogram = DisplayHistogramAnalyzer::analyze(yuvFrame);
    QCOMPARE(yuvDisplayHistogram.analyzedSize, QSize(4, 2));
    QCOMPARE(yuvDisplayHistogram.sampledPixelCount, 8);
    QVERIFY(!yuvDisplayHistogram.usesDisplayProxy());

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
    bayerStorage->displayImage = QImage(1, 1, QImage::Format_RGBA8888);
    bayerStorage->displayImage.fill(Qt::red);
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
    QCOMPARE(bayerAccessor.pixelDescriptionAtDisplayPixel({2, 1}), QStringLiteral("RGB(0,7,0)"));
    const DisplayHistogram bayerDisplayHistogram = DisplayHistogramAnalyzer::analyze(bayerFrame);
    QCOMPARE(bayerDisplayHistogram.analyzedSize, QSize(4, 2));
    QCOMPARE(bayerDisplayHistogram.sampledPixelCount, 8);
    QVERIFY(!bayerDisplayHistogram.usesDisplayProxy());
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
    QCOMPARE(whiteLevelHistogram.maximumValue, 4095);
    QCOMPARE(whiteLevelHistogram.channels.at(3).bins.size(), 4096);
    QCOMPARE(whiteLevelHistogram.channels.at(3).bins.at(6), quint64{1});
    QCOMPARE(whiteLevelHistogram.channels.at(3).bins.at(8), quint64{1});
    QCOMPARE(whiteLevelHistogram.channels.at(3).mean, 7.0);
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

void CoreTests::unifiedHistogramUsesExactPixelsAndBayerColors() {
    QImage stripes(1024, 1024, QImage::Format_RGBA8888);
    for (int y = 0; y < stripes.height(); ++y)
        for (int x = 0; x < stripes.width(); ++x)
            stripes.setPixelColor(x, y, x % 2 ? Qt::white : Qt::black);
    ImageFrame rgb;
    rgb.storage = stripes;
    rgb.descriptor.size = stripes.size();
    const auto exact = DisplayHistogramAnalyzer::analyze(rgb);
    QCOMPARE(exact.sampledPixelCount, qint64{1048576});
    QCOMPARE(exact.red.bins.at(0), quint64{524288});
    QCOMPARE(exact.red.bins.at(255), quint64{524288});
    QCOMPARE(exact.red.mean, 127.5);
    QCOMPARE(exact.red.standardDeviation, 127.5);

    for (BayerPattern pattern : {BayerPattern::RGGB, BayerPattern::GRBG,
                                 BayerPattern::GBRG, BayerPattern::BGGR}) {
        RawImageParameters parameters;
        parameters.size = {4, 4};
        parameters.format = RawPixelFormat::Raw16;
        parameters.bayerPattern = pattern;
        parameters.blackLevel = 16;
        parameters.whiteLevel = 271;
        parameters.displayGamma = 1.0;
        parameters.demosaic = false; // Histogram still reconstructs RGB.
        parameters.whiteBalanceGains = {2.0, 1.0, 1.0};
        parameters.colorCorrectionMatrix = {0, 0, 1, 0, 1, 0, 1, 0, 0};
        auto planes = std::make_shared<PlaneBufferSet>();
        planes->storage.resize(32);
        planes->planes = {{0, 8, 32}};
        planes->displayImage = QImage(1, 1, QImage::Format_RGBA8888);
        planes->displayImage.fill(Qt::white);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                const auto channel = RawPlaneAccessor::channelAtSourcePixel(pattern, {x, y});
                const quint16 value = channel == BayerSampleChannel::Red ? 48
                                    : channel == BayerSampleChannel::Blue ? 144 : 80;
                qToLittleEndian(value, reinterpret_cast<uchar*>(planes->storage.data() + y * 8 + x * 2));
            }
        }
        ImageFrame frame;
        frame.storage = std::shared_ptr<const PlaneBufferSet>(planes);
        for (ImageOrientation orientation : {ImageOrientation::Normal, ImageOrientation::Rotate90Clockwise,
                                             ImageOrientation::Rotate180, ImageOrientation::Rotate270Clockwise}) {
            parameters.orientation = orientation;
            frame.rawParameters = parameters;
            const auto histogram = DisplayHistogramAnalyzer::analyze(frame);
            QCOMPARE(histogram.sampledPixelCount, 16);
            QCOMPARE(histogram.red.bins.at(128), quint64{16});
            QCOMPARE(histogram.green.bins.at(64), quint64{16});
            QCOMPARE(histogram.blue.bins.at(64), quint64{16});
            QCOMPARE(histogram.luma.bins.at(78), quint64{16});
            QCOMPARE(histogram.red.standardDeviation, 0.0);
        }
    }

    for (RawPixelFormat format : {RawPixelFormat::NV12, RawPixelFormat::NV21,
                                  RawPixelFormat::I420, RawPixelFormat::P010}) {
        for (QuantizationRange range : {QuantizationRange::Full, QuantizationRange::Limited}) {
            RawImageParameters parameters;
            parameters.size = {2, 2};
            parameters.format = format;
            parameters.range = range;
            parameters.msbAligned = true;
            const bool tenBit = format == RawPixelFormat::P010;
            const int low = range == QuantizationRange::Full ? 0 : tenBit ? 64 : 16;
            const int high = range == QuantizationRange::Full ? tenBit ? 1023 : 255 : tenBit ? 940 : 235;
            const int center = tenBit ? 512 : 128;
            auto planes = std::make_shared<PlaneBufferSet>();
            const int samples[]{low, high, low, high, center, center};
            for (int value : samples) {
                if (tenBit) {
                    const quint16 stored = static_cast<quint16>(value << 6);
                    planes->storage.append(static_cast<char>(stored & 255));
                    planes->storage.append(static_cast<char>(stored >> 8));
                } else planes->storage.append(static_cast<char>(value));
            }
            planes->planes = tenBit ? QVector<PlaneBuffer>{{0, 4, 8}, {8, 4, 4}}
                : format == RawPixelFormat::I420 ? QVector<PlaneBuffer>{{0, 2, 4}, {4, 1, 1}, {5, 1, 1}}
                : QVector<PlaneBuffer>{{0, 2, 4}, {4, 2, 2}};
            ImageFrame frame;
            frame.storage = std::shared_ptr<const PlaneBufferSet>(planes);
            for (YuvMatrix matrix : {YuvMatrix::BT601, YuvMatrix::BT709, YuvMatrix::BT2020}) {
                parameters.yuvMatrix = matrix;
                frame.rawParameters = parameters;
                const auto histogram = DisplayHistogramAnalyzer::analyze(frame);
                QCOMPARE(histogram.sampledPixelCount, 4);
                for (const HistogramChannel* channel : {&histogram.red, &histogram.green,
                                                        &histogram.blue, &histogram.luma}) {
                    QCOMPARE(channel->bins.at(0), quint64{2});
                    QCOMPARE(channel->bins.at(255), quint64{2});
                    QCOMPARE(channel->mean, 127.5);
                }
            }
            if (!tenBit && range == QuantizationRange::Full) {
                // Asymmetric chroma catches swapped UV and verifies the BT.601 coefficients.
                for (int i = 0; i < 4; ++i) planes->storage[i] = static_cast<char>(128);
                planes->storage[4] = static_cast<char>(format == RawPixelFormat::NV21 ? 255 : 0);
                planes->storage[5] = static_cast<char>(format == RawPixelFormat::NV21 ? 0 : 255);
                parameters.yuvMatrix = YuvMatrix::BT601;
                frame.rawParameters = parameters;
                const auto colored = DisplayHistogramAnalyzer::analyze(frame);
                QCOMPARE(colored.red.bins.at(255), quint64{4});
                QCOMPARE(colored.green.bins.at(81), quint64{4});
                QCOMPARE(colored.blue.bins.at(0), quint64{4});
            }
        }
    }

    RawImageParameters interpolatedParameters;
    interpolatedParameters.size = {4, 2};
    interpolatedParameters.format = RawPixelFormat::NV12;
    interpolatedParameters.range = QuantizationRange::Full;
    interpolatedParameters.yuvMatrix = YuvMatrix::BT601;
    auto interpolatedPlanes = std::make_shared<PlaneBufferSet>();
    interpolatedPlanes->storage = QByteArray(8, static_cast<char>(128));
    interpolatedPlanes->storage.append(QByteArray::fromRawData("\x80\x80\x80\xFF", 4));
    interpolatedPlanes->planes = {{0, 4, 8}, {8, 4, 4}};
    ImageFrame interpolatedFrame;
    interpolatedFrame.rawParameters = interpolatedParameters;
    interpolatedFrame.storage = std::shared_ptr<const PlaneBufferSet>(interpolatedPlanes);
    const auto interpolated = DisplayHistogramAnalyzer::analyze(interpolatedFrame);
    QCOMPARE(interpolated.red.bins.at(128), quint64{2});
    QCOMPARE(interpolated.red.bins.at(173), quint64{2});
    QCOMPARE(interpolated.red.bins.at(255), quint64{4});
    QCOMPARE(interpolated.green.bins.at(128), quint64{2});
    QCOMPARE(interpolated.green.bins.at(105), quint64{2});
    QCOMPARE(interpolated.green.bins.at(60), quint64{2});
    QCOMPARE(interpolated.green.bins.at(37), quint64{2});
    QCOMPARE(interpolated.blue.bins.at(128), quint64{8});
    QCOMPARE(interpolated.luma.bins.at(128), quint64{2});
    QCOMPARE(interpolated.luma.bins.at(121), quint64{2});
    QCOMPARE(interpolated.luma.bins.at(106), quint64{2});
    QCOMPARE(interpolated.luma.bins.at(90), quint64{2});
}

} // namespace ispview

QTEST_GUILESS_MAIN(ispview::CoreTests)
#include "core_tests.moc"
