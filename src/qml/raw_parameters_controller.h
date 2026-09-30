#pragma once

#include "core/raw_image_parameters.h"

#include <QObject>
#include <QStringList>
#include <QVariantMap>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace mvpview {

class ImageLoader;

// QML-facing RAW/YUV editor state. It owns preset persistence and parameter validation, while
// the dialog only renders controls and forwards edited values.
class RawParametersController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY stateChanged)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY stateChanged)
    Q_PROPERTY(QString selectedPreset READ selectedPreset NOTIFY stateChanged)
    Q_PROPERTY(bool yuvFormat READ yuvFormat NOTIFY stateChanged)
    Q_PROPERTY(bool raw16Format READ raw16Format NOTIFY stateChanged)
    Q_PROPERTY(bool endianControlsVisible READ endianControlsVisible NOTIFY stateChanged)
    Q_PROPERTY(QString suggestedPresetName READ suggestedPresetName NOTIFY stateChanged)

  public:
    explicit RawParametersController(ImageLoader* loader, QObject* parent = nullptr);

    [[nodiscard]] QString path() const { return path_; }
    [[nodiscard]] QString fileName() const;
    [[nodiscard]] QVariantMap values() const { return values_; }
    [[nodiscard]] QStringList presetNames() const { return presetNames_; }
    [[nodiscard]] QString selectedPreset() const { return selectedPreset_; }
    [[nodiscard]] bool yuvFormat() const;
    [[nodiscard]] bool raw16Format() const;
    [[nodiscard]] bool endianControlsVisible() const;
    [[nodiscard]] QString suggestedPresetName() const;

    Q_INVOKABLE void loadPath(const QString& path);
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);
    Q_INVOKABLE void setListValue(const QString& key, int index, const QVariant& value);
    Q_INVOKABLE void selectPreset(const QString& name);
    Q_INVOKABLE QString savePreset(const QString& name, bool applyToFolderAfter = false);
    Q_INVOKABLE QString deleteSelectedPreset();
    Q_INVOKABLE QString applyToFolder();

  signals:
    void stateChanged();
    void parametersApplied(const QString& path);
    void notificationRequested(const QString& message, bool error);

  private:
    [[nodiscard]] RawImageParameters parameters() const;
    void setParameters(const RawImageParameters& parameters, const QString& presetName = {});
    void refreshPresets();
    void applyEditedParameters();

    ImageLoader* loader_ = nullptr;
    QTimer* changeTimer_ = nullptr;
    QString path_;
    RawImageParameters baseParameters_;
    QVariantMap values_;
    QStringList presetNames_;
    QString selectedPreset_;
};

} // namespace mvpview
