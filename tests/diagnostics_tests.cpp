#include "diagnostics/diagnostics.h"
#include "diagnostics/diagnostics_controller.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QLockFile>
#include <thread>
#include <vector>
#include <miniz.h>
using namespace ispview::diagnostics;
namespace {
QByteArray logs(const QString& root) {
    QByteArray result; QDirIterator it(root, {"*.jsonl"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { QFile f(it.next()); if (f.open(QIODevice::ReadOnly)) result += f.readAll(); }
    return result;
}
QStringList dumps(const QString& root) {
    QStringList result; QDirIterator it(root, {"*.dmp"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) result.append(it.next()); return result;
}
QString fixture() {
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + "/ispview_diagnostics_crash_fixture.exe";
#else
    return QCoreApplication::applicationDirPath() + "/ispview_diagnostics_crash_fixture";
#endif
}
}
class DiagnosticsTests : public QObject {
    Q_OBJECT
private slots:
    void filteringAndPrivacy() {
        QTemporaryDir root; Service service({root.path(), true, false, Level::Warning});
        service.record(Level::Info, "test", "excluded");
        service.record(Level::Error, "test", "included", {{"message", "读取失败 C:\\private\\photo.raw"}});
        QVERIFY(service.flush()); auto bytes = logs(root.path());
        QVERIFY(!bytes.contains("excluded")); QVERIFY(bytes.contains("included")); QVERIFY(!bytes.contains("private"));
        QVERIFY(bytes.contains(QStringLiteral("读取失败").toUtf8()));
        for (const auto& line : bytes.split('\n')) if (!line.isEmpty()) QVERIFY(QJsonDocument::fromJson(line).isObject());
        service.configure(false, Level::Debug); service.record(Level::Fatal, "test", "disabled"); service.flush();
        QVERIFY(!logs(root.path()).contains("disabled"));
        QVERIFY(!redact("https://host/path?token=secret").contains("secret"));
        QVERIFY(!redact("/Users/alice/photo.jpg").contains("alice"));
    }
    void boundedConcurrentQueueAndRotation() {
        QTemporaryDir root; Options options{root.path(), true, false, Level::Debug};
        options.rotationBytes = 2048; options.queueCapacity = 32; Service service(options);
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) threads.emplace_back([&] {
            for (int i = 0; i < 2000; ++i) service.record(Level::Debug, "test", "stress", {{"i", i}});
        });
        for (auto& thread : threads) thread.join();
        QVERIFY(service.flush());
        const auto bytes = logs(root.path()); QVERIFY(bytes.contains("log.dropped"));
        QDir dir(service.sessionDirectory()); QVERIFY(dir.entryList({"log-*.jsonl"}).size() > 1);
        for (const auto& line : bytes.split('\n')) if (!line.isEmpty()) QVERIFY(QJsonDocument::fromJson(line).isObject());
    }
    void breadcrumbsWithoutOrdinaryLogs() {
        QTemporaryDir root, snapshot; Service service({root.path(), false, true});
        for (int i = 0; i < 200; ++i) service.record(Level::Info, "test", "crumb", {{"i", i}}, true);
        QCOMPARE(service.snapshot(snapshot.path(), 0, false), QString{});
        const auto bytes = logs(snapshot.path()); QVERIFY(bytes.contains("crumb"));
        QCOMPARE(bytes.count('\n'), 128); QVERIFY(logs(root.path()).isEmpty());
        QCOMPARE(QFileInfo(service.sessionDirectory() + "/breadcrumbs.bin").size(), qint64(2048 * 128));
    }
    void writeFailureDoesNotCrash() {
        QTemporaryDir root; QFile file(root.filePath("not-directory")); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        Service service({file.fileName(), true, false}); service.record(Level::Error, "test", "write");
        QVERIFY(service.flush()); QVERIFY(!service.error().isEmpty());
    }
    void cleanupProtectsActiveSession() {
        QTemporaryDir root; QString previous;
        { Service service({root.path(), true, false}); previous = service.sessionDirectory(); service.markCleanExit(); }
        QLockFile active(previous + "/active.lock"); active.setStaleLockTime(0); QVERIFY(active.tryLock());
        Service service({root.path(), true, false}); service.maintain(true); QVERIFY(QDir(previous).exists());
        active.unlock(); service.maintain(true); QVERIFY(!QDir(previous).exists()); QVERIFY(QDir(service.sessionDirectory()).exists());
    }
    void zipExportAndCancellation() {
        QTemporaryDir root, output; Service service({root.path(), true, false});
        service.record(Level::Info, "test", "exported", {{"path", "/Users/private/image.png"}});
        ispview::DiagnosticsController controller(service);
        const auto path = output.filePath(QStringLiteral("诊断.zip"));
        controller.exportLogs(QUrl::fromLocalFile(path), 7, false);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15000);
        QVERIFY2(controller.error().isEmpty(), qPrintable(controller.error())); QCOMPARE(controller.progress(), 100);
        QFile archive(path); QVERIFY(archive.open(QIODevice::ReadOnly)); const auto zipBytes = archive.readAll();
        mz_zip_archive zip{}; QVERIFY(mz_zip_reader_init_mem(&zip, zipBytes.constData(), static_cast<size_t>(zipBytes.size()), 0));
        QVERIFY(mz_zip_reader_locate_file(&zip, "manifest.json", nullptr, 0) >= 0);
        QByteArray manifestBytes;
        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
            mz_zip_archive_file_stat stat{}; QVERIFY(mz_zip_reader_file_stat(&zip, i, &stat));
            const QByteArray name(stat.m_filename);
            QVERIFY(!name.endsWith(".dmp"));
            size_t size = 0; void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0); QVERIFY(data);
            const QByteArray content(static_cast<char*>(data), static_cast<qsizetype>(size));
            if (name == QByteArrayLiteral("manifest.json")) manifestBytes = content;
            QVERIFY(!content.contains("/Users/private")); mz_free(data);
        }
        mz_zip_reader_end(&zip); archive.close();
        const auto manifest = QJsonDocument::fromJson(manifestBytes).object();
        QVERIFY(manifest.contains("skippedSessions"));
        QVERIFY(manifest.value("skippedSessions").isArray());
        QCOMPARE(manifest.value("otherRunningSessionsExcluded").toBool(), false);
        const auto cancelled = output.filePath("cancelled.zip");
        controller.exportLogs(QUrl::fromLocalFile(cancelled), 0, false); controller.cancelExport();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15000); QVERIFY(!QFileInfo::exists(cancelled));
    }
    void bothDisabledAndLateEnable() {
        QTemporaryDir root; const QString store = root.filePath("store");
        Service service({store, false, false});
        service.record(Level::Fatal, "test", "disabled", {}, true);
        QVERIFY(!QDir(store).exists());
        service.configure(true, Level::Info);
        service.record(Level::Info, "test", "enabled");
        QVERIFY(service.flush()); QVERIFY(logs(store).contains("enabled"));
        QVERIFY(!logs(store).contains("disabled"));
    }
    void expiredSessionsAndTimeRange() {
        QTemporaryDir root; QString old;
        { Service service({root.path(), true, false}); old = service.sessionDirectory(); service.markCleanExit(); }
        QFile metadata(old + "/session.json"); QVERIFY(metadata.open(QIODevice::WriteOnly));
        metadata.write("{\"started\":\"2000-01-01T00:00:00.000Z\"}"); metadata.close();
        Service service({root.path(), true, false}); QVERIFY(!QDir(old).exists());
        service.record(Level::Info, "test", "current");
        QTemporaryDir snapshot; QCOMPARE(service.snapshot(snapshot.path(), 1, false), QString{});
        QVERIFY(logs(snapshot.path()).contains("current"));
    }
    void snapshotExcludesAnotherLiveSession() {
        QTemporaryDir root;
        QProcess child; child.start(fixture(), {root.path(), "wait", "off"}); QVERIFY(child.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(root.filePath("ready")), 5000);
        Service service({root.path(), true, false});
        service.record(Level::Info, "test", "local");
        QTemporaryDir snapshot; QCOMPARE(service.snapshot(snapshot.path(), 0, false), QString{});
        QVERIFY(!logs(snapshot.path()).contains("before.crash"));
        // The skipped live session must be reported instead of silently disappearing.
        const auto skipped = service.skippedSessions();
        QCOMPARE(skipped.size(), 1);
        QVERIFY(skipped.first() != QFileInfo(service.sessionDirectory()).fileName());
        QVERIFY(QDir(root.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).contains(skipped.first()));
        service.maintain(true); QVERIFY(logs(root.path()).contains("before.crash"));
        child.kill(); QVERIFY(child.waitForFinished());
    }
    void disabledCrashProducesNoDump() {
        QTemporaryDir root; QProcess child;
        child.start(fixture(), {root.path(), "access", "off"}); QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForFinished(15000)); QVERIFY(dumps(root.path()).isEmpty());
    }
    void timeWindowKeepsLongRunningSession() {
        QTemporaryDir root; Service service({root.path(), true, false});
        QFile meta(service.sessionDirectory() + "/session.json"); QVERIFY(meta.open(QIODevice::WriteOnly));
        meta.write("{\"started\":\"2000-01-01T00:00:00.000Z\"}"); meta.close();
        QFile old(service.sessionDirectory() + "/log-old.jsonl"); QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("{\"timestamp\":\"2000-01-01T00:00:00.000Z\",\"event\":\"too-old\"}\n"); old.close();
        service.record(Level::Info, "test", "recent");
        QTemporaryDir snapshot; QCOMPARE(service.snapshot(snapshot.path(), 1, false), QString{});
        const auto bytes = logs(snapshot.path()); QVERIFY(bytes.contains("recent")); QVERIFY(!bytes.contains("too-old"));
    }
    void nestedContextIsRedacted() {
        QTemporaryDir root; Service service({root.path(), true, false, Level::Debug});
        service.record(Level::Error, "test", "nested",
                       {{QStringLiteral("paths"), QJsonArray{QStringLiteral("/Users/alice/secret.raw")}},
                        {QStringLiteral("detail"),
                         QJsonObject{{QStringLiteral("message"), QStringLiteral("C:\\\\private\\\\photo.raw")}}},
                        {QStringLiteral("counts"), QJsonArray{1, 2, 3}}});
        QVERIFY(service.flush());
        const auto bytes = logs(root.path());
        QVERIFY(bytes.contains("nested"));
        QVERIFY(!bytes.contains("alice"));
        QVERIFY(!bytes.contains("private"));
        QVERIFY(bytes.contains("counts\":[1,2,3]"));
    }
    void oversizedContextIsBounded() {
        QTemporaryDir root; Service service({root.path(), true, false, Level::Debug});
        QJsonArray large; for (int i = 0; i < 40; ++i) large.append(i);
        service.record(Level::Warning, "test", "bounded", {{QStringLiteral("items"), large}});
        QVERIFY(service.flush());
        const auto bytes = logs(root.path());
        QVERIFY(bytes.contains("<8 more>"));
        QVERIFY(!bytes.contains(",32,"));
    }
    void controllerRejectsUnsafeExportTargets() {
        QTemporaryDir root, outside; Service service({root.path(), true, false});
        ispview::DiagnosticsController controller(service);
        // A destination inside the store would make the export part of the next export.
        const QString inside = service.sessionDirectory() + QStringLiteral("/nested.zip");
        controller.exportLogs(QUrl::fromLocalFile(inside), 7, false);
        QVERIFY(!controller.error().isEmpty());
        QVERIFY(!QFileInfo::exists(inside));
        // A remote URL is never a local export target.
        controller.exportLogs(QUrl(QStringLiteral("https://example.com/diagnostics.zip")), 7, false);
        QVERIFY(controller.error().contains(QStringLiteral("local")));
        // A directory cannot be replaced by the archive.
        controller.exportLogs(QUrl::fromLocalFile(outside.path()), 7, false);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15000);
        QVERIFY(!controller.error().isEmpty());
        QVERIFY(QFileInfo(outside.path()).isDir());
    }
    void controllerScansOffTheCallingThread() {
        QTemporaryDir root; Service service({root.path(), true, false});
        service.record(Level::Info, "test", "scan"); QVERIFY(service.flush());
        ispview::DiagnosticsController controller(service);
        // The constructor must not walk the store: the summary arrives from the scan worker.
        QCOMPARE(controller.diskUsage(), qint64(0));
        QTRY_VERIFY_WITH_TIMEOUT(controller.diskUsage() > 0, 5000);
        QVERIFY(!controller.retentionSummary().isEmpty());
    }
    void crashHelperLossIsReported() {
#ifdef Q_OS_WIN
        QTemporaryDir root; QProcess child;
        child.start(fixture(), {root.path(), "helperkill", "on"});
        QVERIFY(child.waitForStarted()); QVERIFY(child.waitForFinished(30000));
        QCOMPARE(child.exitCode(), 0);
        QFile report(root.filePath("helper-status.txt"));
        QVERIFY(report.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(report.readAll()).trimmed(), QStringLiteral("unavailable"));
#endif
    }
    void crashCapture_data() {
        QTest::addColumn<QString>("mode"); QTest::addColumn<bool>("worker");
        for (const auto& mode : {"access", "throw", "abort", "fatal"}) {
            QTest::newRow(mode) << QString::fromLatin1(mode) << false;
            QTest::newRow((QByteArray(mode) + "-worker").constData()) << QString::fromLatin1(mode) << true;
        }
    }
    void crashCapture() {
#if defined(Q_OS_WIN) || (defined(Q_OS_MACOS) && ISPVIEW_HAS_CRASHPAD)
        QFETCH(QString, mode); QFETCH(bool, worker); QTemporaryDir root;
        QProcess child; QStringList args{root.path(), mode, "on"}; if (worker) args << "worker";
        child.start(fixture(), args); QVERIFY(child.waitForStarted()); QVERIFY(child.waitForFinished(20000));
        QVERIFY2(child.exitCode() != 3, child.readAllStandardError().constData());
        const auto reports = dumps(root.path()); QCOMPARE(reports.size(), 1);
        QFile dump(reports.first()); QVERIFY(dump.open(QIODevice::ReadOnly)); QCOMPARE(dump.read(4), QByteArray("MDMP"));
        QVERIFY(dump.size() > 1024); dump.close();
        Service service({root.path(), true, false}); QCOMPARE(service.previousExit(), QString("crash"));
        QTemporaryDir snapshot; QCOMPARE(service.snapshot(snapshot.path(), 0, true), QString{});
        QCOMPARE(dumps(snapshot.path()).size(), 1); QVERIFY(logs(snapshot.path()).contains("before.crash"));
#endif
    }
    void killedAndDisabled() {
        QTemporaryDir root;
        QProcess child; child.start(fixture(), {root.path(), "wait", "off"}); QVERIFY(child.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(root.filePath("ready")), 5000);
        child.kill(); QVERIFY(child.waitForFinished()); QVERIFY(dumps(root.path()).isEmpty());
        Service service({root.path(), true, false}); QCOMPARE(service.previousExit(), QString("unclean"));
    }
};
QTEST_GUILESS_MAIN(DiagnosticsTests)
#include "diagnostics_tests.moc"
