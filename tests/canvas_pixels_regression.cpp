#include "io/raw_image_decoder.h"
#include "io/raw_preset_store.h"
#include "qml/qml_image_canvas.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQuickGraphicsDevice>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <iostream>
#include <stdexcept>

using namespace mvpview;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

QString fixture(const QString& directory) {
    const QString path = directory + QStringLiteral("/fixture_320_240.yuv");
    RawImageParameters raw;
    raw.size = {320, 240};
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "Cannot create YUV fixture");
    const QByteArray bytes(frameByteSize(raw), '\x80');
    check(file.write(bytes) == bytes.size(), "Cannot write YUV fixture");
    return path;
}
} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
    QTemporaryDir temporary;
    try {
        check(temporary.isValid(), "Cannot create fixture directory");
        QStringList paths;
        if (app.arguments().size() > 1) {
            const QDir directory(app.arguments().at(1));
            for (const auto& name : directory.entryList({QStringLiteral("*.yuv")}, QDir::Files, QDir::Name))
                paths.append(directory.absoluteFilePath(name));
            check(!paths.isEmpty(), "Test directory contains no YUV files");
        } else {
            paths.append(fixture(temporary.path()));
        }

        QRhiMetalInitParams parameters;
        std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Metal, &parameters));
        check(bool(rhi), "Metal RHI creation failed (requires GPU access)");
        QQuickRenderControl control;
        QQuickWindow window(&control);
        window.setGeometry(0, 0, 160, 120);
        window.setGraphicsDevice(QQuickGraphicsDevice::fromRhi(rhi.get()));
        check(control.initialize(), "Render control initialization failed");
        std::unique_ptr<QRhiTexture> target(rhi->newTexture(QRhiTexture::RGBA8, QSize(160, 120), 1,
            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        check(target->create(), "Render texture creation failed");
        std::unique_ptr<QRhiTextureRenderTarget> renderTarget(rhi->newTextureRenderTarget(
            QRhiTextureRenderTargetDescription(QRhiColorAttachment(target.get()))));
        std::unique_ptr<QRhiRenderPassDescriptor> pass(renderTarget->newCompatibleRenderPassDescriptor());
        renderTarget->setRenderPassDescriptor(pass.get());
        check(renderTarget->create(), "Render target creation failed");
        window.setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(renderTarget.get()));
        QmlImageCanvas canvas(window.contentItem());
        canvas.setWidth(160);
        canvas.setHeight(120);
        const QColor background(160, 160, 160);
        canvas.setBackgroundColor(background);
        const auto draw = [&] {
            control.beginFrame();
            control.polishItems();
            control.sync();
            control.render();
            control.endFrame();
            app.processEvents();
        };
        int verified = 0;
        const auto verify = [&] {
            QRhiReadbackResult result;
            QRhiCommandBuffer* cb = nullptr;
            check(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess, "Readback frame failed");
            auto* updates = rhi->nextResourceUpdateBatch();
            updates->readBackTexture(QRhiReadbackDescription(target.get()), &result);
            cb->resourceUpdate(updates);
            rhi->endOffscreenFrame();
            check(!result.data.isEmpty(), "GPU pixel readback failed");
            const QImage image(reinterpret_cast<const uchar*>(result.data.constData()),
                result.pixelSize.width(), result.pixelSize.height(), QImage::Format_RGBA8888);
            // Inspect inside every image, away from the comparison separators and letterboxing.
            const int count = canvas.presentationMode() == 0 ? canvas.imageCount() : 1;
            for (int slot = 0; slot < count; ++slot) {
                const int columns = count == 4 ? 2 : count;
                const int rows = count == 4 ? 2 : 1;
                const int x = (2 * (slot % columns) + 1) * image.width() / (2 * columns);
                const int y = (2 * (slot / columns) + 1) * image.height() / (2 * rows);
                bool visible = false;
                for (int dx = -2; dx <= 2; ++dx)
                    visible = visible || image.pixelColor(x + dx, y) != background;
                check(visible, "Decoded image rendered only the canvas background");
                ++verified;
            }
        };
        RawImageDecoder decoder;
        for (const auto& path : paths) {
            const auto raw = RawPresetStore::inferFromFileName(path);
            const auto preview = decoder.decode({path, DecodePurpose::Preview, {256, 256}, raw});
            const auto full = decoder.decode({path, DecodePurpose::Full, {}, raw});
            check(preview.succeeded() && full.succeeded(), "YUV fixture decoding failed");
            // Match asynchronous viewer loading: initialize before the first decode,
            // promote a preview, then update comparison slots one at a time. Existing
            // pipelines must survive changes to other slots' textures and bindings.
            canvas.setFrames({{}}); draw();
            canvas.setFrames({preview.frame}); draw(); verify();
            canvas.setFrames({full.frame}); draw(); verify();
            canvas.setFrames({preview.frame, {}}); draw();
            canvas.setFrames({preview.frame, full.frame}); draw(); verify();
            canvas.setFrames({full.frame, full.frame}); draw(); verify();
            canvas.setPresentationMode(1); draw(); verify();
            canvas.setPresentationMode(0);
            canvas.setFrames({preview.frame, full.frame, preview.frame}); draw(); verify();
            canvas.setFrames({full.frame, preview.frame, full.frame, preview.frame}); draw(); verify();
            canvas.setSmoothDisplay(!canvas.smoothDisplay()); draw(); verify();
            canvas.setFrames({}); draw();
            canvas.setFrames({full.frame}); draw(); verify();
        }
        control.invalidate();
        std::cout << paths.size() << " YUV files; " << verified
                  << " image slots verified through Metal pixel readback\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
