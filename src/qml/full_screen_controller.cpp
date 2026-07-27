#include "qml/full_screen_controller.h"

#include "io/image_loader.h"
#include "io/single_file_rename.h"
#include "platform/platform_services.h"
#include "qml/qml_image_canvas.h"
#include "browser/file_clipboard.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QLocale>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>

#include <algorithm>

namespace ispview {

FullScreenController::FullScreenController(ImageLoader* loader, QObject* parent)
    : QObject(parent), loader_(loader) {
    Q_ASSERT(loader_);
}

QString FullScreenController::currentPath() const {
    return currentIndex_ >= 0 && currentIndex_ < paths_.size() ? paths_.at(currentIndex_)
                                                               : QString{};
}

QString FullScreenController::fileName() const { return QFileInfo(currentPath()).fileName(); }

QString FullScreenController::fileType() const {
    const QString suffix = QFileInfo(currentPath()).suffix().toUpper();
    return suffix.isEmpty() ? QStringLiteral("IMAGE") : suffix;
}

QString FullScreenController::fileSizeText() const {
    const QFileInfo info(currentPath());
    return info.isFile()
               ? QLocale().formattedDataSize(info.size(), 1, QLocale::DataSizeTraditionalFormat)
               : QString{};
}

QString FullScreenController::positionText() const {
    return currentIndex_ >= 0 ? QStringLiteral("%1 / %2").arg(currentIndex_ + 1).arg(paths_.size())
                              : QString{};
}

void FullScreenController::open(const QStringList& requestedPaths, int initialIndex) {
    paths_ = requestedPaths;
    if (paths_.isEmpty()) {
        currentIndex_ = -1;
        frame_.reset();
        refreshCanvas(true);
        emit stateChanged();
        emit closeRequested();
        return;
    }
    showIndex(std::clamp(initialIndex, 0, static_cast<int>(paths_.size()) - 1));
}

void FullScreenController::closeSession() {
    previewHandle_.cancel();
    fullHandle_.cancel();
    ++generation_;
    paths_.clear();
    currentIndex_ = -1;
    loading_ = false;
    errorText_.clear();
    frame_.reset();
    refreshCanvas(true);
    if (loader_) loader_->clearTransientCaches();
    QTimer::singleShot(500, this, [] { PlatformServices::releaseUnusedMemory(); });
    emit stateChanged();
}

void FullScreenController::attachCanvas(QObject* object) {
    auto* canvas = qobject_cast<QmlImageCanvas*>(object);
    if (!canvas) return;
    canvas_ = canvas;
    canvas_->setPresentationMode(0);
    canvas_->setSynchronized(false);
    refreshCanvas(true);
}

void FullScreenController::showPrevious() {
    if (canGoPrevious()) showIndex(currentIndex_ - 1);
}

void FullScreenController::showNext() {
    if (canGoNext()) showIndex(currentIndex_ + 1);
}

void FullScreenController::fitImage() {
    if (canvas_) canvas_->fitAll();
}

void FullScreenController::actualPixels() {
    if (canvas_) canvas_->actualPixelsAll();
}

void FullScreenController::copyCurrent(bool cut) {
    if (!currentPath().isEmpty()) FileClipboard::setPaths({currentPath()}, cut);
}

QString FullScreenController::renameCurrentTo(const QString& requestedName) {
    const QFileInfo source(currentPath());
    if (!source.exists()) return QStringLiteral("The current file no longer exists.");
    const QString newName = requestedName.trimmed();
    if (newName.isEmpty()) return QStringLiteral("Enter a name.");
    if (newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\')) ||
        newName == QStringLiteral(".") || newName == QStringLiteral(".."))
        return QStringLiteral("The name contains unsupported characters.");
    if (newName == source.fileName()) return {};
    const QString destination = source.dir().filePath(newName);
    QString error;
    if (!SingleFileRename::execute(source.absoluteFilePath(), destination, &error)) return error;
    paths_[currentIndex_] = destination;
    emit filesystemChanged();
    showIndex(currentIndex_);
    return {};
}

QString FullScreenController::moveCurrentToTrash() {
    const QString path = currentPath();
    if (path.isEmpty()) return QStringLiteral("No image is open.");
    if (!QFile::moveToTrash(path))
        return QStringLiteral("The file could not be moved to the system Trash.");
    paths_.removeAt(currentIndex_);
    emit filesystemChanged();
    if (paths_.isEmpty()) {
        currentIndex_ = -1;
        frame_.reset();
        refreshCanvas(true);
        emit stateChanged();
        emit closeRequested();
        return {};
    }
    showIndex(std::min(currentIndex_, static_cast<int>(paths_.size()) - 1));
    return {};
}

QString FullScreenController::revealCurrent() {
    if (currentPath().isEmpty() || PlatformServices::revealInFileManager(currentPath())) return {};
    return QStringLiteral("Could not open the system file manager.");
}

void FullScreenController::showIndex(int index) {
    if (index < 0 || index >= paths_.size()) return;
    previewHandle_.cancel();
    fullHandle_.cancel();
    currentIndex_ = index;
    frame_.reset();
    loading_ = true;
    errorText_.clear();
    const quint64 generation = ++generation_;
    const QString path = currentPath();
    refreshCanvas(true);
    emit stateChanged();

    const auto rawParameters = loader_->rawParameters(path);
    const qreal dpr = canvas_ && canvas_->window() ? canvas_->window()->devicePixelRatio() : 1.0;
    QSize previewSize = canvas_ ? QSize(qRound(canvas_->width() * dpr),
                                       qRound(canvas_->height() * dpr))
                                : QSize(1920, 1200);
    previewSize = previewSize.expandedTo(QSize(960, 720)).boundedTo(QSize(2560, 1600));
    if (rawParameters && !rawParameters->isYuv()) {
        previewSize = previewSize.boundedTo(QSize(1280, 800));
    }
    const QPointer<FullScreenController> self(this);
    previewHandle_ = loader_->request(generation, {path, DecodePurpose::Preview, previewSize},
                     [self, generation, path](quint64 id, const DecodeResult& result) {
        if (!self || id != generation || self->generation_ != generation ||
            self->currentPath() != path) return;
        if (!result.frame) {
            self->loading_ = false;
            self->errorText_ = result.error;
            emit self->stateChanged();
            return;
        }
        self->frame_ = result.frame;
        self->loading_ = false;
        self->refreshCanvas(true);
        emit self->stateChanged();
        self->requestFullFrame(path, generation);
    }, RequestOptions{LoadCategory::Interactive, 20, QStringLiteral("fullscreen-preview")});
}

void FullScreenController::requestFullFrame(const QString& path, quint64 generation) {
    const QPointer<FullScreenController> self(this);
    fullHandle_ = loader_->request(generation, {path, DecodePurpose::Full, {}},
                     [self, generation, path](quint64 id, const DecodeResult& result) {
        if (!self || id != generation || self->generation_ != generation ||
            self->currentPath() != path) return;
        if (!result.frame) {
            self->errorText_ = result.error;
            emit self->stateChanged();
            return;
        }
        self->frame_ = result.frame;
        self->refreshCanvas(false);
        emit self->stateChanged();
    }, RequestOptions{LoadCategory::Interactive, 0, QStringLiteral("fullscreen-full")});
}

void FullScreenController::refreshCanvas(bool resetView) {
    if (!canvas_) return;
    canvas_->setFrames({frame_}, 0, resetView);
    canvas_->setPresentationMode(0);
    canvas_->setSynchronized(false);
}

} // namespace ispview
