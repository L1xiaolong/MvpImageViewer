#include "qml/raw_parameters_controller.h"
#include "io/default_image_decoder.h"
#include "io/image_loader.h"
#include "io/raw_preset_store.h"
#include "io/thumbnail_disk_cache.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
using namespace mvpview;
#define REQUIRE(condition) do { if (!(condition)) { std::cerr << "Failed at line " << __LINE__ << ": " #condition "\n"; return 1; } } while (false)
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir temporary(QDir::currentPath() + QStringLiteral("/raw-parameters-XXXXXX"));
    REQUIRE(temporary.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("MvpRawParametersTests"));
    QCoreApplication::setApplicationName(QStringLiteral("RawParametersTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    const QString first = temporary.filePath(QStringLiteral("first.raw"));
    const QString second = temporary.filePath(QStringLiteral("second.raw"));
    const QString yuv = temporary.filePath(QStringLiteral("frame.yuv"));
    for (const auto& path : {first, second, yuv}) {
        QFile file(path); REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(QByteArray(1024, '\x40')) == 1024);
    }
    ImageLoader loader(createDefaultImageDecoder(),nullptr,
        std::make_shared<ThumbnailDiskCache>(temporary.filePath(QStringLiteral("cache"))));
    RawImageParameters initial;
    initial.size = QSize(16,12); initial.format = RawPixelFormat::Raw16;
    initial.validBitsOverride = 10; initial.demosaic = true;
    loader.setRawParameters(first,initial); loader.setRawParameters(second,initial);
    RawImageParameters yuvParameters; yuvParameters.size = QSize(16,12);
    loader.setRawParameters(yuv,yuvParameters);
    RawParametersController controller(&loader);
    int errors = 0;
    QObject::connect(&controller,&RawParametersController::notificationRequested,&app,
        [&](const QString&,bool error) { if (error) ++errors; });
    controller.loadPath(first);
    controller.setValue(QStringLiteral("width"),8);
    controller.loadPath(second); // Flushes the outgoing file before changing the path.
    REQUIRE(loader.rawParameters(first)->size.width() == 8);
    REQUIRE(loader.rawParameters(second)->size.width() == 16);
    QElapsedTimer wait; wait.start();
    while (wait.elapsed() < 200) { QCoreApplication::processEvents(); QThread::msleep(1); }
    REQUIRE(loader.rawParameters(second)->size.width() == 16);
    controller.setListValue(QStringLiteral("whiteBalance"),0,1.5);
    controller.clear();
    REQUIRE(controller.path().isEmpty() && controller.values().isEmpty());
    REQUIRE(loader.rawParameters(second)->whiteBalanceGains[0] == 1.5);
    controller.loadPath(first);
    controller.setValue(QStringLiteral("headerOffset"),4096);
    controller.finishPendingChanges();
    REQUIRE(errors == 1);
    REQUIRE(loader.rawParameters(first)->headerOffset == 0);
    controller.setValue(QStringLiteral("headerOffset"),0);
    controller.setValue(QStringLiteral("width"),16);
    controller.finishPendingChanges();
    REQUIRE(controller.savePreset(QStringLiteral("fixture")).isEmpty());
    REQUIRE(controller.presetNames().contains(QStringLiteral("fixture")));
    controller.setValue(QStringLiteral("displayGamma"),2.4);
    controller.finishPendingChanges();
    controller.selectPreset(QStringLiteral("fixture"));
    REQUIRE(loader.rawParameters(first)->displayGamma == initial.displayGamma);
    REQUIRE(controller.deleteSelectedPreset().isEmpty());
    REQUIRE(!controller.presetNames().contains(QStringLiteral("fixture")));
    controller.setValue(QStringLiteral("width"),8);
    REQUIRE(controller.applyToFolder().isEmpty());
    REQUIRE(loader.rawParameters(first)->size.width() == 8);
    REQUIRE(loader.rawParameters(second)->size.width() == 8);
    REQUIRE(loader.rawParameters(yuv)->size.width() == 16);
    REQUIRE(RawPresetStore::loadForFile(first)->size.width() == 8);
    controller.loadPath(yuv);
    REQUIRE(controller.yuvFormat());
    controller.setValue(QStringLiteral("format"),int(RawPixelFormat::P010));
    controller.finishPendingChanges();
    REQUIRE(controller.endianControlsVisible());
    REQUIRE(loader.rawParameters(yuv)->format == RawPixelFormat::P010);
    controller.setValue(QStringLiteral("littleEndian"),false);
    controller.clear();
    REQUIRE(!loader.rawParameters(yuv)->littleEndian);
    REQUIRE(controller.path().isEmpty());
    std::cout << "RAW/YUV switch/close commits, invalid layouts, presets and folder application passed\n";
    return 0;
}
