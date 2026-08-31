#pragma once

#include "io/directory_scanner.h"

#include <QAbstractListModel>
#include <QHash>
#include <QMimeData>
#include <QPixmap>

namespace ispview {

class ImageLoader;

// Presentation-neutral model shared by the production QML browser panes.
class ThumbnailModel final : public QAbstractListModel {
    Q_OBJECT

  public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        SizeRole,
        ModifiedRole,
        DirectoryRole,
        TypeRole,
        DimensionsRole,
        BitDepthRole,
        FileSizeTextRole,
        ThumbnailUrlRole,
        FileNameRole,
        TechnicalLabelRole,
        SelectedRole,
        SelectionOrdinalRole
    };

    explicit ThumbnailModel(ImageLoader* loader, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    [[nodiscard]] Qt::DropActions supportedDragActions() const override;

    void setFiles(QVector<ImageFileRecord> files);
    void appendFiles(const QVector<ImageFileRecord>& files);
    void updateFiles(const QVector<ImageFileRecord>& files);
    void invalidateThumbnail(const QString& path);
    void setSelectedPaths(const QStringList& paths);
    [[nodiscard]] QString pathAt(int row) const;
    [[nodiscard]] const QVector<ImageFileRecord>& files() const { return files_; }

  private:
    void rebuildPathIndex();

    ImageLoader* loader_;
    QVector<ImageFileRecord> files_;
    QHash<QString, int> pathToRow_;
    mutable QHash<QString, QSize> dimensions_;
    mutable QHash<QString, int> bitDepths_;
    QStringList selectedPaths_;
    QHash<QString, int> selectedOrdinals_;
    QPixmap placeholder_;
    QPixmap folderPlaceholder_;
};

} // namespace ispview
