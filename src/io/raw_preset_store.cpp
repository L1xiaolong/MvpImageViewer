#include "io/raw_preset_store.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QVariantMap>

#include <cstddef>
#include <functional>
#include <limits>

namespace ispview {
namespace {

constexpr auto kNamedPresetsKey = "rawPresets/named";
constexpr auto kFilenameRulesKey = "rawPresets/filenameRules";

// Settings fallback used when a file has no sidecar. The key deliberately covers one directory
// and suffix instead of a single file: RawParametersController::applyToFolder() persists one
// layout for every file of that suffix in the folder. Per-file parameters always win, because
// loadForFile() prefers the sidecar written by saveSidecar().
QString keyForFile(const QString& path) {
    const QFileInfo info(path);
    const QByteArray identity =
        (info.absolutePath() + QLatin1Char('|') + info.suffix().toLower()).toUtf8();
    return QStringLiteral("rawPresets/%1")
        .arg(QString::fromLatin1(
            QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex()));
}

QVariantMap toMap(const RawImageParameters& parameters) {
    QVariantMap values;
    values.insert(QStringLiteral("width"), parameters.size.width());
    values.insert(QStringLiteral("height"), parameters.size.height());
    values.insert(QStringLiteral("format"), static_cast<int>(parameters.format));
    values.insert(QStringLiteral("offset"), parameters.headerOffset);
    values.insert(QStringLiteral("rowStride"), parameters.rowStride);
    values.insert(QStringLiteral("chromaStride"), parameters.chromaStride);
    values.insert(QStringLiteral("frameIndex"), parameters.frameIndex);
    values.insert(QStringLiteral("littleEndian"), parameters.littleEndian);
    values.insert(QStringLiteral("msbAligned"), parameters.msbAligned);
    values.insert(QStringLiteral("validBits"), parameters.validBitsOverride);
    values.insert(QStringLiteral("bayerPattern"), static_cast<int>(parameters.bayerPattern));
    values.insert(QStringLiteral("bayerSampling"), static_cast<int>(parameters.bayerSampling));
    values.insert(QStringLiteral("matrix"), static_cast<int>(parameters.yuvMatrix));
    values.insert(QStringLiteral("range"), static_cast<int>(parameters.range));
    values.insert(QStringLiteral("orientation"), static_cast<int>(parameters.orientation));
    values.insert(QStringLiteral("blackLevel"), parameters.blackLevel);
    values.insert(QStringLiteral("whiteLevel"), parameters.whiteLevel);
    values.insert(QStringLiteral("demosaic"), parameters.demosaic);
    values.insert(QStringLiteral("wbRed"), parameters.whiteBalanceGains[0]);
    values.insert(QStringLiteral("wbGreen"), parameters.whiteBalanceGains[1]);
    values.insert(QStringLiteral("wbBlue"), parameters.whiteBalanceGains[2]);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const auto index = static_cast<std::size_t>(row * 3 + column);
            values.insert(QStringLiteral("ccm%1%2").arg(row).arg(column),
                          parameters.colorCorrectionMatrix[index]);
        }
    }
    values.insert(QStringLiteral("displayGamma"), parameters.displayGamma);
    return values;
}

bool parametersAreValid(const RawImageParameters& result) {
    const int format = static_cast<int>(result.format);
    const int pattern = static_cast<int>(result.bayerPattern);
    const int sampling = static_cast<int>(result.bayerSampling);
    const int matrix = static_cast<int>(result.yuvMatrix);
    const int range = static_cast<int>(result.range);
    return format >= static_cast<int>(RawPixelFormat::NV12) &&
           format <= static_cast<int>(RawPixelFormat::Raw16) &&
           pattern >= static_cast<int>(BayerPattern::RGGB) &&
           pattern <= static_cast<int>(BayerPattern::BGGR) &&
           sampling >= static_cast<int>(BayerSampling::Standard2x2) &&
           sampling <= static_cast<int>(BayerSampling::QuadBayer4x4) &&
           matrix >= static_cast<int>(YuvMatrix::BT601) &&
           matrix <= static_cast<int>(YuvMatrix::BT2020) &&
           range >= static_cast<int>(QuantizationRange::Full) &&
           range <= static_cast<int>(QuantizationRange::Limited) && !result.size.isEmpty() &&
           result.headerOffset >= 0 && result.rowStride >= 0 && result.chromaStride >= 0 &&
           result.frameIndex >= 0 && result.hasValidBitLayout() && result.blackLevel >= 0 &&
           result.blackLevel < result.maximumSampleValue() &&
           (result.whiteLevel == 0 || (result.whiteLevel > result.blackLevel &&
                                       result.whiteLevel <= result.maximumSampleValue())) &&
           result.hasValidDisplayTransform() && result.hasValidOrientation() &&
           result.hasValidBayerSampling() &&
           frameByteSize(result) > 0;
}

std::optional<RawImageParameters> fromMap(const QVariantMap& values) {
    if (values.isEmpty()) {
        return std::nullopt;
    }
    RawImageParameters result;
    result.size = {values.value(QStringLiteral("width")).toInt(),
                   values.value(QStringLiteral("height")).toInt()};
    result.format = static_cast<RawPixelFormat>(values.value(QStringLiteral("format")).toInt());
    result.headerOffset = values.value(QStringLiteral("offset")).toLongLong();
    result.rowStride = values.value(QStringLiteral("rowStride")).toLongLong();
    result.chromaStride = values.value(QStringLiteral("chromaStride")).toLongLong();
    result.frameIndex = values.value(QStringLiteral("frameIndex")).toInt();
    result.littleEndian = values.value(QStringLiteral("littleEndian"), true).toBool();
    result.msbAligned = values.value(QStringLiteral("msbAligned"), false).toBool();
    result.validBitsOverride = values.value(QStringLiteral("validBits"), 0).toInt();
    result.bayerPattern =
        static_cast<BayerPattern>(values.value(QStringLiteral("bayerPattern")).toInt());
    result.bayerSampling = static_cast<BayerSampling>(
        values.value(QStringLiteral("bayerSampling"), 0).toInt());
    result.yuvMatrix = static_cast<YuvMatrix>(values.value(QStringLiteral("matrix"), 1).toInt());
    result.range = static_cast<QuantizationRange>(values.value(QStringLiteral("range"), 1).toInt());
    result.orientation =
        static_cast<ImageOrientation>(values.value(QStringLiteral("orientation"), 0).toInt());
    result.blackLevel = values.value(QStringLiteral("blackLevel")).toInt();
    result.whiteLevel = values.value(QStringLiteral("whiteLevel")).toInt();
    result.demosaic = values.value(QStringLiteral("demosaic"), false).toBool();
    result.whiteBalanceGains = {
        values.value(QStringLiteral("wbRed"), 1.0).toDouble(),
        values.value(QStringLiteral("wbGreen"), 1.0).toDouble(),
        values.value(QStringLiteral("wbBlue"), 1.0).toDouble(),
    };
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const auto index = static_cast<std::size_t>(row * 3 + column);
            const double identity = row == column ? 1.0 : 0.0;
            result.colorCorrectionMatrix[index] =
                values.value(QStringLiteral("ccm%1%2").arg(row).arg(column), identity).toDouble();
        }
    }
    result.displayGamma = values.value(QStringLiteral("displayGamma"), 2.2).toDouble();
    if (!parametersAreValid(result)) {
        return std::nullopt;
    }
    return result;
}

QVariantMap namedPresetMap() { return QSettings().value(kNamedPresetsKey).toMap(); }

bool applyCapturedInteger(const QRegularExpressionMatch& match, const QString& captureName,
                          qint64 minimum, qint64 maximum,
                          const std::function<void(qint64)>& apply) {
    const QString captured = match.captured(captureName);
    if (captured.isEmpty()) {
        return true;
    }
    bool valid = false;
    const qint64 value = captured.toLongLong(&valid);
    if (!valid || value < minimum || value > maximum) {
        return false;
    }
    apply(value);
    return true;
}

} // namespace

std::optional<RawImageParameters> RawPresetStore::loadForFile(const QString& path) {
    QFile sidecar(sidecarPath(path));
    if (sidecar.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(sidecar.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            const QJsonObject root = document.object();
            if (root.value(QStringLiteral("version")).toInt() == 1) {
                if (const auto parameters = fromMap(root.toVariantMap())) {
                    return parameters;
                }
            }
        }
    }
    return fromMap(QSettings().value(keyForFile(path)).toMap());
}

void RawPresetStore::saveForFile(const QString& path, const RawImageParameters& parameters) {
    QSettings settings;
    settings.setValue(keyForFile(path), toMap(parameters));
    settings.sync();
}

bool RawPresetStore::saveSidecar(const QString& path, const RawImageParameters& parameters,
                                 QString* error) {
    QJsonObject root = QJsonObject::fromVariantMap(toMap(parameters));
    root.insert(QStringLiteral("version"), 1);
    QSaveFile file(sidecarPath(path));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

QString RawPresetStore::sidecarPath(const QString& path) {
    return path + QStringLiteral(".ispview.json");
}

RawImageParameters RawPresetStore::inferFromFileName(const QString& path) {
    const QString fileName = QFileInfo(path).fileName();
    for (const RawFilenameRule& rule : filenameRules()) {
        if (!rule.enabled) {
            continue;
        }
        const QRegularExpression expression(QRegularExpression::anchoredPattern(rule.pattern));
        const auto match = expression.match(fileName);
        const auto preset = loadNamedPreset(rule.presetName);
        if (!expression.isValid() || !match.hasMatch() || !preset) {
            continue;
        }
        RawImageParameters result = *preset;
        const bool capturesValid =
            applyCapturedInteger(
                match, QStringLiteral("width"), 1, std::numeric_limits<int>::max(),
                [&result](qint64 value) { result.size.setWidth(static_cast<int>(value)); }) &&
            applyCapturedInteger(
                match, QStringLiteral("height"), 1, std::numeric_limits<int>::max(),
                [&result](qint64 value) { result.size.setHeight(static_cast<int>(value)); }) &&
            applyCapturedInteger(match, QStringLiteral("row_stride"), 0,
                                 std::numeric_limits<qsizetype>::max(),
                                 [&result](qint64 value) { result.rowStride = value; }) &&
            applyCapturedInteger(match, QStringLiteral("chroma_stride"), 0,
                                 std::numeric_limits<qsizetype>::max(),
                                 [&result](qint64 value) { result.chromaStride = value; }) &&
            applyCapturedInteger(match, QStringLiteral("offset"), 0,
                                 std::numeric_limits<qsizetype>::max(),
                                 [&result](qint64 value) { result.headerOffset = value; }) &&
            applyCapturedInteger(
                match, QStringLiteral("frame"), 0, std::numeric_limits<int>::max(),
                [&result](qint64 value) { result.frameIndex = static_cast<int>(value); }) &&
            applyCapturedInteger(
                match, QStringLiteral("valid_bits"), 0, 16,
                [&result](qint64 value) { result.validBitsOverride = static_cast<int>(value); });
        if (capturesValid && parametersAreValid(result)) {
            return result;
        }
    }

    const QString name = QFileInfo(path).completeBaseName().toLower();
    RawImageParameters result;
    const QRegularExpression sizeExpression(QStringLiteral(R"((\d{2,6})[x_](\d{2,6}))"));
    auto matches = sizeExpression.globalMatch(name);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result.size = {match.captured(1).toInt(), match.captured(2).toInt()};
    }
    if (name.contains(QStringLiteral("nv21"))) {
        result.format = RawPixelFormat::NV21;
    } else if (name.contains(QStringLiteral("i420")) || name.contains(QStringLiteral("yuv420p"))) {
        result.format = RawPixelFormat::I420;
    } else if (name.contains(QStringLiteral("p010"))) {
        result.format = RawPixelFormat::P010;
        result.msbAligned = true;
    } else if (name.contains(QStringLiteral("raw14"))) {
        result.format = RawPixelFormat::Raw16;
        result.validBitsOverride = 14;
    } else if (name.contains(QStringLiteral("raw10"))) {
        result.format = RawPixelFormat::MipiRaw10;
    } else if (name.contains(QStringLiteral("raw12"))) {
        result.format = RawPixelFormat::MipiRaw12;
    } else if (name.contains(QStringLiteral("raw16"))) {
        result.format = RawPixelFormat::Raw16;
    }
    // Names such as "raw_8000_6000_10bits.raw" describe unpacked samples in a
    // 16-bit container, not MIPI RAW10 packing. Explicit raw10/raw12 markers above
    // keep their packed interpretation.
    if (!name.contains(QStringLiteral("raw10")) &&
        !name.contains(QStringLiteral("raw12"))) {
        for (int bits : {10, 12, 14, 16}) {
            if (name.contains(QStringLiteral("%1bits").arg(bits)) ||
                name.contains(QStringLiteral("%1bit").arg(bits))) {
                result.format = RawPixelFormat::Raw16;
                result.validBitsOverride = bits;
                break;
            }
        }
    }
    if (name.contains(QStringLiteral("bggr"))) {
        result.bayerPattern = BayerPattern::BGGR;
    } else if (name.contains(QStringLiteral("gbrg"))) {
        result.bayerPattern = BayerPattern::GBRG;
    } else if (name.contains(QStringLiteral("grbg"))) {
        result.bayerPattern = BayerPattern::GRBG;
    }
    if (name.contains(QStringLiteral("quadbayer")) ||
        name.contains(QStringLiteral("quad_bayer")) ||
        name.contains(QStringLiteral("4x4bin")) ||
        name.contains(QStringLiteral("4x4_bin"))) {
        result.bayerSampling = BayerSampling::QuadBayer4x4;
    }
    return result;
}

QStringList RawPresetStore::namedPresetNames() {
    QStringList names = namedPresetMap().keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

std::optional<RawImageParameters> RawPresetStore::loadNamedPreset(const QString& name) {
    const QString normalized = name.trimmed();
    if (normalized.isEmpty()) {
        return std::nullopt;
    }
    return fromMap(namedPresetMap().value(normalized).toMap());
}

bool RawPresetStore::saveNamedPreset(const QString& name, const RawImageParameters& parameters) {
    const QString normalized = name.trimmed();
    RawImageParameters reusable = parameters;
    reusable.frameIndex = 0;
    if (normalized.isEmpty() || normalized.size() > 128 || !parametersAreValid(reusable)) {
        return false;
    }
    QVariantMap presets = namedPresetMap();
    presets.insert(normalized, toMap(reusable));
    QSettings settings;
    settings.setValue(kNamedPresetsKey, presets);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool RawPresetStore::removeNamedPreset(const QString& name) {
    QVariantMap presets = namedPresetMap();
    if (presets.remove(name.trimmed()) == 0) {
        return false;
    }
    QSettings settings;
    settings.setValue(kNamedPresetsKey, presets);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QVector<RawFilenameRule> RawPresetStore::filenameRules() {
    QVector<RawFilenameRule> result;
    const QVariantList stored = QSettings().value(kFilenameRulesKey).toList();
    result.reserve(stored.size());
    for (const QVariant& value : stored) {
        const QVariantMap map = value.toMap();
        RawFilenameRule rule{map.value(QStringLiteral("name")).toString(),
                             map.value(QStringLiteral("pattern")).toString(),
                             map.value(QStringLiteral("preset")).toString(),
                             map.value(QStringLiteral("enabled"), true).toBool()};
        if (!rule.name.isEmpty() && !rule.pattern.isEmpty() && !rule.presetName.isEmpty()) {
            result.push_back(std::move(rule));
        }
    }
    return result;
}

bool RawPresetStore::saveFilenameRules(const QVector<RawFilenameRule>& rules) {
    const QStringList presetNames = namedPresetNames();
    const QSet<QString> presets(presetNames.cbegin(), presetNames.cend());
    QSet<QString> normalizedNames;
    QVariantList stored;
    stored.reserve(rules.size());
    for (const RawFilenameRule& source : rules) {
        RawFilenameRule rule{source.name.trimmed(), source.pattern.trimmed(),
                             source.presetName.trimmed(), source.enabled};
        const QString normalizedName = rule.name.toCaseFolded();
        const QRegularExpression expression(rule.pattern);
        if (rule.name.isEmpty() || rule.name.size() > 128 || rule.pattern.isEmpty() ||
            rule.pattern.size() > 1024 || !expression.isValid() ||
            !presets.contains(rule.presetName) || normalizedNames.contains(normalizedName)) {
            return false;
        }
        normalizedNames.insert(normalizedName);
        stored.push_back(QVariantMap{{QStringLiteral("name"), rule.name},
                                     {QStringLiteral("pattern"), rule.pattern},
                                     {QStringLiteral("preset"), rule.presetName},
                                     {QStringLiteral("enabled"), rule.enabled}});
    }
    QSettings settings;
    settings.setValue(kFilenameRulesKey, stored);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

} // namespace ispview
