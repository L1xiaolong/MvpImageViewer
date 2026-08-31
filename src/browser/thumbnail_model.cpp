#include "browser/thumbnail_model.h"

#include "io/image_loader.h"

#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QMimeData>
#include <QPainter>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace ispview {
namespace {

QString formattedFileSize(qint64 bytes) {
    constexpr qint64 mebibyte = 1024LL * 1024LL;
    if (bytes >= mebibyte) {
        return QStringLiteral("%1 MB")
            .arg(QLocale().toString(static_cast<double>(bytes) / mebibyte, 'f', 1));
    }
    return QStringLiteral("%1 KB").arg(QLocale().toString(qMax<qint64>(1, bytes / 1024)));
}

QString platformFolderIconName() {
#ifdef Q_OS_MACOS
    return QStringLiteral("macos-folder.svg");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows-folder.svg");
#else
    return QStringLiteral("folder.svg");
#endif
}

QPixmap textPlaceholder(const QString& text) {
    QPixmap result(160, 120);
    result.fill(QColor(48, 51, 57));
    QPainter painter(&result);
    painter.setPen(QColor(150, 154, 162));
    painter.drawText(result.rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                     text);
    return result;
}

QPixmap folderPlaceholder() {
    const QIcon icon = QIcon::fromTheme(QStringLiteral("folder"));
    if (!icon.isNull()) return icon.pixmap(120, 96);
    const QPixmap bundled(QStringLiteral(":/icons/ui/%1").arg(platformFolderIconName()));
    return bundled.isNull() ? textPlaceholder(QStringLiteral("Folder")) : bundled;
}

} // namespace

ThumbnailModel::ThumbnailModel(ImageLoader* loader, QObject* parent)
    : QAbstractListModel(parent), loader_(loader), placeholder_(textPlaceholder("Loading…")),
      folderPlaceholder_(folderPlaceholder()) {
    connect(loader_, &ImageLoader::rawParametersChanged, this,
            [this](const QString& path) { invalidateThumbnail(path); });
    connect(loader_, &ImageLoader::thumbnailMetadataReady, this,
            [this](const QString& path, const QSize& sourceSize, int validBits) {
                const int row = pathToRow_.value(path, -1);
                if (row < 0 || (dimensions_.value(path) == sourceSize &&
                                bitDepths_.value(path) == validBits)) {
                    return;
                }
                dimensions_.insert(path, sourceSize);
                bitDepths_.insert(path, validBits);
                const QModelIndex changed = index(row);
                emit dataChanged(changed, changed,
                                 {DimensionsRole, BitDepthRole, TechnicalLabelRole});
            });
}

int ThumbnailModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(files_.size());
}

QVariant ThumbnailModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= files_.size()) {
        return {};
    }
    const auto& file = files_.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return file.fileName;
    case Qt::DecorationRole:
        if (file.isDirectory) {
            return folderPlaceholder_;
        }
        return placeholder_;
    case Qt::ToolTipRole:
        return file.isDirectory
                   ? QStringLiteral("%1\nFolder").arg(file.path)
                   : QStringLiteral("%1\n%2\nModified %3")
                         .arg(file.path, formattedFileSize(file.fileSize),
                              file.modifiedAt.toString(Qt::ISODate));
    case PathRole:
        return file.path;
    case SizeRole:
        return file.fileSize;
    case ModifiedRole:
        return file.modifiedAt;
    case DirectoryRole:
        return file.isDirectory;
    case TypeRole:
        return file.isDirectory
                   ? QStringLiteral("Folder")
                   : file.fileType.isEmpty()
                         ? QFileInfo(file.fileName).suffix().toCaseFolded()
                         : file.fileType;
    case DimensionsRole:
        return dimensions_.value(file.path);
    case BitDepthRole:
        return bitDepths_.value(file.path);
    case FileSizeTextRole:
        return file.isDirectory ? QString{} : formattedFileSize(file.fileSize);
    case ThumbnailUrlRole: {
        if (file.isDirectory) {
            return QStringLiteral("qrc:/icons/ui/%1").arg(platformFolderIconName());
        }
        QString revision = QStringLiteral("%1-%2")
                               .arg(file.fileSize)
                               .arg(file.modifiedAt.toMSecsSinceEpoch());
        if (const auto parameters = loader_->rawParameters(file.path)) {
            revision += QLatin1Char('-') + parameters->cacheKey();
        }
        return QStringLiteral("image://thumbnail/%1?v=%2")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(file.path)),
                 QString::fromLatin1(QUrl::toPercentEncoding(revision)));
    }
    case FileNameRole:
        return file.fileName;
    case TechnicalLabelRole: {
        if (file.isDirectory) {
            return QStringLiteral("Folder");
        }
        const QSize dimensions = dimensions_.value(file.path);
        const QString dimensionText =
            dimensions.isValid()
                ? QStringLiteral("%1×%2").arg(dimensions.width()).arg(dimensions.height())
                : QStringLiteral("Reading size…");
        const QString type = (file.fileType.isEmpty() ? QFileInfo(file.fileName).suffix()
                                                       : file.fileType)
                                 .toUpper();
        const int bitDepth = bitDepths_.value(file.path);
        const QString bitDepthText =
            bitDepth > 0 ? QStringLiteral("%1 bit").arg(bitDepth) : QStringLiteral("Reading…");
        return QStringLiteral("%1 | %2 | %3 | %4")
            .arg(type, dimensionText, bitDepthText, formattedFileSize(file.fileSize));
    }
    case SelectedRole:
        return selectedOrdinals_.contains(file.path);
    case SelectionOrdinalRole:
        return selectedOrdinals_.value(file.path, 0);
    default:
        return {};
    }
}

QHash<int, QByteArray> ThumbnailModel::roleNames() const {
    auto roles = QAbstractListModel::roleNames();
    roles.insert(PathRole, "path");
    roles.insert(SizeRole, "fileSize");
    roles.insert(ModifiedRole, "modifiedAt");
    roles.insert(DirectoryRole, "isDirectory");
    roles.insert(TypeRole, "fileType");
    roles.insert(DimensionsRole, "dimensions");
    roles.insert(BitDepthRole, "bitDepth");
    roles.insert(FileSizeTextRole, "fileSizeText");
    roles.insert(ThumbnailUrlRole, "thumbnailUrl");
    roles.insert(FileNameRole, "fileName");
    roles.insert(TechnicalLabelRole, "technicalLabel");
    roles.insert(SelectedRole, "isSelected");
    roles.insert(SelectionOrdinalRole, "selectionOrdinal");
    return roles;
}

void ThumbnailModel::setSelectedPaths(const QStringList& paths) {
    if (selectedPaths_ == paths) {
        return;
    }
    const QStringList previous = std::exchange(selectedPaths_, paths);
    selectedOrdinals_.clear();
    selectedOrdinals_.reserve(selectedPaths_.size());
    for (int ordinal = 0; ordinal < selectedPaths_.size(); ++ordinal) {
        selectedOrdinals_.insert(selectedPaths_.at(ordinal), ordinal + 1);
    }
    QSet<int> rows;
    for (const QString& path : previous) {
        const int row = pathToRow_.value(path, -1);
        if (row >= 0) {
            rows.insert(row);
        }
    }
    for (const QString& path : selectedPaths_) {
        const int row = pathToRow_.value(path, -1);
        if (row >= 0) {
            rows.insert(row);
        }
    }
    QList<int> orderedRows = rows.values();
    std::sort(orderedRows.begin(), orderedRows.end());
    for (int offset = 0; offset < orderedRows.size();) {
        const int first = orderedRows.at(offset);
        int last = first;
        while (++offset < orderedRows.size() && orderedRows.at(offset) == last + 1) {
            last = orderedRows.at(offset);
        }
        emit dataChanged(index(first), index(last), {SelectedRole, SelectionOrdinalRole});
    }
}

Qt::ItemFlags ThumbnailModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags result = QAbstractListModel::flags(index);
    if (index.isValid()) {
        result |= Qt::ItemIsDragEnabled;
    }
    return result;
}

QStringList ThumbnailModel::mimeTypes() const { return {QStringLiteral("text/uri-list")}; }

QMimeData* ThumbnailModel::mimeData(const QModelIndexList& indexes) const {
    QList<QUrl> urls;
    QSet<QString> paths;
    for (const QModelIndex& index : indexes) {
        if (!index.isValid()) {
            continue;
        }
        const QString path = index.data(PathRole).toString();
        if (!path.isEmpty() && !paths.contains(path)) {
            paths.insert(path);
            urls.append(QUrl::fromLocalFile(path));
        }
    }
    auto* result = new QMimeData;
    result->setUrls(urls);
    return result;
}

Qt::DropActions ThumbnailModel::supportedDragActions() const { return Qt::CopyAction; }

void ThumbnailModel::setFiles(QVector<ImageFileRecord> files) {
    beginResetModel();
    files_ = std::move(files);
    dimensions_.clear();
    bitDepths_.clear();
    rebuildPathIndex();
    endResetModel();
}

void ThumbnailModel::appendFiles(const QVector<ImageFileRecord>& files) {
    if (files.isEmpty()) return;
    const int first = static_cast<int>(files_.size());
    const int last = first + static_cast<int>(files.size()) - 1;
    beginInsertRows({}, first, last);
    files_.reserve(files_.size() + files.size());
    for (const ImageFileRecord& file : files) {
        pathToRow_.insert(file.path, static_cast<int>(files_.size()));
        files_.push_back(file);
    }
    endInsertRows();
}

void ThumbnailModel::updateFiles(const QVector<ImageFileRecord>& files) {
    QHash<QString, ImageFileRecord> incomingByPath;
    incomingByPath.reserve(files.size());
    for (const auto& file : files) incomingByPath.insert(file.path, file);

    int changedCount = 0;
    for (const auto& existing : std::as_const(files_)) {
        const auto incoming = incomingByPath.constFind(existing.path);
        if (incoming == incomingByPath.cend() || existing.fileSize != incoming->fileSize ||
            existing.modifiedAt != incoming->modifiedAt || existing.fileName != incoming->fileName) {
            ++changedCount;
        }
    }
    for (const auto& incoming : files) {
        if (!pathToRow_.contains(incoming.path)) ++changedCount;
    }
    const int referenceCount = std::max(1, std::max(static_cast<int>(files_.size()),
                                                    static_cast<int>(files.size())));
    if (changedCount * 5 > referenceCount) {
        QHash<QString, QSize> retainedDimensions;
        QHash<QString, int> retainedBitDepths;
        for (const auto& incoming : files) {
            const int oldRow = pathToRow_.value(incoming.path, -1);
            if (oldRow < 0) continue;
            const auto& old = files_.at(oldRow);
            if (old.fileSize == incoming.fileSize && old.modifiedAt == incoming.modifiedAt &&
                dimensions_.contains(incoming.path)) {
                retainedDimensions.insert(incoming.path, dimensions_.value(incoming.path));
                retainedBitDepths.insert(incoming.path, bitDepths_.value(incoming.path));
            }
        }
        beginResetModel();
        files_ = files;
        dimensions_ = std::move(retainedDimensions);
        bitDepths_ = std::move(retainedBitDepths);
        rebuildPathIndex();
        endResetModel();
        return;
    }

    // Remove backwards so every existing persistent index before the removed row stays valid.
    for (qsizetype row = files_.size(); row-- > 0;) {
        const QString path = files_.at(row).path;
        if (incomingByPath.contains(path)) {
            continue;
        }
        beginRemoveRows({}, static_cast<int>(row), static_cast<int>(row));
        files_.removeAt(row);
        dimensions_.remove(path);
        bitDepths_.remove(path);
        endRemoveRows();
    }
    rebuildPathIndex();

    for (int row = 0; row < files_.size(); ++row) {
        ImageFileRecord& existing = files_[row];
        const ImageFileRecord& incoming = incomingByPath.value(existing.path);
        const bool contentChanged =
            existing.fileSize != incoming.fileSize || existing.modifiedAt != incoming.modifiedAt;
        if (existing.fileName != incoming.fileName || contentChanged) {
            existing = incoming;
            if (contentChanged) {
                dimensions_.remove(incoming.path);
                bitDepths_.remove(incoming.path);
            }
            const QModelIndex changed = index(row);
            emit dataChanged(changed, changed);
        }
    }

    QVector<ImageFileRecord> additions;
    additions.reserve(changedCount);
    for (const auto& incoming : files) {
        if (!pathToRow_.contains(incoming.path)) additions.push_back(incoming);
    }
    appendFiles(additions);
    rebuildPathIndex();
}

QString ThumbnailModel::pathAt(int row) const {
    return row >= 0 && row < files_.size() ? files_.at(row).path : QString{};
}

void ThumbnailModel::invalidateThumbnail(const QString& path) {
    dimensions_.remove(path);
    bitDepths_.remove(path);
    const int row = pathToRow_.value(path, -1);
    if (row >= 0) {
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed,
                         {Qt::DecorationRole, DimensionsRole, BitDepthRole, ThumbnailUrlRole,
                          TechnicalLabelRole});
    }
}

void ThumbnailModel::rebuildPathIndex() {
    pathToRow_.clear();
    pathToRow_.reserve(files_.size());
    for (int row = 0; row < files_.size(); ++row) {
        pathToRow_.insert(files_.at(row).path, row);
    }
}

} // namespace ispview
