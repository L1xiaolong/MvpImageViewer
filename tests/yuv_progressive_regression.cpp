#include "qml/full_screen_controller.h"
#include "qml/compare_controller.h"
#include "io/thumbnail_disk_cache.h"
#include "io/raw_image_decoder.h"
#include "io/raw_preset_store.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace mvpview;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void waitFor(const std::function<bool()>& ready, int timeoutMs = 5000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeoutMs) { QCoreApplication::processEvents(); QThread::msleep(1); }
    check(ready(), "asynchronous condition timed out");
}
struct Decoder final : IImageDecoder {
    mutable std::atomic_int quick{0}, refined{0}, full{0}, cancelled{0};
    std::atomic_bool hold{false};
    bool canDecode(const QString&) const override { return true; }
    DecodeResult decode(const DecodeRequest& request) const override {
        const bool detail = request.purpose == DecodePurpose::Full;
        if (detail) ++full;
        else if (request.maximumSize.width() <= 512) ++quick;
        else {
            ++refined;
            while (hold && !request.isCancelled()) QThread::msleep(1);
            if (request.isCancelled()) { ++cancelled; return {{}, "Cancelled"}; }
        }
        auto frame = std::make_shared<ImageFrame>();
        frame->metadata.path = request.path;
        frame->metadata.sourceSize = {8000, 6000};
        frame->descriptor.size = detail ? QSize(8000, 6000) : request.maximumSize;
        frame->rawParameters = request.rawParameters;
        frame->sourceSamplesPending = !detail;
        QImage image(16, 16, QImage::Format_RGBA8888); image.fill(Qt::red); frame->storage = image;
        return {frame, {}};
    }
};
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir root(QDir::currentPath() + "/progressive-XXXXXX");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
    QCoreApplication::setOrganizationName("YuvProgressiveRegression");
    try {
        if (app.arguments().size() > 1) {
            const QStringList paths = app.arguments().mid(1, 4);
            ImageLoader loader(std::make_shared<RawImageDecoder>(), nullptr,
                               std::make_shared<ThumbnailDiskCache>(root.path() + "/cache"));
            for (const auto& path : paths) {
                auto parameters = RawPresetStore::inferFromFileName(path);
                parameters.format = RawPixelFormat::NV12;
                loader.setRawParameters(path, parameters);
            }
            CompareController compare(&loader);
            compare.setHistogramVisible(false);
            compare.setPixelValueVisible(false);
            QVector<bool> full(paths.size(), false);
            QObject::connect(&compare, &CompareController::frameChanged, [&](int slot, bool exact) {
                full[slot] = exact;
            });
            QElapsedTimer timer; timer.start();
            compare.setPaths(paths);
            waitFor([&] {
                for (int slot = 0; slot < paths.size(); ++slot)
                    if (!compare.frame(slot)) return false;
                return true;
            }, 60000);
            compare.actualPixelsAll();
            waitFor([&] { return std::all_of(full.cbegin(), full.cend(), [](bool v) { return v; }); }, 60000);
            std::cout << "compare full frames " << timer.elapsed() << " ms\n";
            timer.restart();
            compare.setHistogramVisible(true);
            for (int slot = 0; slot < paths.size(); ++slot) compare.requestHistogram(slot);
            waitFor([&] {
                for (int slot = 0; slot < paths.size(); ++slot)
                    if (!compare.histogram(slot).value("valid").toBool()) return false;
                return true;
            }, 60000);
            std::cout << "compare " << paths.size() << " histograms " << timer.elapsed() << " ms\n";
            const int revision = compare.histogramRevision();
            compare.setHistogramVisible(false);
            timer.restart();
            compare.setHistogramVisible(true);
            for (int slot = 0; slot < paths.size(); ++slot) compare.requestHistogram(slot);
            check(compare.histogramRevision() == revision, "cached histogram restarted");
            std::cout << "compare cached reopen " << timer.elapsed() << " ms\n";
            return 0;
        }
        const QString path = root.path() + "/sample.yuv";
        QFile file(path); check(file.open(QIODevice::WriteOnly), "fixture failed"); file.write("x"); file.close();
        auto decoder = std::make_shared<Decoder>();
        ImageLoader loader(decoder, nullptr, std::make_shared<ThumbnailDiskCache>(root.path() + "/cache"));
        RawImageParameters p; p.size = {8000, 6000}; loader.setRawParameters(path, p);
        FullScreenController screen(&loader);
        decoder->hold = true;
        screen.open({path}, 0);
        waitFor([&] { return !screen.loading() && decoder->refined > 0; });
        check(decoder->quick == 1, "fullscreen did not show quick preview first");
        screen.actualPixels();
        waitFor([&] { return decoder->full > 0 && decoder->cancelled > 0; });
        screen.closeSession(); decoder->hold = false;
        QCoreApplication::processEvents(); loader.clearCache();
        CompareController compare(&loader);
        compare.setPixelValueVisible(false); compare.setHistogramVisible(false);
        int shown = 0;
        QObject::connect(&compare, &CompareController::frameChanged, [&](int, bool) { ++shown; });
        compare.setPaths({path});
        waitFor([&] { return shown >= 2; });
        check(decoder->quick >= 2, "compare skipped quick stage");
        compare.setHistogramVisible(true);
        compare.requestHistogram(0);
        waitFor([&] { return compare.histogram(0).value("valid").toBool(); });
        const auto histogram = compare.histogram(0);
        const auto channel = histogram.value("channels").toList().first().toMap();
        check(channel.value("bins").toList().at(54).toULongLong() == 256,
              "compare luma differs from displayed red pixels");
        compare.setHistogramVisible(false);
        compare.setHistogramVisible(true);
        compare.requestHistogram(0);
        check(compare.histogram(0) == histogram, "compare did not reuse completed histogram");
        compare.setHistogramVisible(false);
        compare.setPaths({}); loader.clearCache();
        decoder->hold = true; shown = 0;
        const int previous = decoder->refined;
        compare.setPaths({path});
        waitFor([&] { return shown == 1 && decoder->refined > previous; });
        compare.setPaths({}); decoder->hold = false;
        QElapsedTimer settle; settle.start();
        while (settle.elapsed() < 100) { QCoreApplication::processEvents(); QThread::msleep(1); }
        check(shown == 1, "late preview reappeared after closing compare");
        std::cout << "Fullscreen/compare quick, refined, exact and cancellation paths passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
