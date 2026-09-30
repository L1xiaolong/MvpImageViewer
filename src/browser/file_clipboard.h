#pragma once

#include <QStringList>

namespace mvpview {

struct FileClipboardContents {
    QStringList paths;
    bool cut = false;
};

// Presentation-neutral bridge from file commands to the system clipboard. URLs remain interoperable with
// Finder/Explorer; the private cut marker is intentionally advisory and only controls whether this
// application copies or moves on paste.
class FileClipboard final {
  public:
    static void setPaths(const QStringList& paths, bool cut);
    [[nodiscard]] static FileClipboardContents contents();
    [[nodiscard]] static bool hasFiles();
    static void clear();

  private:
    static constexpr auto CutMimeType = "application/x-mvp-image-viewer-cut";
};

} // namespace mvpview
