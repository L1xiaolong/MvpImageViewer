#pragma once
#include "core/performance_trace.h"
#include <QEvent>
#include <QGuiApplication>
#include <QScopedValueRollback>
#include <QThread>

namespace mvpview {
// A heartbeat measures delivery gaps; notify measures actual event execution.
// Keep this inert outside the explicitly enabled benchmark mode.
class PerformanceGuiApplication final : public QGuiApplication {
public:
    using QGuiApplication::QGuiApplication;
    bool notify(QObject* receiver, QEvent* event) override {
        if (!performance::enabled() || QThread::currentThread() != thread() || !receiver || !event)
            return QGuiApplication::notify(receiver, event);
        // Events may delete their receiver; snapshot its identity before dispatch.
        const QString receiverClass = QString::fromLatin1(receiver->metaObject()->className());
        const int eventType = int(event->type());
        QScopedValueRollback<int> depth(notificationDepth_, notificationDepth_ + 1);
        const auto startedAt = performance::clock().elapsed();
        QElapsedTimer timer; timer.start();
        const bool handled = QGuiApplication::notify(receiver, event);
        const auto elapsedUs = timer.nsecsElapsed() / 1000;
        if (elapsedUs >= 4000)
            performance::mark(QStringLiteral("ui.gui_event"),
                {{"elapsedUs", elapsedUs}, {"receiverClass", receiverClass},
                 {"eventType", eventType}, {"depth", notificationDepth_}, {"startSinceStartMs", startedAt}});
        return handled;
    }
private:
    int notificationDepth_ = 0;
};
}
