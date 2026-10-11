#include "qml/qml_image_canvas.h"
#include <QGuiApplication>
#include <QQuickGraphicsDevice>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QQuickRenderTarget>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <iostream>

using namespace mvpview;

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    // Select the RHI scene graph; the supplied Null device keeps the test headless.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QRhiNullInitParams parameters;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Null, &parameters));
    if (!rhi) { std::cerr << "Null RHI creation failed\n"; return 1; }
    QQuickRenderControl control;
    QQuickWindow window(&control);
    window.setGeometry(0, 0, 160, 120);
    window.setGraphicsDevice(QQuickGraphicsDevice::fromRhi(rhi.get()));
    if (!control.initialize()) { std::cerr << "Render control initialization failed\n"; return 1; }
    std::unique_ptr<QRhiTexture> target(rhi->newTexture(QRhiTexture::RGBA8, QSize(160, 120), 1,
        QRhiTexture::RenderTarget));
    if (!target->create()) return 1;
    std::unique_ptr<QRhiTextureRenderTarget> renderTarget(rhi->newTextureRenderTarget(
        QRhiTextureRenderTargetDescription(QRhiColorAttachment(target.get()))));
    std::unique_ptr<QRhiRenderPassDescriptor> pass(renderTarget->newCompatibleRenderPassDescriptor());
    renderTarget->setRenderPassDescriptor(pass.get());
    if (!renderTarget->create()) return 1;
    window.setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(renderTarget.get()));

    QmlImageCanvas canvas(window.contentItem());
    canvas.setWidth(160); canvas.setHeight(120);
    auto frame = std::make_shared<ImageFrame>();
    RawImageParameters raw;
    raw.size = {rhi->resourceLimit(QRhi::TextureSizeMax) + 1, 64};
    frame->rawParameters = raw;
    frame->descriptor.size = raw.size;
    frame->metadata.sourceSize = raw.size;
    auto planes = std::make_shared<PlaneBufferSet>();
    const qsizetype ySize = minimumRowStride(raw) * raw.size.height();
    const qsizetype uvSize = minimumChromaRowStride(raw) * (raw.size.height() / 2);
    planes->storage = QByteArray(ySize + uvSize, '\x80');
    planes->planes = {{0, minimumRowStride(raw), ySize},
                      {ySize, minimumChromaRowStride(raw), uvSize}};
    planes->displayImage = QImage(16, 16, QImage::Format_RGBA8888);
    planes->displayImage.fill(Qt::gray);
    frame->storage = std::shared_ptr<const PlaneBufferSet>(planes);
    auto ledger = std::make_shared<PixelMemoryLedger>();
    PixelStorageFootprint footprint; frame->appendPixelStorage(footprint);
    frame->pixelOwnership.attach(ledger, std::move(footprint));
    int rendered = 0;
    QObject::connect(&canvas, &QmlImageCanvas::imageFrameRendered, &app, [&] { ++rendered; });
    canvas.setFrames({frame});
    // Exercise native YUV rejection, CPU fallback, and a second resource rebuild.
    for (int i = 0; i < 2; ++i) {
        canvas.setSmoothDisplay(i != 0);
        control.beginFrame(); control.polishItems(); control.sync(); control.render(); control.endFrame();
        app.processEvents();
    }
    const auto gpu = ledger->gpuResources()->bytes();
    control.invalidate();
    if (rendered == 0 || gpu == 0 || gpu > 256 * 1024) {
        std::cerr << "Oversized YUV did not render with bounded fallback: " << gpu << '\n';
        return 1;
    }
    std::cout << "Oversized YUV rejected; CPU fallback rendered, nominal GPU bytes: " << gpu << '\n';
    return 0;
}
