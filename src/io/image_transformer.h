#pragma once

#include "core/raw_image_parameters.h"

#include <QSize>
#include <QString>

#include <optional>

namespace ispview {

enum class QuarterTurn { Clockwise, CounterClockwise };

// Destructive image editing with a single, recoverable "original" snapshot. All writes use
// QSaveFile so a failed encode or interrupted write never leaves a partially written image.
class ImageTransformer final {
  public:
    [[nodiscard]] static QString rotate(const QString& path, QuarterTurn direction,
                                        const std::optional<RawImageParameters>& raw = {});
    [[nodiscard]] static QString resize(const QString& path, const QSize& size,
                                        const std::optional<RawImageParameters>& raw = {});
    [[nodiscard]] static QString restore(const QString& path);
    [[nodiscard]] static bool canRestore(const QString& path);
    [[nodiscard]] static QString backupPath(const QString& path);
    [[nodiscard]] static QString backupManifestPath(const QString& path);

  private:
    static QString ensureBackup(const QString& path);
};

} // namespace ispview
