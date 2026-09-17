#pragma once
#include "diagnostics/diagnostics.h"
#include <QObject>
#include <QUrl>
#include <QTimer>
#include <atomic>
#include <thread>
namespace ispview {
class DiagnosticsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList recentReports READ recentReports NOTIFY changed)
    Q_PROPERTY(QString directory READ directory CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString previousExit READ previousExit NOTIFY changed)
    Q_PROPERTY(bool crashActive READ crashActive NOTIFY changed)
    Q_PROPERTY(qint64 diskUsage READ diskUsage NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(QString exportedPath READ exportedPath NOTIFY changed)
    Q_PROPERTY(QString retentionSummary READ retentionSummary CONSTANT)
public:
    explicit DiagnosticsController(diagnostics::Service& service, QObject* parent = nullptr);
    ~DiagnosticsController() override;
    QVariantList recentReports() const { return reports_; }
    QString directory() const;
    QString status() const;
    QString error() const;
    QString previousExit() const;
    bool crashActive() const;
    qint64 diskUsage() const { return diskUsage_; }
    bool busy() const { return busy_; }
    int progress() const { return progress_; }
    QString exportedPath() const { return exportedPath_; }
    // Human-readable retention policy built from diagnostics::limits so the settings page cannot
    // advertise limits that differ from the enforced ones.
    QString retentionSummary() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openDirectory();
    Q_INVOKABLE void openExportDirectory();
    Q_INVOKABLE QString clearHistory();
    Q_INVOKABLE void exportLogs(const QUrl& destination, int days, bool includeDumps);
    Q_INVOKABLE void cancelExport();
private:
    // Maintenance and storage reporting walk the whole diagnostic store, so they always run on a
    // worker: the UI thread only receives the finished summary.
    void scheduleScan(bool runMaintenance);
    void applyScan(qint64 diskUsage, QVariantList reports);
    diagnostics::Service& service_;
    QTimer timer_;
    bool busy_ = false;
    int progress_ = 0;
    qint64 diskUsage_ = 0;
    QVariantList reports_;
    QString error_, exportedPath_;
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> closing_{false};
    std::atomic<bool> scanning_{false};
    std::atomic<bool> scanPending_{false};
    std::thread worker_;
    std::thread scanWorker_;
signals:
    void changed();
};
}
