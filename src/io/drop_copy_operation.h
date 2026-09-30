#pragma once

#include <QString>
#include <QStringList>

namespace mvpview {

struct FileTransferResult {
    QStringList destinationPaths;
    QStringList errors;
};

using DropCopyResult = FileTransferResult;

enum class FileTransferMode { Copy, Move };

// Shared filesystem transfer used by drag/drop and clipboard paste. The operation is synchronous
// and UI-free so callers can run it on a worker thread. Existing destinations are never
// overwritten; conflicts receive a deterministic "copy" suffix.
class FileTransferOperation final {
  public:
    [[nodiscard]] static FileTransferResult execute(const QStringList& sourcePaths,
                                                    const QString& targetDirectory,
                                                    FileTransferMode mode);
};

// Implements the filesystem meaning of dropping Finder/Explorer items onto the browser. It is a
// synchronous, UI-free operation intended to run on a worker thread.
class DropCopyOperation final {
  public:
    [[nodiscard]] static DropCopyResult execute(const QStringList& sourcePaths,
                                                const QString& targetDirectory);
};

} // namespace mvpview
