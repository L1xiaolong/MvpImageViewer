#include "io/default_image_decoder.h"
#include "io/directory_scanner.h"
#include "io/image_loader.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace mvpview {
namespace {

bool writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray jpegFixture() {
    QImage image(640, 360, QImage::Format_RGB888);
    image.fill(QColor(72, 118, 146));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG", 85);
    return bytes;
}

} // namespace
} // namespace mvpview

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const bool quick = application.arguments().contains(QStringLiteral("--quick"));
    const int totalEntries = quick ? 1'000 : 10'000;
    const int imageCount = quick ? 300 : 3'000;
    const int hiddenImages = quick ? 20 : 200;
    const int hiddenDirectories = quick ? 20 : 200;
    const int visibleDirectories = totalEntries - imageCount;

    QTemporaryDir directory;
    if (!directory.isValid()) return 2;
    const QByteArray jpeg = mvpview::jpegFixture();
    for (int index = 0; index < imageCount; ++index) {
        if (!mvpview::writeBytes(
                directory.filePath(QStringLiteral("image-%1.jpg").arg(index, 5, 10, QLatin1Char('0'))),
                jpeg)) return 3;
    }
    for (int index = 0; index < visibleDirectories; ++index) {
        if (!QDir().mkpath(directory.filePath(QStringLiteral("album-%1").arg(index, 5, 10,
                                                                          QLatin1Char('0'))))) {
            return 4;
        }
    }
    for (int index = 0; index < hiddenImages; ++index) {
        if (!mvpview::writeBytes(directory.filePath(QStringLiteral(".hidden-%1.jpg").arg(index)),
                                 jpeg)) return 5;
    }
    for (int index = 0; index < hiddenDirectories; ++index) {
        const QString hidden = directory.filePath(QStringLiteral(".hidden-album-%1").arg(index));
        if (!QDir().mkpath(hidden) ||
            !mvpview::writeBytes(QDir(hidden).filePath(QStringLiteral("inside.jpg")), jpeg)) {
            return 6;
        }
    }

    QElapsedTimer scanTimer;
    scanTimer.start();
    double firstBatchMilliseconds = -1.0;
    QVector<mvpview::ImageFileRecord> records;
    mvpview::DirectoryScanner scanner;
    QEventLoop scanLoop;
    QObject::connect(&scanner, &mvpview::DirectoryScanner::scanBatchReady, &scanLoop,
                     [&](const QString&, const QVector<mvpview::ImageFileRecord>&, quint64) {
                         if (firstBatchMilliseconds < 0.0) {
                             firstBatchMilliseconds = scanTimer.nsecsElapsed() / 1'000'000.0;
                         }
                     });
    QObject::connect(&scanner, &mvpview::DirectoryScanner::scanFinished, &scanLoop,
                     [&](const QString&, const QVector<mvpview::ImageFileRecord>& files, quint64) {
                         records = files;
                         scanLoop.quit();
                     });
    scanner.scanAsync(directory.path());
    QTimer::singleShot(15'000, &scanLoop, [&scanLoop] { scanLoop.exit(12); });
    if (scanLoop.exec() != 0) return 12;
    const double scanMilliseconds = scanTimer.nsecsElapsed() / 1'000'000.0;
    if (records.size() != totalEntries) return 7;
    for (const auto& record : records) {
        if (record.fileName.startsWith(QLatin1Char('.'))) return 8;
    }

    mvpview::ImageLoader loader(mvpview::createDefaultImageDecoder());
    QElapsedTimer thumbnailsTimer;
    thumbnailsTimer.start();
    double firstThumbnailMilliseconds = -1.0;
    int completed = 0;
    int submitted = 0;
    constexpr int requestedThumbnails = 24;
    QEventLoop loop;
    for (const auto& record : records) {
        if (record.isDirectory) continue;
        loader.request(
            static_cast<quint64>(submitted + 1),
            {record.path, mvpview::DecodePurpose::Thumbnail, QSize(256, 256)},
            [&](quint64, const mvpview::DecodeResult& result) {
                if (!result.succeeded()) {
                    loop.exit(9);
                    return;
                }
                if (firstThumbnailMilliseconds < 0.0) {
                    firstThumbnailMilliseconds = thumbnailsTimer.nsecsElapsed() / 1'000'000.0;
                }
                if (++completed == requestedThumbnails) loop.quit();
            },
            mvpview::RequestOptions{mvpview::LoadCategory::VisibleThumbnail, 0,
                                    QStringLiteral("browser-benchmark")});
        if (++submitted >= requestedThumbnails) break;
    }
    QTimer::singleShot(15'000, &loop, [&loop] { loop.exit(10); });
    const int loopResult = loop.exec();
    if (loopResult != 0 || completed != requestedThumbnails) return loopResult == 0 ? 11 : loopResult;

QJsonObject output{
        {QStringLiteral("platform"), QSysInfo::prettyProductName()},
        {QStringLiteral("cpuArchitecture"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
        {QStringLiteral("idealThreads"), QThread::idealThreadCount()},
        {QStringLiteral("totalFixtureEntries"),
         totalEntries + hiddenImages + hiddenDirectories},
        {QStringLiteral("targetVisibleEntries"), totalEntries},
        {QStringLiteral("visibleEntries"), records.size()},
        {QStringLiteral("hiddenEntriesExcluded"), hiddenImages + hiddenDirectories},
        {QStringLiteral("firstBatchMilliseconds"), firstBatchMilliseconds},
        {QStringLiteral("scanMilliseconds"), scanMilliseconds},
        {QStringLiteral("firstThumbnailMilliseconds"), firstThumbnailMilliseconds},
        {QStringLiteral("firstViewportMilliseconds"),
         thumbnailsTimer.nsecsElapsed() / 1'000'000.0}};
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        output.insert(QStringLiteral("peakResidentMiB"),
                      counters.PeakWorkingSetSize / (1024.0 * 1024.0));
    }
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(Q_OS_MACOS)
        const double peakBytes = static_cast<double>(usage.ru_maxrss);
#else
        const double peakBytes = static_cast<double>(usage.ru_maxrss) * 1024.0;
#endif
        output.insert(QStringLiteral("peakResidentMiB"), peakBytes / (1024.0 * 1024.0));
    }
#endif
    QFile standardOutput;
    standardOutput.open(stdout, QIODevice::WriteOnly);
    standardOutput.write(QJsonDocument(output).toJson(QJsonDocument::Indented));
    const bool enforce = application.arguments().contains(QStringLiteral("--enforce"));
    if (enforce && (firstBatchMilliseconds > 150.0 || scanMilliseconds > 2'000.0 ||
                    firstThumbnailMilliseconds > 800.0 ||
                    output.value(QStringLiteral("peakResidentMiB")).toDouble() > 512.0)) {
        return 13;
    }
    return 0;
}
