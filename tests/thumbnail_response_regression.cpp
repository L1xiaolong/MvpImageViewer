#include "qml/thumbnail_image_provider.h"
#include "io/default_image_decoder.h"
#include "io/image_loader.h"
#include "io/thumbnail_disk_cache.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QThread>
#include <QCoreApplication>
#include <QEvent>
#include <QUrl>
#include <QQuickTextureFactory>
#include <iostream>

using namespace mvpview;

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QTemporaryDir temporary(QDir::currentPath() + QStringLiteral("/thumbnail-response-XXXXXX"));
    if (!temporary.isValid()) return 1;
    QCoreApplication::setOrganizationName(QStringLiteral("MvpViewRegression"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    const QString path = temporary.path() + QStringLiteral("/sample_32_24.yuv");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray(1152, '\x80')) != 1152) return 1;
    file.close();
    auto decoder = createDefaultImageDecoder();
    ImageLoader loader(decoder, nullptr,
        std::make_shared<ThumbnailDiskCache>(temporary.path() + QStringLiteral("/cache")));
    ThumbnailImageProvider provider(decoder, &loader);
    const QString id = QString::fromLatin1(QUrl::toPercentEncoding(path));
    QEventLoop loop;
    QTimer deadline; deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    int completed = 0, failures = 0;
    const auto observe = [&](QQuickImageResponse* response, bool cancelled) {
        if (response->thread() != loader.thread()) ++failures;
        QObject::connect(response, &QQuickImageResponse::finished, &app,
            [&, response, cancelled] {
                if (QThread::currentThread() != loader.thread()) ++failures;
                if (!cancelled) {
                    std::unique_ptr<QQuickTextureFactory> factory(response->textureFactory());
                    if (!factory || factory->textureSize() != QSize(32, 24)) ++failures;
                }
                // Deliberately reclaim immediately. The response must not touch
                // itself after finished(), including on synchronous cache hits.
                delete response;
                ++completed;
            }, Qt::DirectConnection);
    };
    auto* cold = provider.requestImageResponse(id, QSize(384, 384));
    observe(cold, false);
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (completed == 1) loop.quit(); });
    poll.start(1); deadline.start(10000); loop.exec(); poll.stop();
    if (completed != 1 || failures) return 1;
    std::cout << "Cold thumbnail completion passed; starting cache-hit lifetime stress\n" << std::flush;

    // Emulate Qt's separate image-reader thread while the GUI handles cache
    // hits. First cancel half of the requests before start().
    QThread reader;
    QObject host;
    host.moveToThread(&reader);
    reader.start();
    constexpr int count = 1000;
    for (int i = 0; i < count; ++i) {
        QQuickImageResponse* response = nullptr;
        QMetaObject::invokeMethod(&host, [&] {
            response = provider.requestImageResponse(id, QSize(384, 384));
            observe(response, i % 2 == 0);
            if (i % 2 == 0) response->cancel();
        }, Qt::BlockingQueuedConnection);
    }
    QObject::disconnect(&poll, nullptr, &loop, nullptr);
    QObject::connect(&poll, &QTimer::timeout, &loop,
        [&] { if (completed == count + 1) loop.quit(); });
    poll.start(1); deadline.start(10000); loop.exec(); poll.stop();
    if (completed == count + 1 && failures == 0) {
        QVector<QQuickImageResponse*> pending;
        constexpr int lateCancelCount = 200;
        QMetaObject::invokeMethod(&host, [&] {
            for (int i = 0; i < lateCancelCount; ++i) {
                auto* response = provider.requestImageResponse(id, QSize(384, 384));
                pending.append(response);
                observe(response, true);
            }
        }, Qt::BlockingQueuedConnection);
        // Run the already queued starts; completions posted by these starts
        // remain queued. Now cancel after the handles have been installed.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QMetaObject::invokeMethod(&host, [&] {
            for (auto* response : pending) response->cancel();
        }, Qt::BlockingQueuedConnection);
        QObject::disconnect(&poll, nullptr, &loop, nullptr);
        QObject::connect(&poll, &QTimer::timeout, &loop,
            [&] { if (completed == count + lateCancelCount + 1) loop.quit(); });
        poll.start(1); deadline.start(10000); loop.exec(); poll.stop();
    }
    QMetaObject::invokeMethod(&host, [&] { host.moveToThread(app.thread()); }, Qt::BlockingQueuedConnection);
    reader.quit(); reader.wait();
    if (completed != count + 201 || failures) return 1;
    std::cout << "1200 image-reader requests: GUI lifetime, cache-hit completion and early/late cancellation passed\n";
    return 0;
}
