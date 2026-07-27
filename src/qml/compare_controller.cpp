#include "qml/compare_controller.h"
#include "qml/qml_image_canvas.h"

#include "core/comparison_pixel_probe.h"
#include "core/display_histogram.h"
#include "core/raw_plane_access.h"
#include "io/image_loader.h"
#include "io/raw_preset_store.h"
#include "platform/platform_services.h"

#include <QFileInfo>
#include <QPointer>
#include <QQuickWindow>
#include <QSettings>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>

namespace ispview {
namespace {

template <typename Bins>
double medianFor(const Bins& bins, quint64 samples) {
    if (samples == 0) return 0.0;
    const quint64 firstTarget = (samples - 1) / 2;
    const quint64 secondTarget = samples / 2;
    quint64 cumulative = 0;
    int first = -1;
    int second = -1;
    for (qsizetype index = 0; index < static_cast<qsizetype>(bins.size()); ++index) {
        cumulative += bins.at(static_cast<typename Bins::size_type>(index));
        if (first < 0 && cumulative > firstTarget) first = static_cast<int>(index);
        if (cumulative > secondTarget) { second = static_cast<int>(index); break; }
    }
    return (std::max(0, first) + std::max(0, second)) / 2.0;
}

QVariantList binsFor(const auto& bins) {
    QVariantList values;
    values.reserve(static_cast<qsizetype>(bins.size()));
    for (const auto value : bins) values.append(QVariant::fromValue<qulonglong>(value));
    return values;
}

QVariantMap histogramChannel(const QString& name, const QString& color, const auto& bins,
                            quint64 samples, double mean, double deviation, int minimum, int maximum) {
    return {{QStringLiteral("name"), name}, {QStringLiteral("color"), color},
            {QStringLiteral("bins"), binsFor(bins)}, {QStringLiteral("mean"), mean},
            {QStringLiteral("variance"), deviation * deviation}, {QStringLiteral("min"), minimum},
            {QStringLiteral("max"), maximum}, {QStringLiteral("median"), medianFor(bins, samples)}};
}

QVariantMap displayHistogramMap(const DisplayHistogram& histogram) {
    if (!histogram.isValid()) return {{QStringLiteral("valid"), false}, {QStringLiteral("summary"), QStringLiteral("Display histogram unavailable")}};
    QVariantList channels;
    const quint64 samples = static_cast<quint64>(std::max<qint64>(0, histogram.sampledPixelCount));
    channels << histogramChannel(QStringLiteral("Luma"), QStringLiteral("#F4F5F2"),
                                 histogram.luma.bins, samples, histogram.luma.mean,
                                 histogram.luma.standardDeviation, histogram.luma.minimum,
                                 histogram.luma.maximum);
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("maximumValue"), 255},
            {QStringLiteral("channels"), channels}};
}

QSize logicalFrameSize(const ImageFramePtr& frame) {
    if (!frame) return {};
    const RawPlaneAccessor accessor(*frame);
    return accessor.isValid() ? accessor.displaySize() : frame->descriptor.size;
}

} // namespace

CompareController::CompareController(ImageLoader* loader, QObject* parent)
    : QObject(parent), loader_(loader) {
    const QSettings settings;
    fileInformationVisible_ =
        settings.value(QStringLiteral("compare/fileInformationVisible"), true).toBool();
    exifVisible_ = settings.value(QStringLiteral("compare/exifVisible"), false).toBool();
    histogramVisible_ =
        settings.value(QStringLiteral("compare/histogramVisible"), false).toBool();
    pixelValueVisible_ =
        settings.value(QStringLiteral("compare/pixelValueVisible"), false).toBool();
}

ImageFramePtr CompareController::frame(int slot) const {
    return slot >= 0 && slot < frames_.size() ? frames_.at(slot) : ImageFramePtr{};
}

QString CompareController::fileText(int slot) const {
    const auto value = frame(slot);
    return value ? value->metadata.fileName : errors_.value(slot);
}

QString CompareController::cameraText(int slot) const {
    const auto value = frame(slot);
    if (!value || !value->metadata.camera) return QStringLiteral("No EXIF camera data");
    const auto& camera = *value->metadata.camera;
    QStringList parts;
    const QString model = QStringLiteral("%1 %2").arg(camera.make, camera.model).trimmed();
    if (!model.isEmpty()) parts.append(model);
    if (camera.exposureSeconds > 0.0) parts.append(camera.exposureSeconds < 1.0
        ? QStringLiteral("1/%1 s").arg(qRound(1.0 / camera.exposureSeconds))
        : QStringLiteral("%1 s").arg(camera.exposureSeconds, 0, 'g', 4));
    if (camera.aperture > 0.0) parts.append(QStringLiteral("f/%1").arg(camera.aperture, 0, 'g', 3));
    if (camera.iso > 0) parts.append(QStringLiteral("ISO %1").arg(camera.iso));
    if (camera.focalLengthMm > 0.0) parts.append(QStringLiteral("%1 mm").arg(camera.focalLengthMm, 0, 'g', 4));
    return parts.isEmpty() ? QStringLiteral("No EXIF camera data") : parts.join(QStringLiteral("  •  "));
}

void CompareController::setPaths(const QStringList& requested) {
    const QStringList bounded = requested.mid(0, 4);
    if (bounded == paths_) return;
    for (const LoadHandle& handle : previewHandles_) handle.cancel();
    for (const LoadHandle& handle : fullHandles_) handle.cancel();
    paths_ = bounded;
    frames_.fill({}, paths_.size());
    errors_.fill({}, paths_.size());
    generations_.fill(0, paths_.size());
    histogramGenerations_.fill(0, paths_.size());
    histogramRequested_.fill(false, paths_.size());
    displayHistograms_.fill({}, paths_.size());
    previewHandles_.fill({}, paths_.size());
    fullHandles_.fill({}, paths_.size());
    holdCandidate_ = false;
    emit pathsChanged();
    emit holdCandidateChanged();
    refreshCanvas(-1, true);
    for (int slot = 0; slot < paths_.size(); ++slot) requestFrame(slot, paths_.at(slot));
}

void CompareController::closeSession() {
    setPaths({});
    if (canvas_) canvas_->setFrames({}, -1, true);
    if (loader_) loader_->clearTransientCaches();
    QTimer::singleShot(500, this, [] { PlatformServices::releaseUnusedMemory(); });
}

void CompareController::requestFrame(int slot, const QString& path) {
    if (!loader_) return;
    std::optional<RawImageParameters> rawParameters = loader_->rawParameters(path);
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (!rawParameters && (suffix == QStringLiteral("raw") || suffix == QStringLiteral("yuv"))) {
        rawParameters = RawPresetStore::loadForFile(path);
        if (!rawParameters) {
            const RawImageParameters inferred = RawPresetStore::inferFromFileName(path);
            if (inferred.size.isValid()) rawParameters = inferred;
        }
        if (rawParameters) loader_->setRawParameters(path, *rawParameters);
    }
    const quint64 generation = ++generations_[slot];
    const qreal dpr = canvas_ && canvas_->window() ? canvas_->window()->devicePixelRatio() : 1.0;
    const int slotCount = std::max(1, static_cast<int>(paths_.size()));
    QSize previewSize = canvas_ ? QSize(qRound(canvas_->width() * dpr / slotCount),
                                       qRound(canvas_->height() * dpr))
                                : QSize(1280, 1000);
    previewSize = previewSize.expandedTo(QSize(640, 480)).boundedTo(QSize(1920, 1600));
    const QPointer<CompareController> self(this);
    previewHandles_[slot] = loader_->request(
        generation, {path, DecodePurpose::Preview, previewSize, rawParameters},
        [self, slot, path, generation, rawParameters](quint64 id, const DecodeResult& result) {
            if (!self || id != generation || slot < 0 || slot >= self->generations_.size() ||
                self->generations_.at(slot) != generation) return;
            if (!result.frame) {
                self->errors_[slot] = result.error;
                emit self->loadFailed(slot, result.error);
                ++self->revision_;
                emit self->revisionChanged();
                return;
            }
            self->errors_[slot].clear();
            self->frames_[slot] = result.frame;
            self->refreshCanvas(slot, true);
            self->clearHistograms(slot);
            if (self->histogramRequested_.value(slot)) self->requestHistogram(slot);
            emit self->frameChanged(slot, false);
            ++self->revision_;
            emit self->revisionChanged();
            self->fullHandles_[slot] = self->loader_->request(
                generation, {path, DecodePurpose::Full, {}, rawParameters},
                [self, slot, generation](quint64 fullId, const DecodeResult& full) {
                    if (!self || fullId != generation || slot < 0 ||
                        slot >= self->generations_.size() ||
                        self->generations_.at(slot) != generation || !full.frame) return;
                    self->frames_[slot] = full.frame;
                    self->refreshCanvas(slot, false);
                    self->clearHistograms(slot);
                    if (self->histogramRequested_.value(slot)) self->requestHistogram(slot);
                    emit self->frameChanged(slot, true);
                    ++self->revision_;
                    emit self->revisionChanged();
                }, RequestOptions{LoadCategory::Interactive, 0,
                                  QStringLiteral("compare-full")});
        }, RequestOptions{LoadCategory::Interactive, 20,
                          QStringLiteral("compare-preview")});
}

void CompareController::setPresentationMode(int mode) {
    mode = std::clamp(mode, 0, 1); if (presentationMode_ == mode) return;
    setHoldCandidate(false);
    presentationMode_ = mode;
    if (canvas_) canvas_->setPresentationMode(mode);
    emit presentationModeChanged();
}
void CompareController::setSplitAmount(qreal amount) {
    amount = std::clamp(amount, qreal(0), qreal(1)); if (qFuzzyCompare(splitAmount_, amount)) return;
    splitAmount_ = amount;
    if (canvas_) canvas_->setCompareAmount(amount);
    emit splitAmountChanged();
}
void CompareController::setSynchronized(bool enabled) {
    if (synchronized_ == enabled) return;
    synchronized_ = enabled;
    if (canvas_) canvas_->setSynchronized(enabled);
    emit synchronizedChanged();
}
void CompareController::setHoldCandidate(bool active) {
    if (frames_.size() != 2 || !frames_.value(0) || !frames_.value(1) || presentationMode_ != 0)
        active = false;
    if (holdCandidate_ == active) return;
    holdCandidate_ = active;
    applyHoldFrame();
    emit holdCandidateChanged();
}

void CompareController::setFileInformationVisible(bool visible) {
    if (fileInformationVisible_ == visible) return;
    fileInformationVisible_ = visible;
    QSettings().setValue(QStringLiteral("compare/fileInformationVisible"), visible);
    emit fileInformationVisibleChanged();
}

void CompareController::setExifVisible(bool visible) {
    if (exifVisible_ == visible) return;
    exifVisible_ = visible;
    QSettings().setValue(QStringLiteral("compare/exifVisible"), visible);
    emit exifVisibleChanged();
}

void CompareController::setHistogramVisible(bool visible) {
    if (histogramVisible_ == visible) return;
    histogramVisible_ = visible;
    QSettings().setValue(QStringLiteral("compare/histogramVisible"), visible);
    emit histogramVisibleChanged();
}

void CompareController::setPixelValueVisible(bool visible) {
    if (pixelValueVisible_ == visible) return;
    pixelValueVisible_ = visible;
    QSettings().setValue(QStringLiteral("compare/pixelValueVisible"), visible);
    emit pixelValueVisibleChanged();
}

void CompareController::applyHoldFrame() {
    refreshCanvas();
}

void CompareController::fitAll() {
    if (canvas_) canvas_->fitAll();
}

void CompareController::actualPixelsAll() {
    if (canvas_) canvas_->actualPixelsAll();
}

void CompareController::attachCanvas(QObject* object) {
    auto* canvas = qobject_cast<QmlImageCanvas*>(object);
    if (!canvas) return;
    canvas_ = canvas;
    refreshCanvas(-1, true);
}

void CompareController::refreshCanvas(int changedSlot, bool resetChangedView) {
    if (!canvas_) return;
    QVector<ImageFramePtr> displayedFrames = frames_;
    if (holdCandidate_ && displayedFrames.size() == 2 && displayedFrames.value(1))
        displayedFrames[0] = displayedFrames.at(1);
    canvas_->setFrames(displayedFrames, changedSlot, resetChangedView);
    canvas_->setPresentationMode(presentationMode_);
    canvas_->setCompareAmount(splitAmount_);
    canvas_->setSynchronized(synchronized_);
}

QVariantList CompareController::pixelTexts(int sourceSlot, int x, int y) const {
    const auto source = frame(sourceSlot);
    const QSize sourceSize = logicalFrameSize(source);
    if (!source || sourceSize.isEmpty()) return {};
    const QPointF normalized = ComparisonPixelProbe::normalizedPixelCenter({x, y}, sourceSize);
    QVariantList values;
    values.reserve(frames_.size());
    for (const auto& candidate : frames_) {
        if (!candidate) {
            values.append(QString{});
            continue;
        }
        const auto sample = ComparisonPixelProbe::sample(*candidate, normalized);
        values.append(sample.valid
                          ? QStringLiteral("(%1,%2) %3")
                                .arg(sample.displayPixel.x())
                                .arg(sample.displayPixel.y())
                                .arg(sample.displayValueText())
                          : QStringLiteral("Outside image"));
    }
    return values;
}

void CompareController::requestHistogram(int slot) {
    if (slot < 0 || slot >= frames_.size()) return;
    histogramRequested_[slot] = true;
    if (!frames_.value(slot)) return;
    const quint64 generation = ++histogramGenerations_[slot];
    const ImageFramePtr frame = frames_.at(slot);
    const QPointer<CompareController> self(this);
    QThreadPool::globalInstance()->start([self, frame, slot, generation] {
        const QVariantMap result = displayHistogramMap(DisplayHistogramAnalyzer::analyze(*frame));
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, slot, generation, result] {
            if (self) self->completeHistogram(slot, generation, result);
        }, Qt::QueuedConnection);
    }, -20);
}

QVariantMap CompareController::histogram(int slot) const {
    return slot >= 0 && slot < frames_.size() ? displayHistograms_.value(slot) : QVariantMap{};
}

void CompareController::completeHistogram(int slot, quint64 generation, QVariantMap value) {
    if (slot < 0 || slot >= histogramGenerations_.size()
        || histogramGenerations_.at(slot) != generation) return;
    displayHistograms_[slot] = std::move(value);
    ++histogramRevision_;
    emit histogramChanged(slot);
    emit histogramRevisionChanged();
}

void CompareController::clearHistograms(int slot) {
    if (slot < 0 || slot >= frames_.size()) return;
    ++histogramGenerations_[slot];
    displayHistograms_[slot] = {};
    ++histogramRevision_;
    emit histogramChanged(slot);
    emit histogramRevisionChanged();
}

} // namespace ispview
