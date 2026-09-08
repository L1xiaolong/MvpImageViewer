#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTranslator>
#include <QUrl>
#include <QVariantList>

#include <memory>

class QGuiApplication;
class QCryptographicHash;
class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

namespace ispview {

class AppSettings final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString effectiveLanguage READ effectiveLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool darkTheme READ darkTheme NOTIFY themeChanged)
    Q_PROPERTY(bool restoreLastDirectory READ restoreLastDirectory WRITE setRestoreLastDirectory
                   NOTIFY restoreLastDirectoryChanged)
    Q_PROPERTY(bool confirmTrash READ confirmTrash WRITE setConfirmTrash NOTIFY confirmTrashChanged)
    Q_PROPERTY(bool automaticUpdateChecks READ automaticUpdateChecks WRITE setAutomaticUpdateChecks
                   NOTIFY automaticUpdateChecksChanged)
    Q_PROPERTY(bool applyEmbeddedColorProfiles READ applyEmbeddedColorProfiles WRITE
                   setApplyEmbeddedColorProfiles NOTIFY colorDisplayChanged)
    Q_PROPERTY(bool preserveHighBitDepth READ preserveHighBitDepth WRITE setPreserveHighBitDepth
                   NOTIFY colorDisplayChanged)
    Q_PROPERTY(bool honorExifOrientation READ honorExifOrientation WRITE setHonorExifOrientation
                   NOTIFY colorDisplayChanged)
    Q_PROPERTY(QString canvasBackground READ canvasBackground WRITE setCanvasBackground NOTIFY
                   colorDisplayChanged)
    Q_PROPERTY(
        bool smoothDisplay READ smoothDisplay WRITE setSmoothDisplay NOTIFY smoothDisplayChanged)
    Q_PROPERTY(bool colorManagementAvailable READ colorManagementAvailable CONSTANT)
    Q_PROPERTY(QString updateState READ updateState NOTIFY updateStateChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateStateChanged)
    Q_PROPERTY(QUrl releaseUrl READ releaseUrl NOTIFY updateStateChanged)
    Q_PROPERTY(int updateDownloadProgress READ updateDownloadProgress NOTIFY updateStateChanged)
    Q_PROPERTY(QString updateError READ updateError NOTIFY updateStateChanged)
    Q_PROPERTY(QString downloadedUpdatePath READ downloadedUpdatePath NOTIFY updateStateChanged)
    Q_PROPERTY(QVariantList shortcutEntries READ shortcutEntries NOTIFY shortcutsChanged)
    Q_PROPERTY(int shortcutsRevision READ shortcutsRevision NOTIFY shortcutsChanged)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)

  public:
    explicit AppSettings(QGuiApplication* application, QObject* parent = nullptr);
    ~AppSettings() override;

    [[nodiscard]] QString language() const;
    [[nodiscard]] QString effectiveLanguage() const;
    [[nodiscard]] QString theme() const;
    [[nodiscard]] bool darkTheme() const;
    [[nodiscard]] bool restoreLastDirectory() const;
    [[nodiscard]] bool confirmTrash() const;
    [[nodiscard]] bool automaticUpdateChecks() const;
    [[nodiscard]] bool applyEmbeddedColorProfiles() const;
    [[nodiscard]] bool preserveHighBitDepth() const;
    [[nodiscard]] bool honorExifOrientation() const;
    [[nodiscard]] QString canvasBackground() const;
    [[nodiscard]] bool smoothDisplay() const;
    [[nodiscard]] bool colorManagementAvailable() const;
    [[nodiscard]] QString updateState() const;
    [[nodiscard]] QString latestVersion() const;
    [[nodiscard]] QUrl releaseUrl() const;
    [[nodiscard]] int updateDownloadProgress() const;
    [[nodiscard]] QString updateError() const;
    [[nodiscard]] QString downloadedUpdatePath() const;
    [[nodiscard]] QVariantList shortcutEntries() const;
    [[nodiscard]] int shortcutsRevision() const;
    [[nodiscard]] QString applicationVersion() const;

    void setLanguage(const QString& language);
    void setTheme(const QString& theme);
    void setRestoreLastDirectory(bool restore);
    void setConfirmTrash(bool confirm);
    void setAutomaticUpdateChecks(bool enabled);
    void setApplyEmbeddedColorProfiles(bool enabled);
    void setPreserveHighBitDepth(bool enabled);
    void setHonorExifOrientation(bool enabled);
    void setCanvasBackground(const QString& background);
    void setSmoothDisplay(bool enabled);

    Q_INVOKABLE QString shortcutFor(const QString& action) const;
    Q_INVOKABLE QString setShortcut(const QString& action, const QString& sequence);
    Q_INVOKABLE void resetShortcuts();
    Q_INVOKABLE void startAutomaticUpdateCheck();
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void cancelUpdateDownload();
    Q_INVOKABLE void installUpdate();
    Q_INVOKABLE void openReleasePage() const;
    Q_INVOKABLE void openUserGuide() const;
    Q_INVOKABLE void restoreDefaults();

  signals:
    void languageChanged();
    void themeChanged();
    void restoreLastDirectoryChanged();
    void confirmTrashChanged();
    void automaticUpdateChecksChanged();
    void updateStateChanged();
    void shortcutsChanged();
    void colorDisplayChanged();
    void smoothDisplayChanged();

  private:
    void applyLanguage();
    void setUpdateState(const QString& state, const QString& latestVersion = {},
                        const QUrl& releaseUrl = {});
    void beginInstallerDownload();
    void failUpdate(const QString& error);
    void clearDownloadedUpdate();
    [[nodiscard]] bool isAllowedUpdateUrl(const QUrl& url) const;
    [[nodiscard]] static QString normalizedShortcut(const QString& sequence);
    [[nodiscard]] static QString normalizedLanguage(const QString& language);
    [[nodiscard]] static QString normalizedTheme(const QString& theme);
    [[nodiscard]] static QString normalizedCanvasBackground(const QString& background);

    QGuiApplication* application_ = nullptr;
    QTranslator translator_;
    QString language_;
    QString theme_;
    bool restoreLastDirectory_ = true;
    bool confirmTrash_ = true;
    bool automaticUpdateChecks_ = true;
    bool applyEmbeddedColorProfiles_ = true;
    bool preserveHighBitDepth_ = true;
    bool honorExifOrientation_ = true;
    QString canvasBackground_ = QStringLiteral("neutral");
    bool smoothDisplay_ = true;
    QString updateState_ = QStringLiteral("idle");
    QString latestVersion_;
    QUrl releaseUrl_;
    QUrl installerUrl_;
    QString installerAssetName_;
    QString expectedInstallerSha256_;
    QString downloadedUpdatePath_;
    QString updateError_;
    qint64 installerAssetSize_ = 0;
    qint64 downloadedBytes_ = 0;
    int updateDownloadProgress_ = -1;
    bool cancelUpdateRequested_ = false;
    QHash<QString, QString> shortcuts_;
    int shortcutsRevision_ = 0;
    QNetworkAccessManager* networkManager_ = nullptr;
    QPointer<QNetworkReply> activeUpdateReply_;
    std::unique_ptr<QSaveFile> updateFile_;
    std::unique_ptr<QCryptographicHash> updateHash_;
};

} // namespace ispview
