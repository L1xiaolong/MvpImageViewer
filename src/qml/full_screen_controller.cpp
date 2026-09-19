#include "diagnostics/diagnostics.h"
#include "qml/full_screen_controller.h"

#include "core/comparison_pixel_probe.h"
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
    fullLoadTimer_.setSingleShot(true);
    fullLoadTimer_.setInterval(220);
    connect(&fullLoadTimer_, &QTimer::timeout, this, [this] {
        if (!frame_ || fullRequested_ || currentPath().isEmpty()) {
            return;
        }
        if (loader_->canAutomaticallyLoadFull({frame_})) {
            requestFullFrame(currentPath(), generation_);
        }
    });
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
    if (!info.isFile())
        return {};

    QString text =
        QLocale().formattedDataSize(info.size(), 1, QLocale::DataSizeTraditionalFormat);
    // QLocale separates the number and unit with locale-dependent whitespace (often a normal,
    // non-breaking, or narrow non-breaking space). The fullscreen HUD intentionally keeps the
    // compact "100MB" form regardless of the active locale.
    for (qsizetype index = text.size(); index > 0; --index) {
        if (text.at(index - 1).isSpace())
            text.remove(index - 1, 1);
    }
    return text;
}

QSize FullScreenController::imageSize() const {
    return frame_ ? ComparisonPixelProbe::logicalFrameSize(*frame_) : QSize{};
}

QString FullScreenController::positionText() const {
    return currentIndex_ >= 0 ? QStringLiteral("%1 / %2").arg(currentIndex_ + 1).arg(paths_.size())
                              : QString{};
}

void FullScreenController::open(const QStringList& requestedPaths, int initialIndex) {
    paths_ = requestedPaths;
    if (!paths_.isEmpty()) {
        diagnostics::event(diagnostics::Level::Info, diagnostics::browse(),
                           QStringLiteral("fullscreen.session_open"),
                           {{QStringLiteral("slots"), static_cast<int>(paths_.size())},
                            {QStringLiteral("index"), initialIndex}},
                           true);
    }
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
    if (!paths_.isEmpty())
        diagnostics::event(diagnostics::Level::Info, diagnostics::browse(),
                           QStringLiteral("fullscreen.session_close"),
                           {{QStringLiteral("slots"), static_cast<int>(paths_.size())}}, true);
    fullLoadTimer_.stop();
    previewHandle_.cancel();
    fullHandle_.cancel();
    ++generation_;
    paths_.clear();
    currentIndex_ = -1;
    loading_ = false;
    fullRequested_ = false;
    exactResolutionRequested_ = false;
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
    // Hovering a RAW/YUV preview promotes it to the full decode so the readout can show exact
    // source samples, exactly like the gallery probe does.
    connect(canvas_, &QmlImageCanvas::pixelProbeFullResolutionRequested, this, [this](int) {
        if (frame_ && !fullRequested_) {
            requestFullFrame(currentPath(), generation_);
        }
    });
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
    exactResolutionRequested_ = true;
    fullLoadTimer_.stop();
    if (frame_ && !fullRequested_) {
        requestFullFrame(currentPath(), generation_);
    }
    if (canvas_) canvas_->actualPixelsAll();
}

void FullScreenController::reload() {
    if (currentIndex_ >= 0)
        showIndex(currentIndex_);
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
    // Navigation stays out of the crash breadcrumbs: paging through a large folder would flush
    // every other operator out of the fixed-size ring.
    diagnostics::event(diagnostics::Level::Debug, diagnostics::browse(),
                       QStringLiteral("fullscreen.index_change"),
                       {{QStringLiteral("index"), index}}, false);
    fullLoadTimer_.stop();
    previewHandle_.cancel();
    fullHandle_.cancel();
    currentIndex_ = index;
    frame_.reset();
    loading_ = true;
    fullRequested_ = false;
    exactResolutionRequested_ = false;
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
        if (self->exactResolutionRequested_) {
            self->requestFullFrame(path, generation);
        } else {
            self->scheduleFullFrame(path, generation);
        }
    }, RequestOptions{LoadCategory::Interactive, 20, QStringLiteral("fullscreen-preview")});
}

void FullScreenController::scheduleFullFrame(const QString& path, quint64 generation) {
    if (path != currentPath() || generation != generation_ || fullRequested_) {
        return;
    }
    fullLoadTimer_.start();
}

void FullScreenController::requestFullFrame(const QString& path, quint64 generation) {
    if (fullRequested_ || path != currentPath() || generation != generation_) {
        return;
    }
    fullRequested_ = true;
    const QPointer<FullScreenController> self(this);
    fullHandle_ = loader_->request(generation, {path, DecodePurpose::Full, {}},
                     [self, generation, path](quint64 id, const DecodeResult& result) {
        if (!self || id != generation || self->generation_ != generation ||
            self->currentPath() != path) return;
        if (!result.frame) {
            self->fullRequested_ = false;
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
