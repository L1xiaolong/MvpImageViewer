// Reproducible production-decoder benchmark; intentionally not a timing-sensitive CTest.
#include "io/default_image_decoder.h"
#include "io/image_loader.h"
#include "io/thumbnail_disk_cache.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <iostream>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
using namespace mvpview;

void settle(int ms = 200) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < ms) { QCoreApplication::processEvents(); QThread::msleep(1); }
}
void snapshot(const char* phase, const ImageLoader& loader, qint64 elapsed = 0) {
    QJsonObject row{{"phase", phase}, {"elapsedMs", elapsed},
        {"pixelBytes", qint64(loader.residentPixelBytes())}, {"cacheBytes", qint64(loader.cachedBytes())}};
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{}; counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
        throw std::runtime_error("GetProcessMemoryInfo failed");
    row["privateBytes"] = qint64(counters.PrivateUsage);
    row["workingSetBytes"] = qint64(counters.WorkingSetSize);
    row["peakWorkingSetBytes"] = qint64(counters.PeakWorkingSetSize);
#endif
    std::cout << QJsonDocument(row).toJson(QJsonDocument::Compact).constData() << std::endl;
}
ImageFramePtr load(ImageLoader& loader, const DecodeRequest& request) {
    bool done = false;
    DecodeResult result;
    auto handle = loader.request(1, request, [&](quint64, const DecodeResult& r) { result = r; done = true; });
    QElapsedTimer timer; timer.start();
    while (!done && timer.elapsed() < 30000) { QCoreApplication::processEvents(); QThread::msleep(1); }
    if (!done || !result.frame) throw std::runtime_error("decode failed or timed out");
    return result.frame;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        if (app.arguments().size() < 3) throw std::runtime_error("usage: memory_benchmark <generate|jpeg|png16> <fixture-directory>");
        const QString mode = app.arguments()[1], root = app.arguments()[2];
        QDir().mkpath(root);
        if (mode == "generate") {
            for (bool high : {false, true}) for (int index = 0; index < (high ? 2 : 4); ++index) {
                const QSize size = high ? QSize(2400,1600) : QSize(4000,3000);
                QImage image(size, high ? QImage::Format_RGBA64 : QImage::Format_RGB32);
                for (int y = 0; y < size.height(); ++y) for (int x = 0; x < size.width(); ++x) {
                    if (high) reinterpret_cast<QRgba64*>(image.scanLine(y))[x] = QRgba64::fromRgba64(
                        quint16(x*17+index*5000), quint16(y*29), quint16((x+y)*13),65535);
                    else reinterpret_cast<QRgb*>(image.scanLine(y))[x] = qRgb((x/13+index*50)%256,(y/11)%256,((x+y)/17)%256);
                }
                if (!image.save(root + QStringLiteral("/%1-%2.%3").arg(high ? "png16" : "jpeg").arg(index).arg(high ? "png" : "jpg")))
                    throw std::runtime_error("fixture write failed");
            }
            return 0;
        }
        ImageLoader loader(createDefaultImageDecoder(), nullptr, std::make_shared<ThumbnailDiskCache>(root+"/cache"));
        snapshot("idle",loader);
        const int count = mode == "png16" ? 2 : 4;
        QStringList paths;
        for (int i=0;i<count;++i) paths.append(root+QStringLiteral("/%1-%2.%3").arg(mode).arg(i).arg(mode=="png16"?"png":"jpg"));
        QVector<ImageFramePtr> frames(count);
        QElapsedTimer timer; timer.start();
        for (int edge : {512,2048}) for (int i=0;i<count;++i)
            frames[i]=load(loader,{paths[i],DecodePurpose::Preview,{edge,edge}});
        const auto previewMs=timer.elapsed(); settle(); snapshot("previews",loader,previewMs);
        timer.restart();
        for (int i=0;i<count;++i) frames[i]=load(loader,{paths[i],DecodePurpose::Full});
        const auto fullMs=timer.elapsed(); settle(); snapshot("full",loader,fullMs);
        frames.clear(); settle(); snapshot("closed",loader);
        timer.restart();
        for (const auto& path:paths) frames.append(load(loader,{path,DecodePurpose::Preview,{2048,2048}}));
        snapshot("reopen",loader,timer.elapsed());
        frames.clear(); loader.clearCache(); settle(); snapshot("cleared",loader);
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
