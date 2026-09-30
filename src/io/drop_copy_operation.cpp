#include "io/drop_copy_operation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace mvpview {
namespace {

QString copyName(const QFileInfo& source, int copyNumber) {
    if (copyNumber == 0) {
        return source.fileName();
    }
    const QString marker = copyNumber == 1 ? QStringLiteral(" copy")
                                           : QStringLiteral(" copy %1").arg(copyNumber);
    if (source.isDir()) {
        return source.fileName() + marker;
    }
    const QString name = source.fileName();
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? name.left(dot) + marker + name.mid(dot) : name + marker;
}

QString uniqueDestination(const QFileInfo& source, const QDir& target) {
    for (int copyNumber = 0;; ++copyNumber) {
        const QString candidate = target.filePath(copyName(source, copyNumber));
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
}

bool isInside(const QString& candidate, const QString& directory) {
    const auto normalized = [](const QString& path) {
        return QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()))
            .toCaseFolded();
    };
    const QString child = normalized(candidate);
    const QString parent = normalized(directory);
    if (child.compare(parent, Qt::CaseInsensitive) == 0) {
        return true;
    }
    return child.startsWith(parent + QLatin1Char('/'));
}

bool copyEntry(const QFileInfo& source, const QString& destination, QStringList* errors) {
    if (source.isSymLink()) {
        errors->append(QStringLiteral("Symbolic links are not copied: %1").arg(source.fileName()));
        return false;
    }
    if (source.isFile()) {
        if (!QFile::copy(source.absoluteFilePath(), destination)) {
            errors->append(QStringLiteral("Could not copy file: %1").arg(source.fileName()));
            return false;
        }
        QFile::setPermissions(destination, source.permissions());
        return true;
    }
    if (!source.isDir()) {
        errors->append(QStringLiteral("Unsupported filesystem item: %1").arg(source.fileName()));
        return false;
    }

    // Check every recursive step as well as the public entry point. On Windows, a directory
    // destination can be represented with a different separator/casing after Qt resolves it;
    // without this guard a recursive copy may start walking the destination it is creating.
    if (isInside(destination, source.absoluteFilePath())) {
        errors->append(QStringLiteral("Cannot copy or move a folder inside itself: %1")
                           .arg(source.fileName()));
        return false;
    }

    if (!QDir().mkpath(destination)) {
        errors->append(QStringLiteral("Could not create folder: %1").arg(source.fileName()));
        return false;
    }

    bool complete = true;
    const QFileInfoList children =
        QDir(source.absoluteFilePath())
            .entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                           QDir::NoSort);
    for (const QFileInfo& child : children) {
        complete = copyEntry(child, QDir(destination).filePath(child.fileName()), errors) && complete;
    }
    QFile::setPermissions(destination, source.permissions());
    return complete;
}

bool removeEntry(const QFileInfo& entry) {
    if (entry.isDir() && !entry.isSymLink()) {
        return QDir(entry.absoluteFilePath()).removeRecursively();
    }
    return QFile::remove(entry.absoluteFilePath());
}

bool moveEntry(const QFileInfo& source, const QString& destination, QStringList* errors) {
    if (source.isSymLink()) {
        errors->append(QStringLiteral("Symbolic links are not moved: %1").arg(source.fileName()));
        return false;
    }

    // QFile::rename/QDir::rename is atomic on the same volume. Fall back to copy+remove for
    // cross-volume paste while deleting the source only after a complete copy.
    const bool renamed = source.isDir()
                             ? QDir().rename(source.absoluteFilePath(), destination)
                             : QFile::rename(source.absoluteFilePath(), destination);
    if (renamed) {
        return true;
    }
    const qsizetype errorCountBeforeCopy = errors->size();
    if (!copyEntry(source, destination, errors)) {
        if (QFileInfo::exists(destination)) {
            (void)removeEntry(QFileInfo(destination));
        }
        return false;
    }
    if (!removeEntry(source)) {
        (void)removeEntry(QFileInfo(destination));
        while (errors->size() > errorCountBeforeCopy) {
            errors->removeLast();
        }
        errors->append(QStringLiteral("Could not remove the source after copying: %1")
                           .arg(source.fileName()));
        return false;
    }
    return true;
}

} // namespace

FileTransferResult FileTransferOperation::execute(const QStringList& sourcePaths,
                                                  const QString& targetDirectory,
                                                  FileTransferMode mode) {
    FileTransferResult result;
    const QFileInfo targetInfo(targetDirectory);
    if (!targetInfo.isDir() || !targetInfo.isWritable()) {
        result.errors.append(QStringLiteral("The current folder is not writable."));
        return result;
    }

    const QDir target(targetInfo.absoluteFilePath());
    for (const QString& sourcePath : sourcePaths) {
        const QFileInfo source(sourcePath);
        if (!source.exists()) {
            result.errors.append(QStringLiteral("Source no longer exists: %1").arg(source.fileName()));
            continue;
        }
        if (source.isDir() && isInside(target.absolutePath(), source.absoluteFilePath())) {
            result.errors.append(QStringLiteral("Cannot copy or move a folder inside itself: %1")
                                     .arg(source.fileName()));
            continue;
        }

        if (mode == FileTransferMode::Move &&
            QDir::cleanPath(source.absolutePath()) == QDir::cleanPath(target.absolutePath())) {
            // Cutting and pasting into the same directory is a successful no-op, matching system
            // file managers and avoiding a surprising rename.
            result.destinationPaths.append(source.absoluteFilePath());
            continue;
        }

        const QString destination = uniqueDestination(source, target);
        const bool succeeded = mode == FileTransferMode::Copy
                                   ? copyEntry(source, destination, &result.errors)
                                   : moveEntry(source, destination, &result.errors);
        if (succeeded) {
            result.destinationPaths.append(destination);
        }
    }
    return result;
}

DropCopyResult DropCopyOperation::execute(const QStringList& sourcePaths,
                                          const QString& targetDirectory) {
    return FileTransferOperation::execute(sourcePaths, targetDirectory, FileTransferMode::Copy);
}

} // namespace mvpview
