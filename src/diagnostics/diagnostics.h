#pragma once

#include <QJsonObject>
#include <QLoggingCategory>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <functional>
#include <memory>

namespace ispview::diagnostics {
Q_DECLARE_LOGGING_CATEGORY(startup)
Q_DECLARE_LOGGING_CATEGORY(browse)
Q_DECLARE_LOGGING_CATEGORY(decode)
Q_DECLARE_LOGGING_CATEGORY(render)
Q_DECLARE_LOGGING_CATEGORY(files)
Q_DECLARE_LOGGING_CATEGORY(settings)
Q_DECLARE_LOGGING_CATEGORY(updates)

enum class Level { Debug, Info, Warning, Error, Fatal };

// Retention and size policy shared by the service, the settings UI and the documentation so the
// documented limits cannot drift away from the enforced ones.
namespace limits {
inline constexpr qint64 logRotationBytes = 10LL * 1024 * 1024;
inline constexpr qint64 sessionLogBytes = 100LL * 1024 * 1024;
inline constexpr int normalRetentionDays = 7;
inline constexpr int crashRetentionDays = 30;
inline constexpr int maximumCrashSessions = 10;
inline constexpr qint64 crashSessionBytes = 200LL * 1024 * 1024;
} // namespace limits

struct Options {
    QString root;
    bool loggingEnabled = true;
    bool crashEnabled = true;
    Level level = Level::Info;
    qint64 rotationBytes = limits::logRotationBytes;
    size_t queueCapacity = 4096;
};

// Lives longer than the application and all producers. Tests may supply an isolated root.
class Service final {
public:
    explicit Service(Options options);
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    void configure(bool enabled, Level level);
    void startCrashCapture(const QString& executableDirectory);
    void markCleanExit();
    bool flush(int timeoutMs = 5000);
    QString root() const;
    QString sessionDirectory() const;
    QString error() const;
    QString crashStatus() const;
    // True while crash capture is configured for this session and its helper is still running.
    bool crashCaptureAlive() const;
    // True when the previous session ended without writing its clean-exit marker. Updated by
    // maintain(), which may run on a worker thread.
    QString previousExit() const;
    bool crashEnabled() const;
    qint64 diskUsage() const;
    QVariantList recentReports() const;
    QString maintain(bool clearHistory = false);
    // Called on the export worker. Log files are copied without locking the writer, so logging
    // continues during an export; only the breadcrumb mapping is read under its own lock.
    // Sessions owned by another running instance are skipped and reported by skippedSessions().
    QString snapshot(const QString& destination, int days, bool includeDumps,
                     const std::function<bool()>& isCancelled = {});
    [[nodiscard]] QStringList skippedSessions() const;
    void record(Level level, const char* category, const QString& event,
                const QJsonObject& context = {}, bool breadcrumb = false);
    QString fileId(const QString& path) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

void event(Level level, const QLoggingCategory& category, const QString& name,
           const QJsonObject& context = {}, bool breadcrumb = false);
QString fileId(const QString& path);
QString operationId();
Level parseLevel(const QString& name);
QString levelName(Level level);
// Sanitizes unstructured Qt/third-party messages before disk persistence.
QString redact(QString text);
} // namespace ispview::diagnostics
