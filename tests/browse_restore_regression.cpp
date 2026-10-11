#include "qml/browse_controller.h"
#include "qml/thumbnail_image_provider.h"
#include "browser/thumbnail_model.h"
#include "io/default_image_decoder.h"
#include "io/image_loader.h"
#include "io/thumbnail_disk_cache.h"
#include <QGuiApplication>
#include <QFileSystemModel>
#include <QSettings>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QUrl>
#include <QQuickTextureFactory>
#include <iostream>

using namespace mvpview;

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QTemporaryDir temporary(QDir::currentPath() + QStringLiteral("/browse-restore-XXXXXX"));
    if (!temporary.isValid()) return 1;
    QCoreApplication::setOrganizationName(QStringLiteral("MvpViewRegression"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QString directory = temporary.path() + QStringLiteral("/images");
    if (app.arguments().size() > 1) directory = app.arguments().at(1);
    else {
        if (!QDir().mkpath(directory)) return 1;
        QFile file(directory + QStringLiteral("/restore_32_24.yuv"));
        if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray(1152, '\x80')) != 1152) return 1;
    }
    const int expected = int(QDir(directory).entryInfoList({QStringLiteral("*.yuv")}, QDir::Files).size());
    if (expected == 0) return 1;
    QSettings settings;
    settings.setValue(QStringLiteral("browser/lastDirectory"), directory);
    settings.sync();
    auto decoder = createDefaultImageDecoder();
    ImageLoader loader(decoder, nullptr,
        std::make_shared<ThumbnailDiskCache>(temporary.path() + QStringLiteral("/cache")));
    loader.setMemoryBudget(32 * 1024 * 1024);
    QFileSystemModel folders;
    BrowseController browser(&loader, &folders, {}, true);
    QEventLoop loop;
    QTimer deadline; deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer scan;
    QObject::connect(&scan, &QTimer::timeout, &loop, [&] {
        if (browser.thumbnails()->rowCount() == expected) loop.quit();
    });
    scan.start(5); deadline.start(10000);
    browser.restoreInitialDirectoryAsync(); // Same deferred restoration entry as main.cpp.
    loop.exec(); scan.stop();
    if (QDir::cleanPath(browser.currentDirectory()) != QDir::cleanPath(directory) ||
        browser.thumbnails()->rowCount() != expected) return 1;

    ThumbnailImageProvider provider(decoder, &loader);
    QVector<QQuickImageResponse*> responses;
    int completed = 0, failures = 0;
    for (int i = 0; i < expected; ++i) {
        const auto index = browser.thumbnails()->index(i, 0);
        const auto path = browser.thumbnails()->data(index, ThumbnailModel::PathRole).toString();
        auto* response = provider.requestImageResponse(QString::fromLatin1(QUrl::toPercentEncoding(path)), QSize(384, 384));
        responses.append(response);
        QObject::connect(response, &QQuickImageResponse::finished, &loop, [&, response] {
            std::unique_ptr<QQuickTextureFactory> factory(response->textureFactory());
            if (!factory || factory->textureSize().width() > 384 || factory->textureSize().height() > 384 ||
                factory->textureSize().width() * 3 != factory->textureSize().height() * 4) ++failures;
            if (++completed == expected) loop.quit();
        });
    }
    deadline.start(30000); loop.exec();
    for (auto* response : responses) delete response;
    if (completed != expected || failures != 0) return 1;
    std::cout << "Restored saved directory; image provider finished " << completed << " YUV thumbnails\n";
    return 0;
}
