#pragma once
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <mutex>
namespace mvpview::performance {
inline bool enabled() { static const bool value = qEnvironmentVariableIntValue("MVPVIEW_PERF") != 0; return value; }
inline QElapsedTimer& clock() { static QElapsedTimer timer = [] { QElapsedTimer t; t.start(); return t; }(); return timer; }
inline void mark(const QString& name, QJsonObject fields = {}) {
    if (!enabled()) return;
    fields.insert(QStringLiteral("event"), name);
    fields.insert(QStringLiteral("sinceStartMs"), clock().elapsed());
    qInfo().noquote() << "MVPVIEW_PERF" << QJsonDocument(fields).toJson(QJsonDocument::Compact);
}
}
