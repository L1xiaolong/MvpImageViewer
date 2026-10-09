#pragma once
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <mutex>
#include <atomic>
#include <utility>
namespace mvpview::performance {
inline bool enabled() { static const bool value = qEnvironmentVariableIntValue("MVPVIEW_PERF") != 0; return value; }
inline QElapsedTimer& clock() { static QElapsedTimer timer = [] { QElapsedTimer t; t.start(); return t; }(); return timer; }
inline void mark(const QString& name, QJsonObject fields = {}) {
    if (!enabled()) return;
    fields.insert(QStringLiteral("event"), name);
    fields.insert(QStringLiteral("sinceStartMs"), clock().elapsed());
    qInfo().noquote() << "MVPVIEW_PERF" << QJsonDocument(fields).toJson(QJsonDocument::Compact);
}
class Scope final {
public:
    explicit Scope(QString name, QJsonObject fields = {}) : name_(std::move(name)), fields_(std::move(fields)) {
        if (!enabled()) return;
        static std::atomic<qint64> sequence{0};
        fields_.insert(QStringLiteral("scopeId"), ++sequence);
        timer_.start();
        mark(name_ + QStringLiteral(".begin"), fields_);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() {
        if (!timer_.isValid()) return;
        fields_.insert(QStringLiteral("elapsedMs"), timer_.elapsed());
        mark(name_ + QStringLiteral(".end"), fields_);
    }
private:
    QString name_;
    QJsonObject fields_;
    QElapsedTimer timer_;
};
}
