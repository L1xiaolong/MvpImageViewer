#include "render/bayer_render_parameters.h"
#include "render/yuv_render_parameters.h"

#include <QTest>

namespace ispview {

class RenderTests final : public QObject {
    Q_OBJECT

  private slots:
    void yuvUniformsDescribeMatrixRangeAndLayout();
    void bayerUniformsDescribePackingAndDisplayTransform();
};

void RenderTests::yuvUniformsDescribeMatrixRangeAndLayout() {
    RawImageParameters parameters;
    parameters.format = RawPixelFormat::NV21;
    parameters.yuvMatrix = YuvMatrix::BT709;
    parameters.range = QuantizationRange::Limited;
    auto values = makeYuvRenderUniformData(parameters);
    QCOMPARE(values[0], 1.5748F);
    QCOMPARE(values[4], 16.0F / 255.0F);
    QCOMPARE(values[5], 219.0F / 255.0F);
    QCOMPARE(values[6], 128.0F / 255.0F);
    QCOMPARE(values[7], 224.0F / 255.0F);
    QCOMPARE(values[8], 0.0F);
    QCOMPARE(values[9], 1.0F);

    parameters.format = RawPixelFormat::P010;
    parameters.msbAligned = true;
    parameters.orientation = ImageOrientation::Rotate90Clockwise;
    values = makeYuvRenderUniformData(parameters);
    QCOMPARE(values[4], 16.0F * 4.0F * 64.0F / 65535.0F);
    QCOMPARE(values[6], 512.0F * 64.0F / 65535.0F);
    QCOMPARE(values[9], 0.0F);
    QCOMPARE(values[10], 1.0F);
}

void RenderTests::bayerUniformsDescribePackingAndDisplayTransform() {
    RawImageParameters parameters;
    parameters.size = {8, 6};
    parameters.format = RawPixelFormat::MipiRaw12;
    parameters.rowStride = 16;
    parameters.bayerPattern = BayerPattern::GBRG;
    parameters.bayerSampling = BayerSampling::QuadBayer4x4;
    parameters.blackLevel = 64;
    parameters.whiteLevel = 4095;
    parameters.whiteBalanceGains = {2.0, 1.0, 0.5};
    parameters.demosaic = true;
    parameters.colorCorrectionMatrix = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
    parameters.displayGamma = 2.0;
    parameters.orientation = ImageOrientation::Rotate270Clockwise;
    const auto values = makeBayerRenderUniformData(parameters);
    QCOMPARE(values[0], 8.0F);
    QCOMPARE(values[1], 6.0F);
    QCOMPARE(values[2], 2.0F);
    QCOMPARE(values[3], 1.0F);
    QCOMPARE(values[4], 2.0F);
    QCOMPARE(values[7], 12.0F);
    QCOMPARE(values[8], 64.0F);
    QCOMPARE(values[9], 4095.0F);
    QCOMPARE(values[10], 2.0F);
    QCOMPARE(values[11], 3.0F);
    QCOMPARE(values[15], 1.0F);
    QCOMPARE(values[12], 2.0F);
    QCOMPARE(values[14], 0.5F);
    QCOMPARE(values[16], 1.0F);
    QCOMPARE(values[17], 4.0F);
    QCOMPARE(values[18], 7.0F);
    QCOMPARE(values[20], 2.0F);
    QCOMPARE(values[24], 3.0F);
    QCOMPARE(values[26], 9.0F);
}

} // namespace ispview

QTEST_GUILESS_MAIN(ispview::RenderTests)
#include "render_tests.moc"
