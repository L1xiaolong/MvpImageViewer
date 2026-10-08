#pragma once

#include <QImage>
#include <QMutex>
#include <QString>
#include <QSet>
#include <QThreadPool>

#include <atomic>

namespace mvpview {

class ThumbnailDiskCache final {
  public:
    explicit ThumbnailDiskCache(QString rootDirectory = {});

    [[nodiscard]] QImage load(const QString& key) const;
    [[nodiscard]] bool store(const QString& key, const QImage& image,
                             const QSize& sourceSize = {}, int validBits = 0) const;
    [[nodiscard]] QString rootDirectory() const { return rootDirectory_; }
    [[nodiscard]] QString pathForKey(const QString& key) const;

  private:
    void trimIfNeeded() const;

    QString rootDirectory_;
    mutable std::atomic_uint storeCount_{0};
    mutable QMutex writeKeysMutex_;
    mutable QSet<QString> writingKeys_;
    mutable std::atomic<qint64> lastTrimMs_{0};
    mutable QMutex trimMutex_;
    mutable QThreadPool maintenancePool_;
};

} // namespace mvpview
