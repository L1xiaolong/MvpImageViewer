#include "browser/thumbnail_model.h"
#include "io/default_image_decoder.h"
#include "io/image_transformer.h"
#include "io/raw_preset_store.h"
#include "io/thumbnail_disk_cache.h"
#include "qml/browse_controller.h"
#include <QGuiApplication>
#include <QFile>
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
        streaming(temporary.path());
        modelUpdates(loader);
        cacheConcurrency(temporary.path());
        asyncEditing(loader, temporary.path());
        std::cout << "Streaming, model updates, cache concurrency and asynchronous editing passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
