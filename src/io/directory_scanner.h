#pragma once

#include <QDateTime>
#include <QCollator>
#include <optional>
#include <QFileInfo>
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QSemaphore>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>

namespace mvpview {

struct ImageFileRecord {
    QString path;
    QString fileName;
    qint64 fileSize = 0;
    QDateTime modifiedAt;
    bool isDirectory = false;
    QString fileType;
    std::optional<QCollatorSortKey> nameSortKey{};
    std::optional<QCollatorSortKey> typeSortKey{};
};

class DirectoryScanner final : public QObject {
    Q_OBJECT

  public:
    explicit DirectoryScanner(QObject* parent = nullptr);
    ~DirectoryScanner() override;

    quint64 scanAsync(const QString& directory);
    quint64 scanImageFoldersAsync(const QString& directory);
    void cancel();
    void setBatchBackpressure(bool enabled) { batchBackpressure_ = enabled; }
    void acknowledgeBatch(quint64 generation);
    [[nodiscard]] static QVector<ImageFileRecord> scan(const QString& directory);
    [[nodiscard]] static QVector<ImageFileRecord>
    scanImageFoldersRecursively(const QString& directory,
                               const std::shared_ptr<std::atomic_bool>& cancelled = {});
    [[nodiscard]] static bool isSupportedImageFile(const QString& path);
    [[nodiscard]] static bool isBrowsableEntry(const QFileInfo& info);

  signals:
    void scanStarted(const QString& directory, quint64 generation);
    void scanBatchReady(const QString& directory,
                        const QVector<mvpview::ImageFileRecord>& files,
                        quint64 generation);
    void scanFinished(const QString& directory, const QVector<mvpview::ImageFileRecord>& files,
                      quint64 generation);

  private:
    using CancelFlag = std::shared_ptr<std::atomic_bool>;
    [[nodiscard]] static QVector<ImageFileRecord>
    scanBatched(const QString& directory, const CancelFlag& cancelled,
                const std::function<void(QVector<ImageFileRecord>)>& publishBatch);

    quint64 generation_ = 0;
    CancelFlag currentCancel_;
    QThreadPool pool_;
    bool batchBackpressure_ = false;
    std::shared_ptr<QSemaphore> batchCredits_;
};

} // namespace mvpview
