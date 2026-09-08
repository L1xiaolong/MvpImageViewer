#pragma once

#include "core/image_types.h"
#include "io/image_loader.h"

#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

namespace ispview {


// Presentation data for the QML properties inspector. Decoding and histogram analysis stay in
// C++; QML receives only immutable field rows and plot-ready channel data.
class ImagePropertiesController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(bool directory READ directory NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(QVariantList basicFields READ basicFields NOTIFY stateChanged)
    Q_PROPERTY(QVariantList exifFields READ exifFields NOTIFY stateChanged)
    Q_PROPERTY(QVariantList rawFields READ rawFields NOTIFY stateChanged)
    Q_PROPERTY(bool hasRawParameters READ hasRawParameters NOTIFY stateChanged)
    Q_PROPERTY(bool hasHistogramSource READ hasHistogramSource NOTIFY stateChanged)
    Q_PROPERTY(int histogramRevision READ histogramRevision NOTIFY histogramRevisionChanged)

  public:
    explicit ImagePropertiesController(ImageLoader* loader, QObject* parent = nullptr);

    [[nodiscard]] QString path() const { return path_; }
    [[nodiscard]] QString fileName() const { return fileName_; }
    [[nodiscard]] bool directory() const { return directory_; }
    [[nodiscard]] bool loading() const { return loading_; }
    [[nodiscard]] QString errorText() const { return errorText_; }
    [[nodiscard]] QVariantList basicFields() const { return basicFields_; }
    [[nodiscard]] QVariantList exifFields() const { return exifFields_; }
    [[nodiscard]] QVariantList rawFields() const { return rawFields_; }
    [[nodiscard]] bool hasRawParameters() const { return !rawFields_.isEmpty(); }
    [[nodiscard]] bool hasHistogramSource() const;
    [[nodiscard]] int histogramRevision() const { return histogramRevision_; }

    Q_INVOKABLE void loadPath(const QString& path);
    // source 0 is the rendered display; source 1 is the original RAW/YUV planes.
    Q_INVOKABLE void requestHistogram(int source);
    Q_INVOKABLE QVariantMap histogram(int source) const;

  signals:
    void stateChanged();
    void histogramRevisionChanged();
    void histogramChanged(int source);

  private:
    void setFrame(ImageFramePtr frame);
    void setHistogram(int source, quint64 generation, QVariantMap value);
    void resetHistograms();

    ImageLoader* loader_ = nullptr;
    QString path_;
    QString fileName_;
    bool directory_ = false;
    bool loading_ = false;
    QString errorText_;
    QVariantList basicFields_;
    QVariantList exifFields_;
    QVariantList rawFields_;
    ImageFramePtr frame_;
    quint64 loadGeneration_ = 0;
    quint64 displayHistogramGeneration_ = 0;
    quint64 sourceHistogramGeneration_ = 0;
    int histogramRevision_ = 0;
    QVariantMap displayHistogram_;
    QVariantMap sourceHistogram_;
    QSet<int> pendingHistogramSources_;
    LoadHandle loadHandle_;
};

} // namespace ispview
