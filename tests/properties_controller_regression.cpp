#include "qml/image_properties_controller.h"
#include "io/default_image_decoder.h"
#include "io/qt_image_decoder.h"
#include "core/display_color_space.h"
#include "io/thumbnail_disk_cache.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>
#include <QFile>
#include <QDir>
#include <atomic>
#include <functional>
#include <iostream>

using namespace mvpview;
namespace {
bool waitFor(const std::function<bool()>& predicate, int timeout = 5000) {
    QElapsedTimer elapsed; elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeout) {
        QCoreApplication::processEvents(); QThread::msleep(1);
    }
    return predicate();
}
QVariantMap findField(const QVariantList& groups, const QString& id) {
    for (const auto& group : groups)
        for (const auto& row : group.toMap().value(QStringLiteral("fields")).toList())
            if (row.toMap().value(QStringLiteral("id")).toString() == id) return row.toMap();
    return {};
}
bool hasGroup(const QVariantList& groups, const QString& id) {
    for (const auto& group : groups)
        if (group.toMap().value(QStringLiteral("id")) == id) return true;
    return false;
}
class DeferredDecoder final : public IImageDecoder {
public:
    mutable QSemaphore gate;
    mutable std::atomic<int> starts{0}, finishes{0};
    bool canDecode(const QString&) const override { return true; }
    DecodeResult decode(const DecodeRequest& request) const override {
        if (request.path.contains(QStringLiteral("slow"))) {
            ++starts;
            if (!gate.tryAcquire(1, 5000)) return {{}, QStringLiteral("Gate timed out")};
        }
        auto frame = std::make_shared<ImageFrame>();
        QImage image(16,12,QImage::Format_RGBA64); image.fill(Qt::gray);
        frame->storage = image;
        frame->descriptor.size = image.size();
        frame->descriptor.validBits = frame->descriptor.storageBits = 16;
        frame->metadata.path = request.path;
        frame->metadata.fileName = QFileInfo(request.path).fileName();
        frame->metadata.sourceSize = image.size();
        frame->metadata.format = QStringLiteral("PNG");
        ImageMetadata::Camera camera;
        camera.make = QStringLiteral("Fixture"); camera.model = QStringLiteral("Camera");
        camera.sensorSize = QSize(32,24); camera.exposureSeconds = 1.0/125;
        frame->metadata.camera = camera;
        ++finishes;
        return {frame,{}}; // Deliberately return even if cancelled, exercising delivery guards.
    }
};
}
#define REQUIRE(condition) do { if (!(condition)) { std::cerr << "Failed at line " << __LINE__ << ": " #condition "\n"; return 1; } } while (false)
int main(int argc, char** argv) {
    QGuiApplication app(argc,argv);
    QTemporaryDir directory(QDir::currentPath() + QStringLiteral("/properties-controller-XXXXXX"));
    REQUIRE(directory.isValid());
    const QString base = directory.path();
    auto path = [&](const char* name) { return base + QLatin1Char('/') + QLatin1String(name); };
    QImage pixels(16,12,QImage::Format_RGB32); pixels.fill(Qt::gray);
    REQUIRE(pixels.save(path("plain.png")));
    REQUIRE(pixels.save(path("plain.jpg")));
    REQUIRE(pixels.save(path("slow.png")));
    REQUIRE(pixels.save(path("slow-clear.png")));
    REQUIRE(pixels.save(path("rich.png")));
    QImage high(16,12,QImage::Format_RGBA64); high.fill(Qt::gray);
    REQUIRE(high.save(path("high.png")));
    auto deferred = std::make_shared<DeferredDecoder>();
    ImageLoader loader(deferred, nullptr, std::make_shared<ThumbnailDiskCache>(path("fake-cache")));
    ImagePropertiesController controller(&loader), other(&loader);
    bool sawMetadataLoading = false;
    QObject::connect(&controller, &ImagePropertiesController::stateChanged, &app, [&] {
        sawMetadataLoading |= controller.metadataLoading();
    });
    controller.loadPath(path("slow.png"));
    REQUIRE(!findField(controller.overviewGroups(),QStringLiteral("path")).isEmpty());
    REQUIRE(controller.loading());
    REQUIRE(waitFor([&] { return deferred->starts == 1; }));
    controller.loadPath(path("rich.png"));
    REQUIRE(waitFor([&] { return !controller.loading() && !controller.metadataLoading(); }));
    REQUIRE(controller.fileName() == QStringLiteral("rich.png"));
    REQUIRE(sawMetadataLoading);
    REQUIRE(hasGroup(controller.overviewGroups(),QStringLiteral("capture")));
    REQUIRE(!findField(controller.advancedGroups(),QStringLiteral("sensor-size")).isEmpty());
    REQUIRE(controller.histogram(0).isEmpty()); // Reading EXIF never starts an analysis.
    controller.requestHistogram(0);
    REQUIRE(waitFor([&] { return controller.histogram(0).value(QStringLiteral("valid")).toBool(); }));
    const auto histogram = controller.histogram(0);
    controller.retranslate();
    REQUIRE(controller.histogram(0) == histogram);
    REQUIRE(controller.hasHistogramSource());
    deferred->gate.release();
    REQUIRE(waitFor([&] { return deferred->finishes >= 2; }));
    REQUIRE(controller.fileName() == QStringLiteral("rich.png"));
    controller.loadPath(path("slow-clear.png"));
    REQUIRE(waitFor([&] { return deferred->starts == 2; }));
    controller.clear(); deferred->gate.release();
    REQUIRE(waitFor([&] { return deferred->finishes >= 3; }));
    REQUIRE(controller.path().isEmpty() && controller.overviewGroups().isEmpty() && controller.histogram(0).isEmpty());
    const int starts = deferred->starts;
    controller.loadPath(path("slow.png"),true);
    controller.loadPath(path("rich.png"),true);
    REQUIRE(waitFor([&] { return !controller.loading() && !controller.metadataLoading(); }));
    REQUIRE(deferred->starts == starts); // Debounce prevents decoding an abandoned selection.
    other.loadPath(base);
    REQUIRE(other.directory()); REQUIRE(!controller.directory());
    REQUIRE(findField(other.overviewGroups(),QStringLiteral("path")).value(QStringLiteral("value")) == QDir::toNativeSeparators(base));

    QtImageDecoder::setPreserveHighBitDepth(true);
    ImageLoader realLoader(createDefaultImageDecoder(), nullptr, std::make_shared<ThumbnailDiskCache>(path("real-cache")));
    ImagePropertiesController real(&realLoader);
    auto load = [&](const QString& target) {
        real.loadPath(target);
        return waitFor([&] { return !real.loading() && !real.metadataLoading(); });
    };
    for (const auto& target : {path("plain.png"),path("plain.jpg"),path("high.png")}) {
        REQUIRE(load(target)); REQUIRE(real.errorText().isEmpty());
        REQUIRE(hasGroup(real.overviewGroups(),QStringLiteral("image")));
        REQUIRE(!hasGroup(real.overviewGroups(),QStringLiteral("capture")));
        real.requestHistogram(1);
        REQUIRE(waitFor([&] { return real.histogram(1).value(QStringLiteral("valid")).toBool(); }));
        const auto native = real.histogram(1);
        REQUIRE(native.value(QStringLiteral("channels")).toList().size() == 4);
        REQUIRE(native.value(QStringLiteral("maximumValue")).toInt() == (target.endsWith(QStringLiteral("high.png")) ? 65535 : 255));
        QStringList names;
        for (const auto& channel : native.value(QStringLiteral("channels")).toList())
            names.append(channel.toMap().value(QStringLiteral("name")).toString());
        REQUIRE(names == QStringList({"Luma", "Red", "Green", "Blue"}));
    }
    REQUIRE(real.hasHistogramSource());
    real.requestHistogram(1);
    REQUIRE(waitFor([&] { return real.histogram(1).value(QStringLiteral("valid")).toBool(); }));
    const auto beforeColorHistogram = real.histogram(1);
    const int beforeColorChange = real.histogramRevision();
    setCurrentDisplayColorSpace(DisplayColorSpace::DisplayP3);
    realLoader.clearCache();
    REQUIRE(waitFor([&] { return !real.loading() && !real.metadataLoading(); }));
    REQUIRE(real.histogramRevision() > beforeColorChange);
    REQUIRE(findField(real.overviewGroups(),QStringLiteral("display-color-space")).value(QStringLiteral("value")).toString().contains(QStringLiteral("P3")));
    REQUIRE(waitFor([&] { return real.histogram(1).value(QStringLiteral("valid")).toBool(); }));
    REQUIRE(real.histogram(1).value(QStringLiteral("channels")) == beforeColorHistogram.value(QStringLiteral("channels")));
    setCurrentDisplayColorSpace(DisplayColorSpace::Srgb); realLoader.clearCache();
    REQUIRE(waitFor([&] { return !real.loading() && !real.metadataLoading(); }));
    for (bool yuv : {false,true}) {
        RawImageParameters parameters;
        parameters.size = QSize(16,12);
        parameters.format = yuv ? RawPixelFormat::NV12 : RawPixelFormat::Raw16;
        parameters.validBitsOverride = yuv ? 0 : 10;
        parameters.demosaic = !yuv;
        const QString target = path(yuv ? "fixture_16_12.yuv" : "fixture_16_12.raw");
        QFile file(target); REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(QByteArray(yuv ? 288 : 384,'\x40')) > 0); file.close();
        realLoader.setRawParameters(target,parameters);
        REQUIRE(load(target)); REQUIRE(real.errorText().isEmpty());
        REQUIRE(hasGroup(real.advancedGroups(),QStringLiteral("raw")));
        REQUIRE(real.hasHistogramSource());
        if (!yuv) REQUIRE(findField(real.advancedGroups(),QStringLiteral("color-matrix")).value(QStringLiteral("matrix")).toList().size() == 9);
        real.requestHistogram(1);
        REQUIRE(waitFor([&] { return real.histogram(1).value(QStringLiteral("valid")).toBool(); }));
        const auto channels = real.histogram(1).value(QStringLiteral("channels")).toList();
        REQUIRE(channels.size() == (yuv ? 3 : 4));
        QStringList names;
        for (const auto& channel : channels) names.append(channel.toMap().value(QStringLiteral("name")).toString());
        REQUIRE(names == (yuv ? QStringList{"Y", "U", "V"} : QStringList{"R", "Gr", "Gb", "B"}));
        const int revision = real.histogramRevision();
        parameters.orientation = ImageOrientation::Rotate180;
        realLoader.setRawParameters(target,parameters);
        REQUIRE(waitFor([&] { return !real.loading() && !real.metadataLoading(); }));
        REQUIRE(real.histogramRevision() > revision);
    }
    QImage gray(16, 12, QImage::Format_Grayscale16); gray.fill(Qt::gray);
    REQUIRE(gray.save(path("gray.png"))); REQUIRE(load(path("gray.png")));
    real.requestHistogram(1);
    REQUIRE(waitFor([&] { return real.histogram(1).value(QStringLiteral("valid")).toBool(); }));
    const auto grayChannels = real.histogram(1).value(QStringLiteral("channels")).toList();
    REQUIRE(grayChannels.size() == 4);
    REQUIRE(grayChannels.first().toMap().value(QStringLiteral("id")) == QStringLiteral("luma"));
    for (const auto& channel : grayChannels)
        REQUIRE(channel.toMap().value(QStringLiteral("mean")) == grayChannels.first().toMap().value(QStringLiteral("mean")));
    REQUIRE(real.histogram(1).value(QStringLiteral("maximumValue")).toInt() == 65535);
    REQUIRE(load(path("missing.png"))); REQUIRE(!real.errorText().isEmpty());
    REQUIRE(!findField(real.overviewGroups(),QStringLiteral("path")).isEmpty());
    REQUIRE(pixels.save(path("missing.png"))); real.reload();
    REQUIRE(waitFor([&] { return !real.loading() && !real.metadataLoading(); }));
    REQUIRE(real.errorText().isEmpty());
    QFile bad(path("broken.png")); REQUIRE(bad.open(QIODevice::WriteOnly)); bad.write("bad image"); bad.close();
    REQUIRE(load(path("broken.png"))); REQUIRE(!real.errorText().isEmpty());
    real.clear(); controller.clear(); other.clear();
    std::cout << "Properties metadata, generations, lazy histograms, JPEG/PNG/high-bit/RAW/YUV and retry passed\n";
    return 0;
}
