#pragma once

class QKeyEvent;

namespace mvpview {

// Normalizes the native Finder/Explorer trash gesture without exposing platform macros to UI
// windows and views. Text-editing focus remains a caller policy, not a platform concern.
[[nodiscard]] bool isPlatformTrashShortcut(const QKeyEvent* event);

// Item views also honor Qt's standard Delete sequence when used independently from MainWindow.
[[nodiscard]] bool isItemViewTrashShortcut(const QKeyEvent* event);

} // namespace mvpview
