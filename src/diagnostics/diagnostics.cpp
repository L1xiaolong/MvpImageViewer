#include "diagnostics/diagnostics.h"
#include "diagnostics/crash_capture.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QThread>
#include <QTimeZone>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <thread>

namespace ispview::diagnostics {
Q_LOGGING_CATEGORY(startup, "isp.startup")
Q_LOGGING_CATEGORY(browse, "isp.browse")
Q_LOGGING_CATEGORY(decode, "isp.decode")
Q_LOGGING_CATEGORY(render, "isp.render")
Q_LOGGING_CATEGORY(files, "isp.files")
Q_LOGGING_CATEGORY(settings, "isp.settings")
Q_LOGGING_CATEGORY(updates, "isp.updates")
namespace {
std::shared_mutex globalMutex;
Service* current = nullptr;
QtMessageHandler oldHandler = nullptr;
thread_local bool handling = false;
constexpr int slotSize = 2048, slotCount = 128;
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QByteArray json(const QJsonObject& obj) { return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n'; }
bool save(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QJsonObject readObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QStringList regularFiles(const QString& dir) {
    QStringList result;
    QDirIterator it(dir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto path = it.next();
        // Do not follow directory symlinks, nor export arbitrary files.
        result.append(path);
    }
    return result;
}
bool validDump(const QString& path) {
    if (!path.endsWith(QStringLiteral(".dmp"))) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 32) return false;
    const QByteArray header = file.read(32);
    if (!header.startsWith("MDMP")) return false;
    const quint32 count = qFromLittleEndian<quint32>(header.constData() + 8);
    const quint32 directory = qFromLittleEndian<quint32>(header.constData() + 12);
    if (count == 0 || count > 1024 || quint64(directory) + quint64(count) * 12 > quint64(file.size())) return false;
    if (!file.seek(directory)) return false;
    bool exception = false, modules = false;
    for (quint32 i = 0; i < count; ++i) {
        const QByteArray entry = file.read(12);
        if (entry.size() != 12) return false;
        const quint32 type = qFromLittleEndian<quint32>(entry.constData());
        const quint32 size = qFromLittleEndian<quint32>(entry.constData() + 4);
        const quint32 rva = qFromLittleEndian<quint32>(entry.constData() + 8);
        if (quint64(rva) + size > quint64(file.size())) return false;
        exception |= type == 6 && size >= 168; modules |= type == 4 && size >= 4;
    }
    return exception && modules;
}
bool hasDump(const QString& dir) {
    for (const auto& path : regularFiles(dir)) if (validDump(path)) return true;
    return false;
}
QJsonObject dumpSummary(const QString& dir) {
    for (const auto& path : regularFiles(dir)) {
        if (!validDump(path)) continue;
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) continue;
        const auto header = file.read(32);
        if (header.size() != 32) continue;
        QJsonObject result{{"captured", QDateTime::fromSecsSinceEpoch(qFromLittleEndian<quint32>(header.constData() + 20), QTimeZone::UTC).toString(Qt::ISODateWithMs)}};
        const quint32 count = qFromLittleEndian<quint32>(header.constData() + 8);
        const quint32 directory = qFromLittleEndian<quint32>(header.constData() + 12);
        for (quint32 i = 0; i < count; ++i) {
            file.seek(directory + i * 12); const auto entry = file.read(12);
            if (entry.size() != 12) return {};
            if (qFromLittleEndian<quint32>(entry.constData()) != 6) continue;
            file.seek(qFromLittleEndian<quint32>(entry.constData() + 8)); const auto exception = file.read(32);
            if (exception.size() != 32) return {};
            result.insert(QStringLiteral("thread"), qint64(qFromLittleEndian<quint32>(exception.constData())));
            result.insert(QStringLiteral("exceptionCode"), QString::number(qFromLittleEndian<quint32>(exception.constData() + 8), 16));
        }
        return result;
    }
    return {};
}
qint64 sizeOf(const QString& dir) {
    qint64 total = 0;
    for (const auto& path : regularFiles(dir)) total += QFileInfo(path).size();
    return total;
}
bool sessionName(const QString& name) {
    static const QRegularExpression re(QStringLiteral("^[0-9]{8}T[0-9]{9}-[a-f0-9-]{36}$"));
    return re.match(name).hasMatch();
}
void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& message) {
    if (handling) return;
    handling = true;
    Level level = Level::Info;
    switch (type) {
    case QtDebugMsg: level = Level::Debug; break;
    case QtWarningMsg: level = Level::Warning; break;
    case QtCriticalMsg: level = Level::Error; break;
    case QtFatalMsg: level = Level::Fatal; break;
    case QtInfoMsg: break;
    }
    {
        std::shared_lock lock(globalMutex);
        if (current) current->record(level, ctx.category ? ctx.category : "qt", QStringLiteral("qt.message"),
                                     {{QStringLiteral("message"), redact(message)}}, level >= Level::Warning);
    }
    // Preserve debugger/console output; the previous handler is never invoked under our locks.
    if (oldHandler) oldHandler(type, ctx, message);
    handling = false;
    if (type == QtFatalMsg) captureFatalMessage();
}
}
QString levelName(Level level) {
    return QString::fromLatin1(std::array{"Debug", "Info", "Warning", "Error", "Fatal"}[static_cast<size_t>(level)]);
}
Level parseLevel(const QString& name) {
    for (int i = 0; i < 5; ++i) if (levelName(static_cast<Level>(i)) == name) return static_cast<Level>(i);
    return Level::Info;
}
QString operationId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString redact(QString text) {
    // Most structured values are UUIDs, file hashes and enum labels. Avoid the regex engine
    // on these hot-path values while retaining identical filtering for paths/URLs/secrets.
    if (!text.contains(QLatin1Char('/')) && !text.contains(QLatin1Char('\\'))
        && !text.contains(QLatin1Char(':')) && !text.contains(QLatin1Char('='))) return text.left(4096);
    // Unstructured external diagnostics are best-effort sanitized; structured events never use paths.
    static const QRegularExpression url(QStringLiteral(R"(\b[a-zA-Z][a-zA-Z0-9+.-]*://[^\s<>"']+)"));
    static const QRegularExpression win(QStringLiteral(R"((?:[a-zA-Z]:[\\/]|\\\\)[^\r\n"'<>]+)"));
    static const QRegularExpression unixPath(QStringLiteral(R"((?<!\w)/(?:[^\s"'<>]+))"));
    static const QRegularExpression secret(QStringLiteral(R"((?i)(token|authorization|password|secret)\s*[:=]\s*\S+)"));
    text.replace(url, QStringLiteral("<url>"));
    text.replace(win, QStringLiteral("<path>"));
    text.replace(unixPath, QStringLiteral("<path>"));
    text.replace(secret, QStringLiteral("<secret>"));
    return text.left(4096);
}
// Structured contexts must not leak paths, URLs, credentials or image data, even when a caller
// nests them inside an array or object; trusting every caller to use fileId() was not enough.
// Only the first levels are retained: support diagnostics never need deep or unbounded values.
constexpr int kMaximumContextDepth = 3;
constexpr qsizetype kMaximumContextItems = 32;
QJsonValue sanitized(const QJsonValue& value, int depth) {
    if (value.isString()) {
        return QJsonValue(redact(value.toString()));
    }
    if (value.isArray()) {
        if (depth >= kMaximumContextDepth) {
            return QJsonValue(QStringLiteral("<truncated>"));
        }
        const QJsonArray source = value.toArray();
        QJsonArray result;
        for (qsizetype index = 0; index < source.size() && index < kMaximumContextItems; ++index) {
            result.append(sanitized(source.at(index), depth + 1));
        }
        if (source.size() > kMaximumContextItems) {
            result.append(QStringLiteral("<%1 more>").arg(source.size() - kMaximumContextItems));
        }
        return result;
    }
    if (value.isObject()) {
        if (depth >= kMaximumContextDepth) {
            return QJsonValue(QStringLiteral("<truncated>"));
        }
        const QJsonObject source = value.toObject();
        QJsonObject result;
        for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
            result.insert(it.key(), sanitized(it.value(), depth + 1));
        }
        return result;
    }
    return value;
}
struct Service::Impl {
    Options options;
    QString id, dir, previous, error, crashStatus = QStringLiteral("disabled");
    QStringList skipped; // sessions the last snapshot could not read, guarded by statusMutex
    std::unique_ptr<QLockFile> sessionLock;
    CrashCapture crash;
    std::atomic<bool> enabled;
    std::atomic<Level> level;
    std::mutex queueMutex, ioMutex, statusMutex, crumbMutex;
    std::condition_variable wake, drained;
    struct Entry { Level level; QByteArray bytes; };
    std::deque<Entry> queue;
    bool stopping = false, urgent = false;
    quint64 accepted = 0, completed = 0, dropped = 0;
    std::thread worker;
    QFile log, crumbs;
    uchar* mapped = nullptr;
    int slot = 0, part = 0;
    quint64 crumbSequence = 0;
    QHash<QString, QPair<qint64, quint64>> repeated;
    explicit Impl(Options o) : options(std::move(o)), enabled(options.loggingEnabled), level(options.level) {}
    void fail(const QString& message) { std::lock_guard guard(statusMutex); error = message; }
    bool openLog() {
        log.setFileName(dir + QStringLiteral("/log-%1.jsonl").arg(part++, 4, 10, QLatin1Char('0')));
        if (!log.open(QIODevice::WriteOnly | QIODevice::Append)) { fail(QStringLiteral("Cannot write diagnostic log")); return false; }
        QFile::setPermissions(log.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        return true;
    }
    void write(const QByteArray& bytes) {
        if (!log.isOpen() && !openLog()) return;
        if (log.size() > 0 && log.size() + bytes.size() > options.rotationBytes) {
            log.close();
            if (!openLog()) return;
            // Active sessions must also be bounded, without removing their open file.
            auto parts = QDir(dir).entryInfoList({QStringLiteral("log-*.jsonl")}, QDir::Files, QDir::Name);
            qint64 total = 0;
            for (const auto& p : parts) total += p.size();
            for (const auto& p : parts) {
                if (total <= limits::sessionLogBytes || p.absoluteFilePath() == log.fileName()) break;
                if (QFile::remove(p.absoluteFilePath())) total -= p.size();
            }
        }
        if (log.write(bytes) != bytes.size()) fail(QStringLiteral("Diagnostic log write failed (disk full or unavailable)"));
    }
    void run() {
        for (;;) {
            std::deque<Entry> batch;
            quint64 watermark = 0, lost = 0;
            {
                std::unique_lock lock(queueMutex);
                wake.wait_for(lock, std::chrono::milliseconds(250), [&] { return stopping || urgent; });
                batch.swap(queue); watermark = accepted; lost = std::exchange(dropped, 0); urgent = false;
                if (stopping && batch.empty() && lost == 0) break;
            }
            {
                std::lock_guard lock(ioMutex);
                for (const auto& entry : batch) write(entry.bytes);
                if (lost) write(json({{"timestamp", now()}, {"level", "Warning"}, {"category", "isp.startup"},
                                     {"session", id}, {"event", "log.dropped"}, {"count", static_cast<double>(lost)}}));
                if (log.isOpen() && !log.flush()) fail(QStringLiteral("Diagnostic log flush failed"));
            }
            { std::lock_guard lock(queueMutex); completed = watermark; }
            drained.notify_all();
        }
        { std::lock_guard lock(ioMutex); if (log.isOpen()) { log.flush(); log.close(); } }
        drained.notify_all();
    }
};
Service::Service(Options options) : impl_(std::make_unique<Impl>(std::move(options))) {
    auto& d = *impl_;
    d.id = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz")) + QLatin1Char('-') + operationId();
    d.dir = QDir(d.options.root).absoluteFilePath(d.id);
    if (d.options.loggingEnabled || d.options.crashEnabled) {
        if (!QDir().mkpath(d.dir)) d.fail(QStringLiteral("Cannot create diagnostics directory"));
        QFile::setPermissions(d.options.root, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QFile::setPermissions(d.dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        d.sessionLock = std::make_unique<QLockFile>(d.dir + QStringLiteral("/active.lock"));
        d.sessionLock->setStaleLockTime(0);
        if (!d.sessionLock->tryLock()) d.fail(QStringLiteral("Cannot lock diagnostics session"));
        if (!save(d.dir + QStringLiteral("/session.json"), json({{"session", d.id}, {"started", now()},
                  {"version", ISPVIEW_PROJECT_VERSION}, {"build", ISPVIEW_BUILD_ID},
                  {"os", QSysInfo::prettyProductName()}, {"architecture", QSysInfo::currentCpuArchitecture()},
                  {"qt", QT_VERSION_STR}}))) d.fail(QStringLiteral("Cannot save diagnostic session"));
    }
    if (d.options.crashEnabled) {
        d.crumbs.setFileName(d.dir + QStringLiteral("/breadcrumbs.bin"));
        if (d.crumbs.open(QIODevice::ReadWrite) && d.crumbs.resize(slotSize * slotCount))
            d.mapped = d.crumbs.map(0, slotSize * slotCount);
        if (!d.mapped) d.fail(QStringLiteral("Cannot initialize crash breadcrumbs"));
    }
    d.worker = std::thread([&d] { d.run(); });
    maintain();
    std::unique_lock lock(globalMutex);
    current = this;
    oldHandler = qInstallMessageHandler(messageHandler);
}
Service::~Service() {
    { std::unique_lock lock(globalMutex); qInstallMessageHandler(oldHandler); current = nullptr; }
    auto& d = *impl_;
    { std::lock_guard lock(d.queueMutex); d.stopping = true; }
    d.wake.notify_one();
    d.worker.join();
    if (d.mapped) d.crumbs.unmap(d.mapped);
}
void Service::configure(bool enabled, Level level) {
    auto& d = *impl_;
    // Even a session started with both switches off can enable ordinary logging at runtime.
    if (enabled && !d.sessionLock) {
        QDir().mkpath(d.dir);
        d.sessionLock = std::make_unique<QLockFile>(d.dir + QStringLiteral("/active.lock"));
        d.sessionLock->setStaleLockTime(0);
        if (!d.sessionLock->tryLock()) d.fail(QStringLiteral("Cannot lock diagnostics session"));
        save(d.dir + QStringLiteral("/session.json"), json({{"session", d.id}, {"started", now()},
             {"version", ISPVIEW_PROJECT_VERSION}, {"build", ISPVIEW_BUILD_ID}}));
    }
    d.level.store(level); d.enabled.store(enabled);
}
void Service::record(Level level, const char* category, const QString& name, const QJsonObject& context, bool breadcrumb) {
    auto& d = *impl_;
    const bool persist = d.enabled.load() && level >= d.level.load();
    if (!persist && !(breadcrumb && d.options.crashEnabled)) return;
    QJsonObject safe;
    for (auto it = context.constBegin(); it != context.constEnd(); ++it)
        safe.insert(it.key(), sanitized(it.value(), 0));
    QJsonObject obj{{"timestamp", now()}, {"level", levelName(level)}, {"category", QString::fromLatin1(category)},
                    {"session", d.id}, {"pid", QCoreApplication::applicationPid()},
                    {"thread", QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16)},
                    {"event", name}, {"context", safe}};
    if (breadcrumb && d.mapped) {
        std::lock_guard lock(d.crumbMutex);
        obj.insert(QStringLiteral("sequence"), static_cast<double>(++d.crumbSequence));
        QByteArray bytes = json(obj);
        if (bytes.size() >= slotSize) {
            obj.insert(QStringLiteral("context"), QJsonObject{{"truncated", true}}); bytes = json(obj);
        }
        auto* slot = d.mapped + (d.slot++ % slotCount) * slotSize;
        memset(slot, 0, slotSize);
        memcpy(slot, bytes.constData(), static_cast<size_t>(qMin(bytes.size(), qsizetype(slotSize - 1))));
    }
    if (!persist) return;
    std::lock_guard lock(d.queueMutex);
    // Bound both the dedup table and queue. Dedup uses event/category, not sensitive message text.
    if (level >= Level::Warning && level != Level::Fatal) {
        const QByteArray detail = json({{"message", safe.value("message")}, {"reason", safe.value("reason")}, {"resource", safe.value("resource")}});
        const QString key = QString::fromLatin1(category) + QLatin1Char(':') + name + levelName(level)
                            + QString::fromLatin1(QCryptographicHash::hash(detail, QCryptographicHash::Sha256).toHex());
        const qint64 time = QDateTime::currentMSecsSinceEpoch();
        auto it = d.repeated.find(key);
        if (it != d.repeated.end() && time - it->first < 1000) { ++it->second; ++d.dropped; return; }
        if (it != d.repeated.end()) obj.insert(QStringLiteral("suppressed"), static_cast<double>(it->second));
        if (d.repeated.size() >= 128) d.repeated.clear();
        d.repeated.insert(key, {time, 0});
    }
    if (d.queue.size() >= d.options.queueCapacity) {
        auto low = std::find_if(d.queue.begin(), d.queue.end(), [level](const auto& e) { return e.level < level; });
        ++d.dropped;
        if (low == d.queue.end()) return;
        d.queue.erase(low);
    }
    QByteArray bytes = json(obj);
    if (bytes.size() > 16384) { obj.insert(QStringLiteral("context"), QJsonObject{{"truncated", true}}); bytes = json(obj); }
    d.queue.push_back({level, bytes}); ++d.accepted;
    if (level >= Level::Error) { d.urgent = true; d.wake.notify_one(); }
}
bool Service::flush(int timeoutMs) {
    auto& d = *impl_;
    std::unique_lock lock(d.queueMutex);
    const quint64 target = d.accepted;
    d.urgent = true; d.wake.notify_one();
    return d.drained.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return d.completed >= target; });
}
void Service::markCleanExit() {
    record(Level::Info, "isp.startup", QStringLiteral("application.exit"), {}, true);
    flush();
    if (impl_->sessionLock && !save(impl_->dir + QStringLiteral("/closed.json"), json({{"ended", now()}})))
        impl_->fail(QStringLiteral("Cannot save clean shutdown marker"));
}
void Service::startCrashCapture(const QString& executableDirectory) {
    auto& d = *impl_;
    if (!d.options.crashEnabled) return;
    const QString result = d.crash.start(d.dir, executableDirectory, d.id);
    {
        std::lock_guard lock(d.statusMutex);
        d.crashStatus = result.isEmpty() ? QStringLiteral("active") : QStringLiteral("unavailable");
    }
    if (!result.isEmpty()) d.fail(result);
}
QString Service::root() const { return impl_->options.root; }
QString Service::sessionDirectory() const { return impl_->dir; }
QString Service::error() const { std::lock_guard lock(impl_->statusMutex); return impl_->error; }
QString Service::crashStatus() const {
    auto& d = *impl_;
    std::lock_guard lock(d.statusMutex);
    // A helper that exited after startup must not keep reporting an active crash capture.
    if (d.crashStatus == QStringLiteral("active") && !d.crash.alive()) {
        d.crashStatus = QStringLiteral("unavailable");
        d.error = QStringLiteral("Crash helper stopped; restart the application");
    }
    return d.crashStatus;
}
bool Service::crashCaptureAlive() const { return crashStatus() == QStringLiteral("active"); }
QString Service::previousExit() const {
    std::lock_guard lock(impl_->statusMutex);
    return impl_->previous;
}
bool Service::crashEnabled() const { return impl_->options.crashEnabled; }
qint64 Service::diskUsage() const { return sizeOf(root()); }
QVariantList Service::recentReports() const {
    QVariantList result;
    const auto sessions = QDir(root()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::Reversed);
    for (const auto& info : sessions) {
        if (!sessionName(info.fileName())) continue;
        auto summary = dumpSummary(info.absoluteFilePath());
        if (summary.isEmpty()) continue;
        summary.insert(QStringLiteral("session"), info.fileName()); result.append(summary.toVariantMap());
        if (result.size() == 10) break;
    }
    return result;
}
QString Service::fileId(const QString& path) const {
    return QString::fromLatin1(QCryptographicHash::hash((impl_->id + path).toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
}
QString Service::maintain(bool clearHistory) {
    auto& d = *impl_;
    if (!QDir(root()).exists()) return {};
    QLockFile rootLock(root() + QStringLiteral("/maintenance.lock")); rootLock.setStaleLockTime(0);
    if (!rootLock.tryLock()) return clearHistory ? QStringLiteral("Diagnostic files are in use; try again") : QString{};
    auto sessions = QDir(root()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::Reversed);
    qint64 normalBytes = sizeOf(d.dir), crashBytes = 0; int crashes = 0; bool failed = false;
    // maintain() may run on a worker thread; the notice decision is computed locally and only
    // published under the status lock.
    QString pendingPrevious;
    {
        std::lock_guard lock(d.statusMutex);
        pendingPrevious = d.previous;
    }
    for (const auto& info : sessions) {
        if (!sessionName(info.fileName()) || info.absoluteFilePath() == d.dir) continue;
        QLockFile session(info.absoluteFilePath() + QStringLiteral("/active.lock")); session.setStaleLockTime(0);
        if (!session.tryLock()) {
            if (clearHistory && session.error() != QLockFile::LockFailedError) failed = true;
            continue;
        }
        const bool crash = hasDump(info.absoluteFilePath());
        const auto meta = readObject(info.absoluteFilePath() + QStringLiteral("/session.json"));
        const auto started = QDateTime::fromString(meta.value(QStringLiteral("started")).toString(), Qt::ISODateWithMs);
        const qint64 age = started.isValid() ? started.daysTo(QDateTime::currentDateTimeUtc()) : 31;
        if (pendingPrevious.isEmpty() && !QFileInfo::exists(info.absoluteFilePath() + QStringLiteral("/noticed.json")) &&
            (crash || !QFileInfo::exists(info.absoluteFilePath() + QStringLiteral("/closed.json")))) {
            pendingPrevious = crash ? QStringLiteral("crash") : QStringLiteral("unclean");
            save(info.absoluteFilePath() + QStringLiteral("/noticed.json"), json({{"noticed", now()}}));
        }
        const qint64 bytes = sizeOf(info.absoluteFilePath());
        bool remove = clearHistory;
        if (crash) {
            ++crashes;
            crashBytes += bytes;
            remove |= age > limits::crashRetentionDays || crashes > limits::maximumCrashSessions ||
                      crashBytes > limits::crashSessionBytes;
        } else {
            normalBytes += bytes;
            remove |= age > limits::normalRetentionDays || normalBytes > limits::sessionLogBytes;
        }
        // Unlock before deletion (Windows cannot delete the open lock), root lock prevents races with exporters.
        session.unlock();
        if (remove && !QDir(info.absoluteFilePath()).removeRecursively()) failed = true;
    }
    if (!pendingPrevious.isEmpty()) {
        std::lock_guard lock(d.statusMutex);
        if (d.previous.isEmpty()) d.previous = pendingPrevious;
    }
    return failed ? QStringLiteral("Some diagnostic files could not be removed") : QString{};
}
QString Service::snapshot(const QString& destination, int days, bool includeDumps,
                          const std::function<bool()>& isCancelled) {
    auto& d = *impl_;
    const auto cancelled = [&isCancelled] { return isCancelled && isCancelled(); };
    {
        std::lock_guard lock(d.statusMutex);
        d.skipped.clear();
    }
    if (!QDir(root()).exists()) return QStringLiteral("No diagnostic records are available");
    if (!flush()) return QStringLiteral("Timed out flushing diagnostics");
    QLockFile rootLock(root() + QStringLiteral("/maintenance.lock")); rootLock.setStaleLockTime(0);
    if (!rootLock.tryLock(5000)) return QStringLiteral("Diagnostics are in use; try again");
    // Log files are copied without holding ioMutex, so the writer keeps draining during an
    // export; only the breadcrumb mapping needs its own lock and is read in one short section.
    const auto sessions = QDir(root()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    for (const auto& info : sessions) {
        if (cancelled()) return QStringLiteral("Export cancelled");
        if (!sessionName(info.fileName())) continue;
        std::unique_ptr<QLockFile> lock;
        if (info.absoluteFilePath() != d.dir) {
            lock = std::make_unique<QLockFile>(info.absoluteFilePath() + QStringLiteral("/active.lock"));
            lock->setStaleLockTime(0);
            if (!lock->tryLock()) {
                // Tell the exporter which sessions were left out instead of dropping them silently.
                std::lock_guard guard(d.statusMutex);
                d.skipped.append(info.fileName());
                continue;
            }
        }
        const auto cutoff = days > 0 ? QDateTime::currentDateTimeUtc().addDays(-days) : QDateTime{};
        const auto captured = dumpSummary(info.absoluteFilePath());
        const auto capturedAt = QDateTime::fromString(captured.value(QStringLiteral("captured")).toString(), Qt::ISODateWithMs);
        const bool dumpInRange = !cutoff.isValid() || (capturedAt.isValid() && capturedAt >= cutoff);
        auto paths = regularFiles(info.absoluteFilePath());
        QDateTime latest;
        for (const auto& path : paths) {
            const auto file = QFileInfo(path);
            if (file.fileName() != QStringLiteral("active.lock") && file.fileName() != QStringLiteral("noticed.json"))
                latest = qMax(latest, file.lastModified().toUTC());
        }
        if (info.absoluteFilePath() != impl_->dir && cutoff.isValid() && latest < cutoff
            && !(capturedAt.isValid() && capturedAt >= cutoff)) continue;
        int partialRecords = 0;
        const QString dest = destination + QLatin1Char('/') + info.fileName();
        if (!QDir().mkpath(dest)) return QStringLiteral("Cannot create export snapshot");
        for (const auto& path : paths) {
            if (cancelled()) return QStringLiteral("Export cancelled");
            const QString name = QFileInfo(path).fileName();
            const bool dump = validDump(path);
            if (name == QStringLiteral("breadcrumbs.bin")) {
                std::lock_guard crumbLock(d.crumbMutex);
                QFile file(path); if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("Cannot read breadcrumbs");
                const QByteArray bytes = file.readAll();
                QList<QJsonObject> records;
                for (int i = 0; i + slotSize <= bytes.size(); i += slotSize) {
                    QByteArray slot = bytes.mid(i, slotSize); const auto nul = slot.indexOf('\0'); if (nul >= 0) slot.truncate(nul);
                    const auto obj = QJsonDocument::fromJson(slot).object();
                    const auto time = QDateTime::fromString(obj.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
                    if (!obj.isEmpty() && (!cutoff.isValid() || time >= cutoff)) records.append(obj);
                }
                std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return a.value("sequence").toDouble() < b.value("sequence").toDouble(); });
                QByteArray lines; for (const auto& record : records) lines += json(record);
                if (!save(dest + QStringLiteral("/breadcrumbs.jsonl"), lines)) return QStringLiteral("Cannot save breadcrumbs snapshot");
            } else if (name.startsWith(QStringLiteral("log-")) && name.endsWith(QStringLiteral(".jsonl"))) {
                QFile input(path); QSaveFile output(dest + QLatin1Char('/') + name);
                // Rotation can remove an older part between listing and reading it.
                if (!input.open(QIODevice::ReadOnly)) continue;
                if (!output.open(QIODevice::WriteOnly)) return QStringLiteral("Cannot snapshot diagnostic log");
                while (!input.atEnd()) {
                    const QByteArray line = input.readLine();
                    const auto object = QJsonDocument::fromJson(line).object();
                    if (object.isEmpty()) { ++partialRecords; continue; }
                    const auto time = QDateTime::fromString(object.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
                    if ((!cutoff.isValid() || time >= cutoff) && output.write(line) != line.size()) return QStringLiteral("Cannot write log snapshot");
                }
                if (input.error() != QFileDevice::NoError || !output.commit()) return QStringLiteral("Cannot complete log snapshot");
            } else if (name == QStringLiteral("session.json") || name == QStringLiteral("closed.json") || (includeDumps && dump && dumpInRange)) {
                if (!QFile::copy(path, dest + QLatin1Char('/') + name)) return QStringLiteral("Cannot copy diagnostic snapshot");
            }
        }
        if (!save(dest + QStringLiteral("/summary.json"), json({{"session", info.fileName()},
                  {"version", ISPVIEW_PROJECT_VERSION}, {"build", ISPVIEW_BUILD_ID},
                  {"crashDumpAvailable", hasDump(info.absoluteFilePath())}, {"crash", dumpSummary(info.absoluteFilePath())}, {"dumpIncluded", includeDumps && dumpInRange && !captured.isEmpty()}, {"partialLogRecordsSkipped", partialRecords},
                  {"cleanExit", QFileInfo::exists(info.absoluteFilePath() + QStringLiteral("/closed.json"))}})))
            return QStringLiteral("Cannot save diagnostic summary");
    }
    return {};
}
QStringList Service::skippedSessions() const {
    std::lock_guard lock(impl_->statusMutex);
    return impl_->skipped;
}
void event(Level level, const QLoggingCategory& category, const QString& name, const QJsonObject& context, bool breadcrumb) {
    std::shared_lock lock(globalMutex);
    if (current) current->record(level, category.categoryName(), name, context, breadcrumb);
}
QString fileId(const QString& path) {
    std::shared_lock lock(globalMutex); return current ? current->fileId(path) : QStringLiteral("untracked");
}
} // namespace ispview::diagnostics
