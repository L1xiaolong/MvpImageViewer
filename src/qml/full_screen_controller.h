#pragma once

#include "core/image_types.h"
#include "io/image_loader.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

namespace ispview {

class QmlImageCanvas;

// Owns a full-screen viewing session while the QML page owns every visual and interaction
// surface. The controller deliberately exposes file operations as small commands rather than
// creating dialogs, menus, or windows.
class FullScreenController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList paths READ paths NOTIFY stateChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY stateChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(QString fileType READ fileType NOTIFY stateChanged)
    Q_PROPERTY(QString fileSizeText READ fileSizeText NOTIFY stateChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY stateChanged)
    Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY stateChanged)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)

  public:
    explicit FullScreenController(ImageLoader* loader, QObject* parent = nullptr);

    [[nodiscard]] QStringList paths() const { return paths_; }
    [[nodiscard]] int currentIndex() const { return currentIndex_; }
    [[nodiscard]] QString currentPath() const;
    [[nodiscard]] QString fileName() const;
    [[nodiscard]] QString fileType() const;
    [[nodiscard]] QString fileSizeText() const;
    [[nodiscard]] QString positionText() const;
    [[nodiscard]] bool canGoPrevious() const { return currentIndex_ > 0; }
    [[nodiscard]] bool canGoNext() const {
        return currentIndex_ >= 0 && currentIndex_ + 1 < paths_.size();
    }
    [[nodiscard]] bool loading() const { return loading_; }
    [[nodiscard]] QString errorText() const { return errorText_; }

    Q_INVOKABLE void open(const QStringList& paths, int initialIndex);
    Q_INVOKABLE void closeSession();
    Q_INVOKABLE void attachCanvas(QObject* canvas);
    Q_INVOKABLE void showPrevious();
    Q_INVOKABLE void showNext();
    Q_INVOKABLE void fitImage();
    Q_INVOKABLE void actualPixels();
    Q_INVOKABLE void copyCurrent(bool cut = false);
    Q_INVOKABLE QString renameCurrentTo(const QString& requestedName);
    Q_INVOKABLE QString moveCurrentToTrash();
    Q_INVOKABLE QString revealCurrent();

  signals:
    void stateChanged();
    void filesystemChanged();
    void closeRequested();

  private:
    void showIndex(int index);
    void scheduleFullFrame(const QString& path, quint64 generation);
    void requestFullFrame(const QString& path, quint64 generation);
    void refreshCanvas(bool resetView);

    ImageLoader* loader_ = nullptr;
    QPointer<QmlImageCanvas> canvas_;
    QStringList paths_;
    int currentIndex_ = -1;
    quint64 generation_ = 0;
    bool loading_ = false;
    bool fullRequested_ = false;
    bool exactResolutionRequested_ = false;
    QString errorText_;
    ImageFramePtr frame_;
    LoadHandle previewHandle_;
    LoadHandle fullHandle_;
    QTimer fullLoadTimer_;
};

} // namespace ispview
