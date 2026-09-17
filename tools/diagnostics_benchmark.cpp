#include "diagnostics/diagnostics.h"
#include "io/default_image_decoder.h"
#include "io/image_decoder.h"
#include "io/directory_scanner.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cstdio>
using namespace ispview;
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) return 2;
    QTemporaryDir fixture;
    QImage image(1920, 1080, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) image.setPixel(x, y, qRgb(x % 256, y % 256, (x + y) % 256));
    const QString png = fixture.filePath("image.png"); if (!image.save(png)) return 3;
    const QString raw = fixture.filePath("image.yuv");
    QFile bytes(raw); if (!bytes.open(QIODevice::WriteOnly)) return 3;
    bytes.write(QByteArray(1920 * 1080 * 3 / 2, char(128))); bytes.close();
    const QString folder = fixture.filePath("large-folder"); QDir().mkpath(folder);
    for (int i = 0; i < 2000; ++i) { QFile f(folder + QStringLiteral("/%1.png").arg(i)); if (!f.open(QIODevice::WriteOnly)) return 3; }
    auto decoder = createDefaultImageDecoder();
    RawImageParameters parameters; parameters.size = {1920, 1080}; parameters.format = RawPixelFormat::NV12;
    DecodeRequest pngRequest(png, DecodePurpose::Full);
    DecodeRequest rawRequest(raw, DecodePurpose::Full, {}, parameters);
    if (!decoder->decode(pngRequest).succeeded() || !decoder->decode(rawRequest).succeeded()) return 4;
    std::array<std::array<QList<double>, 3>, 2> samples;
    // Interleave modes to reduce cache and thermal bias; skip the first warm-up round.
    for (int round = 0; round < 8; ++round) {
        for (int order = 0; order < 2; ++order) {
            const int mode = (round + order) % 2;
            QTemporaryDir logs;
            diagnostics::Service service({logs.path(), mode == 1, mode == 1});
            QElapsedTimer timer; timer.start();
            diagnostics::event(diagnostics::Level::Info, diagnostics::browse(), "directory.open", {}, true);
            if (DirectoryScanner::scan(folder).size() != 2000) return 5;
            const double scan = static_cast<double>(timer.nsecsElapsed()) / 1e6;
            timer.restart();
            for (int i = 0; i < 30; ++i) if (!decoder->decode(pngRequest).succeeded()) return 4;
            const double pngMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
            timer.restart();
            for (int i = 0; i < 30; ++i) if (!decoder->decode(rawRequest).succeeded()) return 4;
            const double rawMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
            if (round) { samples[mode][0].append(scan); samples[mode][1].append(pngMs); samples[mode][2].append(rawMs); }
            service.markCleanExit();
        }
    }
    QJsonArray results;
    // Store maintenance is informational, not gated: it now runs on the diagnostics worker, and
    // these numbers show how long the UI thread's summary refresh waits for that worker.
    double maintainMs = 0.0, usageMs = 0.0, snapshotMs = 0.0;
    {
        QTemporaryDir store;
        const QString started = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        for (int session = 0; session < 5; ++session) {
            const QString dir = store.path() + QStringLiteral("/20260101T%1-1b2c3d4e-5f60-4a7b-8c9d-0e1f2a3b4c5d")
                                                    .arg(session, 9, 10, QLatin1Char('0'));
            if (!QDir().mkpath(dir)) return 3;
            for (int file = 0; file < 400; ++file) {
                QFile entry(dir + QStringLiteral("/log-%1.jsonl").arg(file, 4, 10, QLatin1Char('0')));
                if (!entry.open(QIODevice::WriteOnly)) return 3;
                entry.write(QByteArray(4096, 'x'));
            }
            QFile meta(dir + QStringLiteral("/session.json"));
            if (!meta.open(QIODevice::WriteOnly)) return 3;
            meta.write(QJsonDocument(QJsonObject{{"started", started}}).toJson());
        }
        diagnostics::Service service({store.path(), true, false});
        QElapsedTimer timer; timer.start();
        service.maintain();
        maintainMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        timer.restart();
        (void)service.diskUsage();
        usageMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        QTemporaryDir snapshot; timer.restart();
        const QString snapshotError = service.snapshot(snapshot.path(), 0, false);
        snapshotMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        if (!snapshotError.isEmpty()) return 7;
        service.markCleanExit();
    }
    const char* names[]{"scan-2000-files", "decode-30-png", "decode-30-nv12"};
    bool pass = true;
    for (int task = 0; task < 3; ++task) {
        double median[2]{};
        for (int mode = 0; mode < 2; ++mode) {
            auto& list = samples[mode][task]; std::sort(list.begin(), list.end()); median[mode] = list[list.size() / 2];
        }
        const double delta = (median[1] / median[0] - 1.0) * 100.0; pass &= delta <= 5;
        results.append(QJsonObject{{"task", names[task]}, {"disabledMedianMs", median[0]},
                                   {"defaultInfoMedianMs", median[1]}, {"changePercent", delta}});
    }
    const QJsonObject storeTimings{{"maintainMs", maintainMs}, {"diskUsageMs", usageMs},
        {"snapshotMs", snapshotMs},
        {"scope", "Informational, not gated: 5 synthetic sessions with 2000 rotated log files."}};
    const QByteArray result = QJsonDocument(QJsonObject{{"results", results}, {"storeTimings", storeTimings},
        {"withinFivePercent", pass},
        {"scope", "Warm synthetic directory/PNG/NV12 pipeline. Excludes camera RAW, GPU interaction and helper process."}}).toJson();
    QFile output(app.arguments()[1]); if (!output.open(QIODevice::WriteOnly)) return 6;
    output.write(result); std::fwrite(result.constData(), 1, static_cast<size_t>(result.size()), stdout);
    return pass ? 0 : 1;
}
