#include "io/directory_scanner.h"

#include "io/supported_image_formats.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QPointer>
#include <QSet>

#include <algorithm>
#include <utility>

namespace mvpview {
namespace {

QString normalizedAbsolutePath(const QString& path) {
    return QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()))
        .toCaseFolded();
}

bool isDescendantPath(const QDir& root, const QString& candidate) {
    const QString relative = QDir::fromNativeSeparators(root.relativeFilePath(candidate));
    return relative != QStringLiteral(".") && relative != QStringLiteral("..") &&
           !relative.startsWith(QStringLiteral("../")) && !QDir::isAbsolutePath(relative);
}

constexpr int kScanBatchSize = 192;

} // namespace

DirectoryScanner::DirectoryScanner(QObject* parent) : QObject(parent) {
    pool_.setMaxThreadCount(1);
    pool_.setExpiryTimeout(10'000);
}

DirectoryScanner::~DirectoryScanner() { cancel(); }

void DirectoryScanner::cancel() {
    if (currentCancel_) currentCancel_->store(true, std::memory_order_relaxed);
}

quint64 DirectoryScanner::scanAsync(const QString& directory) {
    cancel();
    const quint64 generation = ++generation_;
    const CancelFlag cancelled = std::make_shared<std::atomic_bool>(false);
    currentCancel_ = cancelled;
    const QPointer<DirectoryScanner> self(this);
    pool_.start(
        [self, directory, generation, cancelled] {
            if (self) {
                QMetaObject::invokeMethod(
                    self,
                    [self, directory, generation] {
                        if (self) emit self->scanStarted(directory, generation);
                    },
                    Qt::QueuedConnection);
            }
            auto files = scanBatched(
                directory, cancelled,
                [self, directory, generation, cancelled](QVector<ImageFileRecord> batch) {
                    if (!self || cancelled->load(std::memory_order_relaxed)) return;
                    QMetaObject::invokeMethod(
                        self,
                        [self, directory, generation, batch = std::move(batch), cancelled] {
                            if (self && !cancelled->load(std::memory_order_relaxed)) {
                                emit self->scanBatchReady(directory, batch, generation);
                            }
                        },
                        Qt::QueuedConnection);
                });
            if (cancelled->load(std::memory_order_relaxed)) return;
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self,
                [self, directory, files = std::move(files), generation] {
                    if (self) {
                        emit self->scanFinished(directory, files, generation);
                    }
                },
                Qt::QueuedConnection);
        },
        0);
    return generation;
}

quint64 DirectoryScanner::scanImageFoldersAsync(const QString& directory) {
    cancel();
    const quint64 generation = ++generation_;
    const CancelFlag cancelled = std::make_shared<std::atomic_bool>(false);
    currentCancel_ = cancelled;
    const QPointer<DirectoryScanner> self(this);
    pool_.start(
        [self, directory, generation, cancelled] {
            auto files = scanImageFoldersRecursively(directory);
            if (cancelled->load(std::memory_order_relaxed)) return;
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self,
                [self, directory, files = std::move(files), generation] {
                    if (self) {
                        emit self->scanFinished(directory, files, generation);
                    }
                },
                Qt::QueuedConnection);
        },
        0);
    return generation;
}

QVector<ImageFileRecord> DirectoryScanner::scan(const QString& directory) {
    return scanBatched(directory, {}, {});
}

QVector<ImageFileRecord> DirectoryScanner::scanBatched(
    const QString& directory, const CancelFlag& cancelled,
    const std::function<void(QVector<ImageFileRecord>)>& publishBatch) {
    QVector<ImageFileRecord> result;
    QVector<ImageFileRecord> batch;
    batch.reserve(kScanBatchSize);
    QDirIterator iterator(directory,
                          QDir::AllEntries | QDir::Readable | QDir::NoDotAndDotDot |
                              QDir::NoSymLinks,
                          QDirIterator::NoIteratorFlags);
    while (iterator.hasNext()) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) return {};
        const QFileInfo info = iterator.nextFileInfo();
        if (!isBrowsableEntry(info)) continue;
        if (!info.isDir() && (!info.isFile() || !hasSupportedImageSuffix(info.fileName()))) {
            continue;
        }
        ImageFileRecord record{info.absoluteFilePath(), info.fileName(),
                               info.isDir() ? 0 : info.size(), info.lastModified(), info.isDir(),
                               {}};
        record.fileType = info.isDir() ? QStringLiteral("folder")
                                       : info.suffix().toCaseFolded();
        result.push_back(record);
        if (publishBatch) {
            batch.push_back(std::move(record));
            // A time-only threshold can degenerate into one queued UI update per entry on a slow
            // disk. Keep a hard lower bound so the producer cannot overwhelm the GUI event loop.
            if (batch.size() >= kScanBatchSize) {
                publishBatch(std::exchange(batch, {}));
                batch.reserve(kScanBatchSize);
            }
        }
    }
    if (publishBatch && !batch.isEmpty()) publishBatch(std::move(batch));

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(result.begin(), result.end(), [&collator](const auto& left, const auto& right) {
        if (left.isDirectory != right.isDirectory) {
            return left.isDirectory;
        }
        return collator.compare(left.fileName, right.fileName) < 0;
    });
    return result;
}

QVector<ImageFileRecord> DirectoryScanner::scanImageFoldersRecursively(const QString& directory) {
    const QDir root(directory);
    const QString rootPath = QDir::cleanPath(root.absolutePath());
    const QString normalizedRootPath = normalizedAbsolutePath(rootPath);
    if (!QFileInfo(rootPath).isDir()) {
        return {};
    }

    QVector<ImageFileRecord> result;

    // Images directly in the selected folder remain selectable. Descendant images are represented
    // by their containing folders, so a large tree does not turn into one unstructured image list.
    const QFileInfoList directImages = root.entryInfoList(
        supportedImageNameFilters(), QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::NoSort);
    result.reserve(directImages.size());
    for (const QFileInfo& image : directImages) {
        if (!isBrowsableEntry(image)) continue;
        result.push_back({image.absoluteFilePath(), image.fileName(), image.size(),
                          image.lastModified(), false, image.suffix().toCaseFolded()});
    }

    QSet<QString> imageFolders;
    QDirIterator iterator(rootPath, supportedImageNameFilters(),
                          QDir::Files | QDir::Readable | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QFileInfo image(iterator.next());
        if (!isBrowsableEntry(image)) continue;
        QString folder = QDir::cleanPath(image.absolutePath());
        if (folder == rootPath) {
            continue;
        }

        // Mark both the direct image folder and every ancestor below the selected root. This makes
        // every branch that can lead to an image visible while excluding empty/document-only trees.
        while (normalizedAbsolutePath(folder) != normalizedRootPath &&
               isDescendantPath(root, folder)) {
            const QFileInfo folderInfo(folder);
            if (!isBrowsableEntry(folderInfo)) break;
            imageFolders.insert(folder);
            const QString parent = QDir::cleanPath(QFileInfo(folder).absolutePath());
            if (normalizedAbsolutePath(parent) == normalizedAbsolutePath(folder)) {
                break;
            }
            folder = parent;
        }
    }

    for (const QString& folder : std::as_const(imageFolders)) {
        const QFileInfo info(folder);
        result.push_back({folder, root.relativeFilePath(folder), 0, info.lastModified(), true,
                          QStringLiteral("folder")});
    }

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(result.begin(), result.end(), [&collator](const auto& left, const auto& right) {
        if (left.isDirectory != right.isDirectory) {
            return left.isDirectory;
        }
        return collator.compare(left.fileName, right.fileName) < 0;
    });
    return result;
}

bool DirectoryScanner::isSupportedImageFile(const QString& path) {
    const QFileInfo info(path);
    if (!isBrowsableEntry(info) || !info.isFile()) {
        return false;
    }
    return hasSupportedImageSuffix(info.fileName());
}

bool DirectoryScanner::isBrowsableEntry(const QFileInfo& info) {
    const QString name = info.fileName();
    return info.exists() && name != QStringLiteral(".") && name != QStringLiteral("..") &&
           !name.startsWith(QLatin1Char('.')) && !info.isHidden() && !info.isSymLink() &&
           info.isReadable();
}

} // namespace mvpview
