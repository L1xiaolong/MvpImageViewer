#pragma once

#include <QString>

namespace mvpview {

class SingleFileRename final {
  public:
    // Renames one file within its current directory. A sibling .mvpview.json sidecar is moved
    // transactionally with the image so RAW/YUV interpretation does not become detached.
    [[nodiscard]] static bool execute(const QString& sourcePath, const QString& destinationPath,
                                      QString* error = nullptr);
};

} // namespace mvpview
