#include "qml/raw_parameters_controller.h"

#include "io/image_loader.h"
#include "io/raw_preset_store.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>

namespace ispview {
namespace {

QVariantList listFor(const auto& values) {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(values.size()));
    for (const auto value : values) result.append(value);
    return result;
}

QVariantMap valuesFor(const RawImageParameters& value) {
    return {{QStringLiteral("format"), static_cast<int>(value.format)},
            {QStringLiteral("width"), value.size.width()},
            {QStringLiteral("height"), value.size.height()},
            {QStringLiteral("rowStride"), value.rowStride},
            {QStringLiteral("chromaStride"), value.chromaStride},
            {QStringLiteral("headerOffset"), value.headerOffset},
            {QStringLiteral("validBits"), value.validBitsOverride},
            {QStringLiteral("bayerPattern"), static_cast<int>(value.bayerPattern)},
            {QStringLiteral("bayerSampling"), static_cast<int>(value.bayerSampling)},
            {QStringLiteral("yuvMatrix"), static_cast<int>(value.yuvMatrix)},
            {QStringLiteral("yuvPrimaries"), static_cast<int>(value.yuvPrimaries)},
            {QStringLiteral("yuvTransfer"), static_cast<int>(value.yuvTransfer)},
            {QStringLiteral("chromaLocation"), static_cast<int>(value.chromaLocation)},
            {QStringLiteral("range"), static_cast<int>(value.range)},
            {QStringLiteral("orientation"), static_cast<int>(value.orientation)},
            {QStringLiteral("littleEndian"), value.littleEndian},
            {QStringLiteral("msbAligned"), value.msbAligned},
            {QStringLiteral("demosaic"), value.demosaic},
            {QStringLiteral("blackLevel"), value.blackLevel},
            {QStringLiteral("whiteLevel"), value.whiteLevel},
            {QStringLiteral("whiteBalance"), listFor(value.whiteBalanceGains)},
            {QStringLiteral("colorMatrix"), listFor(value.colorCorrectionMatrix)},
            {QStringLiteral("displayGamma"), value.displayGamma}};
}

template <std::size_t Size>
void copyNumbers(const QVariantList& source, std::array<double, Size>& destination) {
    for (qsizetype index = 0;
         index < std::min<qsizetype>(source.size(), static_cast<qsizetype>(Size)); ++index)
        destination.at(static_cast<std::size_t>(index)) = source.at(index).toDouble();
}

} // namespace

RawParametersController::RawParametersController(ImageLoader* loader, QObject* parent)
    : QObject(parent), loader_(loader), changeTimer_(new QTimer(this)) {
    Q_ASSERT(loader_);
    changeTimer_->setSingleShot(true);
    changeTimer_->setInterval(150);
    connect(changeTimer_, &QTimer::timeout, this, &RawParametersController::applyEditedParameters);
    refreshPresets();
}

QString RawParametersController::fileName() const { return QFileInfo(path_).fileName(); }

bool RawParametersController::yuvFormat() const { return parameters().isYuv(); }

bool RawParametersController::raw16Format() const {
    return parameters().format == RawPixelFormat::Raw16;
}

bool RawParametersController::endianControlsVisible() const {
    const RawPixelFormat format = parameters().format;
    return format == RawPixelFormat::P010 || format == RawPixelFormat::Raw16;
}

QString RawParametersController::suggestedPresetName() const {
    const RawImageParameters value = parameters();
    return QStringLiteral("%1_%2_%3")
        .arg(value.size.width()).arg(value.size.height())
        .arg(rawPixelFormatName(value.format).replace(QLatin1Char(' '), QLatin1Char('_')));
}

void RawParametersController::loadPath(const QString& requestedPath) {
    path_ = QFileInfo(requestedPath).absoluteFilePath();
    std::optional<RawImageParameters> value = loader_->rawParameters(path_);
    if (!value) value = RawPresetStore::loadForFile(path_);
    if (!value) value = RawPresetStore::inferFromFileName(path_);
    setParameters(value.value_or(RawImageParameters{}));
}

void RawParametersController::setValue(const QString& key, const QVariant& value) {
    if (path_.isEmpty() || !values_.contains(key) || values_.value(key) == value) return;
    values_.insert(key, value);
    selectedPreset_.clear();
    emit stateChanged();
    changeTimer_->start();
}

void RawParametersController::setListValue(const QString& key, int index,
                                           const QVariant& value) {
    QVariantList values = values_.value(key).toList();
    if (index < 0 || index >= values.size() || values.at(index) == value) return;
    values[index] = value;
    setValue(key, values);
}

void RawParametersController::selectPreset(const QString& name) {
    if (name.isEmpty()) {
        selectedPreset_.clear();
        emit stateChanged();
        return;
    }
    const auto preset = RawPresetStore::loadNamedPreset(name);
    if (!preset) {
        emit notificationRequested(QStringLiteral("Unable to load configuration: %1").arg(name),
                                   true);
        return;
    }
    setParameters(*preset, name);
    applyEditedParameters();
}

QString RawParametersController::savePreset(const QString& requestedName,
                                            bool applyToFolderAfter) {
    const QString name = requestedName.trimmed();
    if (name.isEmpty()) return QStringLiteral("Enter a configuration name.");
    if (!RawPresetStore::saveNamedPreset(name, parameters()))
        return QStringLiteral("The configuration is incomplete or invalid.");
    refreshPresets();
    selectedPreset_ = name;
    emit stateChanged();
    if (applyToFolderAfter) {
        const QString error = applyToFolder();
        if (!error.isEmpty()) return error;
    }
    emit notificationRequested(QStringLiteral("Configuration saved: %1").arg(name), false);
    return {};
}

QString RawParametersController::deleteSelectedPreset() {
    if (selectedPreset_.isEmpty()) return QStringLiteral("Select a saved configuration.");
    const QString name = selectedPreset_;
    if (!RawPresetStore::removeNamedPreset(name))
        return QStringLiteral("Unable to delete configuration: %1").arg(name);
    selectedPreset_.clear();
    refreshPresets();
    emit stateChanged();
    emit notificationRequested(QStringLiteral("Configuration deleted: %1").arg(name), false);
    return {};
}

QString RawParametersController::applyToFolder() {
    if (path_.isEmpty()) return QStringLiteral("No RAW/YUV file is selected.");
    RawImageParameters value = parameters();
    value.frameIndex = 0;
    if (availableFrameCount(QFileInfo(path_).size(), value) <= 0)
        return QStringLiteral("The parameters do not describe one complete frame.");
    RawPresetStore::saveForFile(path_, value);
    const QFileInfo source(path_);
    const QFileInfoList candidates = source.dir().entryInfoList(
        QDir::Files | QDir::Readable | QDir::NoDotAndDotDot, QDir::Name);
    int appliedCount = 0;
    for (const QFileInfo& candidate : candidates) {
        if (candidate.suffix().compare(source.suffix(), Qt::CaseInsensitive) != 0) continue;
        loader_->setRawParameters(candidate.absoluteFilePath(), value);
        ++appliedCount;
    }
    emit parametersApplied(path_);
    emit notificationRequested(
        QStringLiteral("Configuration applied to %1 %2 file(s) in this folder")
            .arg(appliedCount).arg(source.suffix().toUpper()), false);
    return {};
}

RawImageParameters RawParametersController::parameters() const {
    RawImageParameters value = baseParameters_;
    value.format = static_cast<RawPixelFormat>(values_.value(QStringLiteral("format")).toInt());
    value.size = {values_.value(QStringLiteral("width")).toInt(),
                  values_.value(QStringLiteral("height")).toInt()};
    value.rowStride = values_.value(QStringLiteral("rowStride")).toLongLong();
    value.chromaStride = values_.value(QStringLiteral("chromaStride")).toLongLong();
    value.headerOffset = values_.value(QStringLiteral("headerOffset")).toLongLong();
    value.frameIndex = 0;
    value.validBitsOverride = value.format == RawPixelFormat::Raw16
        ? values_.value(QStringLiteral("validBits")).toInt() : 0;
    value.bayerPattern = static_cast<BayerPattern>(
        values_.value(QStringLiteral("bayerPattern")).toInt());
    value.bayerSampling = static_cast<BayerSampling>(
        values_.value(QStringLiteral("bayerSampling")).toInt());
    value.yuvMatrix = static_cast<YuvMatrix>(values_.value(QStringLiteral("yuvMatrix")).toInt());
    value.yuvPrimaries = static_cast<YuvPrimaries>(
        values_.value(QStringLiteral("yuvPrimaries")).toInt());
    value.yuvTransfer = static_cast<YuvTransfer>(
        values_.value(QStringLiteral("yuvTransfer")).toInt());
    value.chromaLocation = static_cast<ChromaLocation>(
        values_.value(QStringLiteral("chromaLocation")).toInt());
    value.range = static_cast<QuantizationRange>(values_.value(QStringLiteral("range")).toInt());
    value.orientation = static_cast<ImageOrientation>(
        values_.value(QStringLiteral("orientation")).toInt());
    value.littleEndian = values_.value(QStringLiteral("littleEndian")).toBool();
    value.msbAligned = values_.value(QStringLiteral("msbAligned")).toBool();
    value.demosaic = value.isYuv() ? false
                                   : values_.value(QStringLiteral("demosaic")).toBool();
    value.blackLevel = values_.value(QStringLiteral("blackLevel")).toInt();
    value.whiteLevel = values_.value(QStringLiteral("whiteLevel")).toInt();
    copyNumbers(values_.value(QStringLiteral("whiteBalance")).toList(), value.whiteBalanceGains);
    copyNumbers(values_.value(QStringLiteral("colorMatrix")).toList(),
                value.colorCorrectionMatrix);
    value.displayGamma = values_.value(QStringLiteral("displayGamma")).toDouble();
    return value;
}

void RawParametersController::setParameters(const RawImageParameters& value,
                                            const QString& presetName) {
    changeTimer_->stop();
    baseParameters_ = value;
    baseParameters_.frameIndex = 0;
    values_ = valuesFor(baseParameters_);
    selectedPreset_ = presetName;
    refreshPresets();
    emit stateChanged();
}

void RawParametersController::refreshPresets() { presetNames_ = RawPresetStore::namedPresetNames(); }

void RawParametersController::applyEditedParameters() {
    if (path_.isEmpty()) return;
    RawImageParameters value = parameters();
    if (availableFrameCount(QFileInfo(path_).size(), value) <= 0) {
        emit notificationRequested(
            QStringLiteral("RAW/YUV parameters do not describe one complete frame"), true);
        return;
    }
    loader_->setRawParameters(path_, value);
    baseParameters_ = value;
    emit parametersApplied(path_);
}

} // namespace ispview
