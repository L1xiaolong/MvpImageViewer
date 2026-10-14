#pragma once

#include "core/image_types.h"
#include "io/image_loader.h"

#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>

namespace mvpview {


// Presentation data for the QML properties inspector. Decoding and histogram analysis stay in
// C++; QML receives only immutable field rows and plot-ready channel data.
class ImagePropertiesController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(bool directory READ directory NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(bool metadataLoading READ metadataLoading NOTIFY stateChanged)
    Q_PROPERTY(QVariantList overviewGroups READ overviewGroups NOTIFY stateChanged)
    Q_PROPERTY(QVariantList advancedGroups READ advancedGroups NOTIFY stateChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY stateChanged)
    Q_PROPERTY(bool hasHistogramSource READ hasHistogramSource NOTIFY stateChanged)
    Q_PROPERTY(int histogramRevision READ histogramRevision NOTIFY histogramRevisionChanged)

  public:
    explicit ImagePropertiesController(ImageLoader* loader, QObject* parent = nullptr);
    ~ImagePropertiesController() override;

    [[nodiscard]] QString path() const { return path_; }
    [[nodiscard]] QString fileName() const { return fileName_; }
    [[nodiscard]] bool directory() const { return directory_; }
    [[nodiscard]] bool loading() const { return loading_; }
    [[nodiscard]] QString errorText() const { return errorText_; }
    [[nodiscard]] bool metadataLoading() const { return metadataLoading_; }
    [[nodiscard]] QVariantList overviewGroups() const { return overviewGroups_; }
    [[nodiscard]] QVariantList advancedGroups() const { return advancedGroups_; }
    [[nodiscard]] QStringList warnings() const { return warnings_; }
    [[nodiscard]] bool hasHistogramSource() const;
    [[nodiscard]] int histogramRevision() const { return histogramRevision_; }

    Q_INVOKABLE void loadPath(const QString& path, bool deferred = false);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void retranslate();
    // The inspector always requests source 1: native file channels (RGB/Gray/Bayer/YUV).
    // Source 0 remains available for existing display-analysis consumers.
    Q_INVOKABLE void requestHistogram(int source);
    Q_INVOKABLE QVariantMap histogram(int source) const;

  signals:
    void stateChanged();
    void histogramRevisionChanged();
    void histogramChanged(int source);

  private:
    void setFrame(ImageFramePtr frame);
    void startLoading();
    void rebuildFields();
    void setHistogram(int source, quint64 generation, QVariantMap value);
    void resetHistograms();

    ImageLoader* loader_ = nullptr;
    QString path_;
    QString fileName_;
    bool directory_ = false;
    bool loading_ = false;
    QString errorText_;
    bool metadataLoading_ = false;
    QVariantList overviewGroups_;
    QVariantList advancedGroups_;
    QStringList warnings_;
    QString metadataError_;
    QTimer loadTimer_;
    ImageFramePtr frame_;
    quint64 loadGeneration_ = 0;
    quint64 displayHistogramGeneration_ = 0;
    quint64 sourceHistogramGeneration_ = 0;
    int histogramRevision_ = 0;
    QVariantMap displayHistogram_;
    QVariantMap sourceHistogram_;
    QSet<int> pendingHistogramSources_;
    LoadHandle loadHandle_;
    LoadHandle metadataHandle_;
    LoadHandle histogramHandles_[2];
};

} // namespace mvpview
