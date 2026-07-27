#pragma once

#include "core/image_types.h"
#include "io/image_loader.h"
#include <QObject>
#include <QStringList>
#include <QVector>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

namespace ispview {

class QmlImageCanvas;

// Presentation-independent comparison session. It owns exactly the same
// asynchronous preview/full-frame lifecycle used by CompareWindow.
class CompareController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList paths READ paths NOTIFY pathsChanged)
    Q_PROPERTY(int presentationMode READ presentationMode WRITE setPresentationMode NOTIFY presentationModeChanged)
    Q_PROPERTY(qreal splitAmount READ splitAmount WRITE setSplitAmount NOTIFY splitAmountChanged)
    Q_PROPERTY(bool synchronized READ synchronized WRITE setSynchronized NOTIFY synchronizedChanged)
    Q_PROPERTY(bool holdCandidate READ holdCandidate WRITE setHoldCandidate NOTIFY holdCandidateChanged)
    Q_PROPERTY(bool fileInformationVisible READ fileInformationVisible WRITE setFileInformationVisible
                   NOTIFY fileInformationVisibleChanged)
    Q_PROPERTY(bool exifVisible READ exifVisible WRITE setExifVisible NOTIFY exifVisibleChanged)
    Q_PROPERTY(bool histogramVisible READ histogramVisible WRITE setHistogramVisible
                   NOTIFY histogramVisibleChanged)
    Q_PROPERTY(bool pixelValueVisible READ pixelValueVisible WRITE setPixelValueVisible
                   NOTIFY pixelValueVisibleChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(int histogramRevision READ histogramRevision NOTIFY histogramRevisionChanged)

  public:
    explicit CompareController(ImageLoader* loader, QObject* parent = nullptr);
    QStringList paths() const { return paths_; }
    int presentationMode() const { return presentationMode_; }
    qreal splitAmount() const { return splitAmount_; }
    bool synchronized() const { return synchronized_; }
    bool holdCandidate() const { return holdCandidate_; }
    bool fileInformationVisible() const { return fileInformationVisible_; }
    bool exifVisible() const { return exifVisible_; }
    bool histogramVisible() const { return histogramVisible_; }
    bool pixelValueVisible() const { return pixelValueVisible_; }
    ImageFramePtr frame(int slot) const;
    int revision() const { return revision_; }
    int histogramRevision() const { return histogramRevision_; }
    Q_INVOKABLE QString fileText(int slot) const;
    Q_INVOKABLE QString cameraText(int slot) const;

    Q_INVOKABLE void setPaths(const QStringList& paths);
    Q_INVOKABLE void closeSession();
    Q_INVOKABLE void setPresentationMode(int mode);
    Q_INVOKABLE void setSplitAmount(qreal amount);
    Q_INVOKABLE void setSynchronized(bool enabled);
    Q_INVOKABLE void setHoldCandidate(bool active);
    Q_INVOKABLE void setFileInformationVisible(bool visible);
    Q_INVOKABLE void setExifVisible(bool visible);
    Q_INVOKABLE void setHistogramVisible(bool visible);
    Q_INVOKABLE void setPixelValueVisible(bool visible);
    Q_INVOKABLE void attachCanvas(QObject* canvas);
    Q_INVOKABLE void fitAll();
    Q_INVOKABLE void actualPixelsAll();
    Q_INVOKABLE QVariantList pixelTexts(int sourceSlot, int x, int y) const;
    Q_INVOKABLE void requestHistogram(int slot);
    Q_INVOKABLE QVariantMap histogram(int slot) const;

  signals:
    void pathsChanged();
    void presentationModeChanged();
    void splitAmountChanged();
    void synchronizedChanged();
    void holdCandidateChanged();
    void fileInformationVisibleChanged();
    void exifVisibleChanged();
    void histogramVisibleChanged();
    void pixelValueVisibleChanged();
    void frameChanged(int slot, bool fullResolution);
    void loadFailed(int slot, const QString& error);
    void revisionChanged();
    void histogramRevisionChanged();
    void histogramChanged(int slot);

  private:
    void requestFrame(int slot, const QString& path);
    void requestFullFrame(int slot, LoadCategory category);
    void requestAutomaticFullFrames();
    void ensureFullFrames();
    void applyHoldFrame();
    void refreshCanvas(int changedSlot = -1, bool resetChangedView = false);
    void clearHistograms(int slot);
    void completeHistogram(int slot, quint64 generation, QVariantMap value);
    ImageLoader* loader_;
    QStringList paths_;
    QVector<ImageFramePtr> frames_;
    QStringList errors_;
    QVector<quint64> generations_;
    QPointer<QmlImageCanvas> canvas_;
    int presentationMode_ = 0;
    qreal splitAmount_ = 0.5;
    bool synchronized_ = true;
    bool holdCandidate_ = false;
    bool fileInformationVisible_ = true;
    bool exifVisible_ = false;
    bool histogramVisible_ = false;
    bool pixelValueVisible_ = false;
    int revision_ = 0;
    int histogramRevision_ = 0;
    QVector<quint64> histogramGenerations_;
    QVector<bool> histogramRequested_;
    QVector<QVariantMap> displayHistograms_;
    QVector<LoadHandle> previewHandles_;
    QVector<LoadHandle> fullHandles_;
    QVector<bool> fullRequested_;
    QVector<bool> fullResolution_;
    QTimer automaticFullLoadTimer_;
};

} // namespace ispview
