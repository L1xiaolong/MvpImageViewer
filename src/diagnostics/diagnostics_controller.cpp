#include "diagnostics/diagnostics_controller.h"
#include <QDesktopServices>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <miniz.h>
namespace ispview {
namespace {
struct ZipOutput { QSaveFile* file; std::atomic<bool>* cancelled; };
size_t writeZip(void* opaque, mz_uint64 offset, const void* data, size_t count) {
    auto& out = *static_cast<ZipOutput*>(opaque);
    if (out.cancelled->load() || !out.file->seek(static_cast<qint64>(offset))) return 0;
    const auto written = out.file->write(static_cast<const char*>(data), static_cast<qint64>(count));
    return written < 0 ? 0 : static_cast<size_t>(written);
}
size_t readZip(void* opaque, mz_uint64 offset, void* data, size_t count) {
    auto& file = *static_cast<QFile*>(opaque);
    if (!file.seek(static_cast<qint64>(offset))) return 0;
    const auto read = file.read(static_cast<char*>(data), static_cast<qint64>(count));
    return read < 0 ? 0 : static_cast<size_t>(read);
}
}
DiagnosticsController::DiagnosticsController(diagnostics::Service& service, QObject* parent)
    : QObject(parent), service_(service) {
    // The first scan runs without maintenance: Service already performed it while detecting the
    // previous session, and re-walking the store here would only delay the first window.
    scheduleScan(false);
    timer_.setInterval(60000);
    connect(&timer_, &QTimer::timeout, this, [this] { if (!busy_) scheduleScan(true); });
    timer_.start();
}
DiagnosticsController::~DiagnosticsController() {
    closing_.store(true);
    cancelled_.store(true);
    if (scanWorker_.joinable()) scanWorker_.join();
    if (worker_.joinable()) worker_.join();
}
QString DiagnosticsController::directory() const { return service_.root(); }
QString DiagnosticsController::status() const { return service_.crashStatus(); }
QString DiagnosticsController::error() const { return error_.isEmpty() ? service_.error() : error_; }
QString DiagnosticsController::previousExit() const { return service_.previousExit(); }
bool DiagnosticsController::crashActive() const { return service_.crashEnabled(); }
QString DiagnosticsController::retentionSummary() const {
    return tr("Logs: %1 days / %2 MiB. Crash reports: %3 days / %4 reports / %5 MiB. "
              "Active sessions are protected.")
        .arg(diagnostics::limits::normalRetentionDays)
        .arg(diagnostics::limits::sessionLogBytes / (1024 * 1024))
        .arg(diagnostics::limits::crashRetentionDays)
        .arg(diagnostics::limits::maximumCrashSessions)
        .arg(diagnostics::limits::crashSessionBytes / (1024 * 1024));
}
void DiagnosticsController::refresh() { scheduleScan(true); }
void DiagnosticsController::scheduleScan(bool runMaintenance) {
    if (closing_.load(std::memory_order_relaxed)) return;
    if (scanning_.exchange(true, std::memory_order_relaxed)) {
        scanPending_.store(true, std::memory_order_relaxed);
        return;
    }
    if (scanWorker_.joinable()) scanWorker_.join();
    scanWorker_ = std::thread([this, runMaintenance] {
        if (runMaintenance) service_.maintain();
        const qint64 usage = service_.diskUsage();
        QVariantList reports = service_.recentReports();
        if (closing_.load(std::memory_order_relaxed)) return;
        QMetaObject::invokeMethod(this, [this, usage, reports] { applyScan(usage, reports); },
                                  Qt::QueuedConnection);
    });
}
void DiagnosticsController::applyScan(qint64 diskUsage, QVariantList reports) {
    diskUsage_ = diskUsage;
    reports_ = std::move(reports);
    scanning_.store(false, std::memory_order_relaxed);
    emit changed();
    // A refresh requested while this scan ran must not be lost.
    if (scanPending_.exchange(false, std::memory_order_relaxed)) scheduleScan(false);
}
void DiagnosticsController::openDirectory() { QDesktopServices::openUrl(QUrl::fromLocalFile(directory())); }
void DiagnosticsController::openExportDirectory() {
    if (!exportedPath_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(exportedPath_).absolutePath()));
}
QString DiagnosticsController::clearHistory() {
    if (busy_) return tr("Export is running.");
    // Deliberately synchronous: this is an explicit destructive action, the dialog reports its
    // result, and retention has to be applied before the UI claims success. Periodic scans use
    // scheduleScan() instead so ordinary refreshes never block the UI thread.
    error_ = service_.maintain(true); refresh(); return error_;
}
void DiagnosticsController::cancelExport() { cancelled_.store(true); }
void DiagnosticsController::exportLogs(const QUrl& destination, int days, bool includeDumps) {
    if (busy_) return;
    if (!destination.isLocalFile() || destination.toLocalFile().isEmpty()) {
        error_ = tr("Choose a local ZIP file."); emit changed(); return;
    }
    const QString path = destination.toLocalFile();
    // An export must not become part of the diagnostic store or overwrite its files.
    const QString root = QDir(directory()).absolutePath() + QLatin1Char('/');
    const QString canonicalRoot = QFileInfo(directory()).canonicalFilePath() + QLatin1Char('/');
    const QString canonicalParent = QFileInfo(QFileInfo(path).absolutePath()).canonicalFilePath() + QLatin1Char('/');
    if (QFileInfo(path).isSymLink() || QFileInfo(path).absoluteFilePath().startsWith(root, Qt::CaseInsensitive)
        || (canonicalRoot != QStringLiteral("/") && canonicalParent.startsWith(canonicalRoot, Qt::CaseInsensitive))) {
        error_ = tr("Choose a location outside the diagnostics directory."); emit changed(); return;
    }
    if (worker_.joinable()) worker_.join();
    busy_ = true; progress_ = 0; error_.clear(); exportedPath_.clear(); cancelled_.store(false); emit changed();
    worker_ = std::thread([this, path, days, includeDumps] {
        QString failure;
        QTemporaryDir snapshot;
        if (!snapshot.isValid()) failure = tr("Cannot create temporary export directory.");
        else failure = service_.snapshot(snapshot.path(), days, includeDumps,
                                         [this] { return cancelled_.load(std::memory_order_relaxed); });
        // The manifest records what the export actually left out instead of claiming a blanket
        // exclusion, so a support engineer can tell a missing session from a failed export.
        const QSettings settings;
        const QStringList skipped = service_.skippedSessions();
        QJsonArray skippedNames;
        for (const QString& name : skipped) skippedNames.append(name);
        const QJsonObject manifest{{"formatVersion", 1}, {"includeDumps", includeDumps},
            {"loggingEnabled", settings.value("diagnostics/loggingEnabled", true).toBool()},
            {"logLevel", settings.value("diagnostics/logLevel", "Info").toString()},
            {"crashReportingEnabled", settings.value("diagnostics/crashReportingEnabled", true).toBool()},
            {"otherRunningSessionsExcluded", !skipped.isEmpty()},
            {"skippedSessions", skippedNames}};
        QSaveFile output(path); mz_zip_archive zip{}; bool initialized = false;
        ZipOutput target{&output, &cancelled_};
        if (failure.isEmpty() && !cancelled_.load()) {
            if (!output.open(QIODevice::WriteOnly)) failure = tr("Cannot write the ZIP file.");
            else {
                zip.m_pWrite = writeZip; zip.m_pIO_opaque = &target;
                initialized = mz_zip_writer_init_v2(&zip, 0, MZ_ZIP_FLAG_WRITE_ZIP64);
                if (!initialized) failure = tr("Cannot initialize ZIP export.");
            }
        }
        if (initialized) {
            const QByteArray bytes = QJsonDocument(manifest).toJson();
            if (!mz_zip_writer_add_mem(&zip, "manifest.json", bytes.constData(), static_cast<size_t>(bytes.size()), MZ_BEST_SPEED))
                failure = tr("Cannot write export manifest.");
            QStringList paths; QDirIterator it(snapshot.path(), QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (it.hasNext()) paths.append(it.next());
            int index = 0;
            for (const auto& filePath : paths) {
                if (!failure.isEmpty() || cancelled_.load()) break;
                QFile file(filePath);
                const QByteArray name = QDir(snapshot.path()).relativeFilePath(filePath).toUtf8();
                if (!file.open(QIODevice::ReadOnly) || !mz_zip_writer_add_read_buf_callback(&zip, name.constData(), readZip,
                    &file, static_cast<mz_uint64>(file.size()), nullptr, nullptr, 0, MZ_BEST_SPEED, nullptr, 0, nullptr, 0))
                    failure = tr("Cannot compress diagnostic file.");
                const int progress = ++index * 95 / qMax(1, static_cast<int>(paths.size()));
                if (closing_.load(std::memory_order_relaxed)) break;
                QMetaObject::invokeMethod(this, [this, progress] { progress_ = progress; emit changed(); }, Qt::QueuedConnection);
            }
            if (failure.isEmpty() && !cancelled_.load() && !mz_zip_writer_finalize_archive(&zip)) failure = tr("Cannot finalize ZIP export.");
            mz_zip_writer_end(&zip);
        }
        if (cancelled_.load()) failure = tr("Export cancelled.");
        if (failure.isEmpty() && !output.commit()) failure = tr("Cannot save ZIP export.");
        if (!failure.isEmpty()) output.cancelWriting();
        if (closing_.load(std::memory_order_relaxed)) return;
        QMetaObject::invokeMethod(this, [this, path, failure] {
            busy_ = false; error_ = failure;
            if (failure.isEmpty()) { exportedPath_ = path; progress_ = 100; }
            refresh();
        }, Qt::QueuedConnection);
    });
}
}
