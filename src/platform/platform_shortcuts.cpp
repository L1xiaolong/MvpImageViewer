#include "platform/platform_shortcuts.h"

#include <QKeyEvent>
#include <QKeySequence>

namespace mvpview {

bool isPlatformTrashShortcut(const QKeyEvent* event) {
    if (!event) {
        return false;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
#if defined(Q_OS_MACOS)
    // Qt maps the physical Command key to ControlModifier so portable Ctrl shortcuts become
    // Command shortcuts on macOS. Finder's Command+Delete can arrive as Backspace or Delete.
    return modifiers == Qt::ControlModifier &&
           (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete);
#else
    return modifiers == Qt::NoModifier && event->key() == Qt::Key_Delete;
#endif
}

bool isItemViewTrashShortcut(const QKeyEvent* event) {
    return event && (event->matches(QKeySequence::Delete) || isPlatformTrashShortcut(event));
}

} // namespace mvpview
