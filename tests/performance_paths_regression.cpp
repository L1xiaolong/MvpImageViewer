#include "browser/thumbnail_model.h"
#include "browser/thumbnail_filter_proxy_model.h"
#include "io/default_image_decoder.h"
#include "io/image_transformer.h"
#include "io/raw_preset_store.h"
#include "io/thumbnail_disk_cache.h"
#include "qml/browse_controller.h"
#include "qml/thumbnail_image_provider.h"
#include <QQuickTextureFactory>
#include <QUrl>
#include <QGuiApplication>
#include <QFile>
#include <QLockFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <QPersistentModelIndex>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mvpview;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
QByteArray read(const QString& path) {
    QFile file(path); check(file.open(QIODevice::ReadOnly), "read failed"); return file.readAll();
}
void write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error((path + ": " + file.errorString()).toStdString());
}

void streaming(const QString& root) {
    RawImageParameters p; p.size = {4, 2}; p.headerOffset = 5;
    const QByteArray header("HEAD!");
    const QByteArray trailer("END");
    QByteArray original = header, rotated = header;
    for (int frame = 0; frame < 257; ++frame) {
        QByteArray pixels;
        for (int i = 0; i < 12; ++i) pixels.append(char((i + frame) % 256));
        original += pixels;
        for (int i : {4, 0, 5, 1, 6, 2, 7, 3, 8, 9, 10, 11}) rotated += pixels.at(i);
    }
    original += trailer; rotated += trailer;
    const QString path = root + "/sequence.yuv";
    write(path, original);
    check(ImageTransformer::rotate(path, QuarterTurn::Clockwise, p).isEmpty(), "rotation failed");
    check(read(path) == rotated, "multi-frame rotation/header/trailer mismatch");
    const auto saved = RawPresetStore::loadForFile(path);
    check(saved && saved->size == QSize(2, 4), "rotated sidecar dimensions wrong");
    check(ImageTransformer::restore(path).isEmpty(), "restore failed");
    check(read(path) == original, "restore differs from original");
    check(ImageTransformer::resize(path, p.size, p).isEmpty(), "identity resize failed");
    check(read(path) == original, "identity resize changed frames/header/trailer");
    check(ImageTransformer::restore(path).isEmpty(), "second restore failed");

    // Restore also crosses the fixed copying block boundary.
    const QString large = root + "/large.yuv";
    const QByteArray backup(3 * 1024 * 1024 + 17, '\x57');
    write(large, "short"); write(ImageTransformer::backupPath(large), backup);
    write(ImageTransformer::backupManifestPath(large), "{}");
    check(ImageTransformer::restore(large).isEmpty() && read(large) == backup, "chunked restore failed");

    for (const auto format : {RawPixelFormat::NV12, RawPixelFormat::NV21, RawPixelFormat::I420,
                             RawPixelFormat::P010, RawPixelFormat::MipiRaw10,
                             RawPixelFormat::MipiRaw12, RawPixelFormat::Raw16}) {
        RawImageParameters layout; layout.size = {4, 4}; layout.format = format; layout.headerOffset = 5;
        QByteArray samples(frameByteSize(layout) * 9, Qt::Uninitialized);
        for (qsizetype i = 0; i < samples.size(); ++i)
            samples[i] = format == RawPixelFormat::P010 && i % 2 ? 0 : char(i % 256);
        const QByteArray contents = header + samples + "!";
        const QString file = root + "/roundtrip-" + QString::number(int(format)) + (layout.isYuv() ? ".yuv" : ".raw");
        write(file, contents);
        check(ImageTransformer::rotate(file, QuarterTurn::Clockwise, layout).isEmpty(), "format rotation failed");
        auto turned = RawPresetStore::loadForFile(file);
        check(turned.has_value(), "format sidecar missing");
        check(ImageTransformer::rotate(file, QuarterTurn::CounterClockwise, turned).isEmpty(), "inverse rotation failed");
        check(read(file) == contents, "RAW/YUV roundtrip pixels differ");
        check(ImageTransformer::restore(file).isEmpty() && read(file) == contents, "format backup restore failed");
    }
}

void modelUpdates(ImageLoader& loader) {
    ThumbnailModel model(&loader);
    QVector<ImageFileRecord> records;
    for (int i = 0; i < 10000; ++i) {
        ImageFileRecord record; record.path = QString::number(i); record.fileName = record.path;
        records.append(record);
    }
    model.setFiles(records);
    model.setSelectedPaths({"9999"});
    emit loader.thumbnailMetadataReady("9999", {640, 480}, 12);
    int resets = 0, removals = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [&] { ++removals; });
    QPersistentModelIndex survivor(model.index(9999));
    records.remove(100, 100);
    model.updateFiles(records);
    check(resets == 0 && removals == 1 && survivor.isValid(), "contiguous removal invalidated persistent indices");
    check(survivor.data(ThumbnailModel::PathRole).toString() == "9999", "survivor changed");
    QVector<ImageFileRecord> scattered;
    for (int i = 0; i < records.size(); ++i) if (i % 20) scattered.append(records.at(i));
    model.updateFiles(scattered);
    check(resets == 1, "scattered removals did not take bounded reset path");
    for (int row = 0; row < scattered.size(); ++row)
        check(model.rowForPath(scattered.at(row).path) == row, "path index stale");
    const auto selected = model.index(model.rowForPath("9999"));
    check(selected.data(ThumbnailModel::SelectedRole).toBool(), "selection lost");
    check(selected.data(ThumbnailModel::DimensionsRole).toSize() == QSize(640, 480), "metadata lost");
}

void cacheConcurrency(const QString& root) {
    ThumbnailDiskCache cache(root + "/parallel-cache");
    QImage pixels(96, 64, QImage::Format_RGBA8888); pixels.fill(QColor(12, 34, 56, 90));
    std::vector<std::future<void>> jobs;
    for (int worker = 0; worker < 6; ++worker) jobs.push_back(std::async(std::launch::async, [&, worker] {
        for (int i = 0; i < 16; ++i) {
            const QString key = QString::number(worker) + ':' + QString::number(i);
            check(cache.store(key, pixels), "concurrent store failed");
            check(cache.load(key) == pixels, "concurrent cache pixels differ");
            (void)cache.load(QString::number((worker + 1) % 6) + ':' + QString::number(i));
        }
    }));
    for (auto& job : jobs) job.get();
    for (const bool transparent : {false, true}) {
        const QString key = transparent ? "broken-png" : "broken-jpeg";
        QImage source(96, 64, QImage::Format_RGBA8888);
        source.fill(QColor(12, 34, 56, transparent ? 90 : 255));
        const QString path = cache.pathForKey(key) + (transparent ? ".png" : ".jpg");
        write(path, "truncated");
        check(cache.load(key).isNull(), "corrupt cache accepted");
        ThumbnailDiskCache second(cache.rootDirectory());
        {
            QLockFile writerLock(cache.pathForKey(key) + ".lock");
            check(writerLock.tryLock(0), "fixture lock failed");
            check(!second.store(key, source), "repair ignored active writer");
            check(read(path) == "truncated", "active writer bytes removed");
        }
        check(second.store(key, source, {6000, 4000}, 12), "corrupt cache repair failed");
        const auto repaired = cache.load(key);
        check(!repaired.isNull() && repaired.text("mvpview.validBits") == "12", "repaired cache unreadable");
        if (transparent) check(repaired == source, "repaired pixels changed");
    }
    // Writer metadata must round-trip for both cache formats without changing
    // source pixels/text shared with the live thumbnail.
    for (const bool transparent : {false, true}) {
        QImage source(96, 64, QImage::Format_RGBA8888);
        source.fill(QColor(12, 34, 56, transparent ? 90 : 255));
        const QImage shared = source;
        const auto* data = source.constBits();
        const QString key = transparent ? "metadata-png" : "metadata-jpeg";
        check(cache.store(key, source, QSize(6000, 4000), 12), "metadata store failed");
        const QImage decoded = cache.load(key);
        check(!decoded.isNull() && decoded.size() == source.size(), "metadata cache load failed");
        check(decoded.text("mvpview.sourceWidth") == "6000" &&
              decoded.text("mvpview.sourceHeight") == "4000" &&
              decoded.text("mvpview.validBits") == "12", "cache metadata lost");
        check(source.constBits() == data && shared.constBits() == data && source.textKeys().isEmpty(),
              "cache writer mutated shared source");
        if (transparent) check(decoded == source, "transparent cache pixels changed");
    }
}

void asyncEditing(ImageLoader& loader, const QString& root) {
    const QString path = root + "/editable.png";
    QImage image(800, 600, QImage::Format_RGBA8888); image.fill(Qt::red);
    check(image.save(path), "image fixture failed");
    QFileSystemModel folders;
    BrowseController browser(&loader, &folders, root, false);
    BrowseController other(&loader, &folders, root, false);
    browser.selectPath(path); other.selectPath(path);
    QEventLoop loop; QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    bool completed = false; QString error;
    QObject::connect(&browser, &BrowseController::transformFinished, &loop, [&](const QString& done, const QString& failure) {
        check(done == path, "completion path changed"); completed = true; error = failure; loop.quit();
    });
    check(browser.resizeSelected(1200, 900).isEmpty(), "async resize not accepted");
    check(browser.transformBusy() && !completed, "resize completed synchronously");
    check(!other.rotateSelectedClockwise().isEmpty(), "cross-pane duplicate admitted");
    check(!other.renameSelectedTo("renamed.png").isEmpty(), "active image renamed");
    check(!other.moveSelectedToTrashConfirmed().isEmpty(), "active image trashed");
    int heartbeats = 0; QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; }); heartbeat.start(1);
    timeout.start(15000); loop.exec();
    check(completed && error.isEmpty() && !browser.transformBusy(), "async completion failed");
    check(heartbeats > 0, "GUI event loop stalled");
    check(QImage(path).size() == QSize(1200, 900), "resize output wrong");
    completed = false;
    check(browser.restoreSelected().isEmpty(), "async restore not accepted");
    timeout.start(15000); loop.exec();
    check(completed && error.isEmpty() && QImage(path) == image, "async restore mismatch");
    completed = false;
    check(browser.resizeSelected(20000, 20000).isEmpty(), "failure request not accepted");
    timeout.start(15000); loop.exec();
    check(completed && !error.isEmpty() && browser.canTransform(), "error did not release busy state");
    check(QImage(path) == image, "failed operation modified image");
}

class GalleryDecoder : public IImageDecoder {
public:
    mutable std::atomic_int previews{0};
    std::shared_ptr<const IImageDecoder> decoder = createDefaultImageDecoder();
    bool canDecode(const QString& path) const override { return decoder->canDecode(path); }
    DecodeResult decode(const DecodeRequest& request) const override {
        if (request.purpose == DecodePurpose::Preview) ++previews;
        return decoder->decode(request);
    }
};

void galleryPreview(const QString& root) {
    const QString path = root + "/gallery_32_24.yuv";
    write(path, QByteArray(1152, '\x80'));
    auto decoder = std::make_shared<GalleryDecoder>();
    ImageLoader loader(decoder, nullptr, std::make_shared<ThumbnailDiskCache>(root + "/gallery-cache"));
    QFileSystemModel folders;
    BrowseController browser(&loader, &folders, root, false);
    ThumbnailImageProvider provider(decoder, &loader);
    QEventLoop loop;
    QTimer deadline; deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    browser.setGalleryPath(path);
    auto* response = provider.requestImageResponse(
        QString::fromLatin1(QUrl::toPercentEncoding(path)) + "?purpose=gallery", {2048, 2048});
    bool ready = false;
    QObject::connect(response, &QQuickImageResponse::finished, &loop, [&] {
        std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
        ready = texture && texture->textureSize() == QSize(32, 24);
        loop.quit();
    });
    deadline.start(10000); loop.exec();
    delete response;
    check(ready && browser.galleryImageReady(), "gallery preview did not reach both consumers");
    check(decoder->previews == 1, "gallery information and display decoded RAW preview twice");
    browser.setGalleryPath({});
    loader.clearCache();
    // The selected image can remain open after its strip card scrolls away.
    loader.updateViewport("gallery-strip", {{root + "/other.png", 60}}, false);
    ready = false;
    response = provider.requestImageResponse(
        QString::fromLatin1(QUrl::toPercentEncoding(path)) + "?purpose=gallery-thumbnail", {320, 240});
    QObject::connect(response, &QQuickImageResponse::finished, &loop, [&] {
        std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
        ready = texture && texture->textureSize() == QSize(32, 24);
        loop.quit();
    });
    deadline.start(10000); loop.exec();
    delete response;
    check(ready, "gallery thumbnail stalled outside the strip viewport");
    check(decoder->previews == 1, "gallery placeholder requested a large preview");
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir temporary(QDir::currentPath() + QStringLiteral("/performance-paths-XXXXXX"));
    QCoreApplication::setOrganizationName("MvpPerformanceRegression");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    try {
        check(temporary.isValid(), "temporary directory unavailable");
        ImageLoader loader(createDefaultImageDecoder(), nullptr,
                           std::make_shared<ThumbnailDiskCache>(temporary.path() + "/cache"));
        if (app.arguments().contains("--benchmark-refresh")) {
            for (int iteration = 0; iteration < 3; ++iteration) {
                ThumbnailModel model(&loader);
                ThumbnailFilterProxyModel proxy;
                proxy.setSourceModel(&model);
                QVector<ImageFileRecord> records;
                records.reserve(100000);
                for (int i = 0; i < 100000; ++i) {
                    ImageFileRecord record;
                    record.path = QStringLiteral("/fixture/image-%1.png").arg(i);
                    record.fileName = QStringLiteral("image-%1.png").arg(i);
                    records.append(record);
                }
                model.setFiles(records);
                (void)proxy.rowCount();
                QElapsedTimer timer; timer.start();
                model.updateFiles(records);
                std::cout << "refresh unchanged 100000: " << double(timer.nsecsElapsed())/1e6 << " ms\n";
                records[50000].fileSize = 123;
                timer.restart(); model.updateFiles(records);
                std::cout << "refresh one change 100000: " << double(timer.nsecsElapsed())/1e6 << " ms\n";
                QVector<ImageFileRecord> remaining;
                for (int i = 0; i < records.size(); ++i) if (i % 20) remaining.append(records[i]);
                timer.restart(); model.updateFiles(remaining);
                std::cout << "refresh scattered deletion 5%: " << double(timer.nsecsElapsed())/1e6 << " ms\n";
                check(proxy.rowCount() == 95000, "benchmark proxy count wrong");
            }
            return 0;
        }
        streaming(temporary.path());
        modelUpdates(loader);
        cacheConcurrency(temporary.path());
        asyncEditing(loader, temporary.path());
        galleryPreview(temporary.path());
        std::cout << "Streaming, model updates, cache concurrency, asynchronous editing and gallery preview sharing passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
