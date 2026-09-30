#pragma once

#include "core/raw_image_parameters.h"

#include <QStringList>
#include <QVector>

#include <optional>

namespace mvpview {

struct RawFilenameRule {
    QString name;
    QString pattern;
    QString presetName;
    bool enabled = true;
};

class RawPresetStore final {
  public:
    [[nodiscard]] static std::optional<RawImageParameters> loadForFile(const QString& path);
    static void saveForFile(const QString& path, const RawImageParameters& parameters);
    [[nodiscard]] static bool saveSidecar(const QString& path, const RawImageParameters& parameters,
                                          QString* error = nullptr);
    [[nodiscard]] static QString sidecarPath(const QString& path);
    [[nodiscard]] static RawImageParameters inferFromFileName(const QString& path);
    [[nodiscard]] static QStringList namedPresetNames();
    [[nodiscard]] static std::optional<RawImageParameters>
    loadNamedPreset(const QString& name);
    [[nodiscard]] static bool saveNamedPreset(const QString& name,
                                              const RawImageParameters& parameters);
    [[nodiscard]] static bool removeNamedPreset(const QString& name);
    [[nodiscard]] static QVector<RawFilenameRule> filenameRules();
    [[nodiscard]] static bool saveFilenameRules(const QVector<RawFilenameRule>& rules);
};

} // namespace mvpview
