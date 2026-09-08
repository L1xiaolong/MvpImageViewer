#include "qml/app_settings.h"
#include "io/encoded_color_management.h"
#include "io/qt_image_decoder.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>
#include <QVersionNumber>

#ifndef ISPVIEW_GITHUB_REPOSITORY
#define ISPVIEW_GITHUB_REPOSITORY ""
#endif

namespace ispview {
namespace {
constexpr auto kLanguageKey = "general/language";
constexpr auto kThemeKey = "appearance/theme";
constexpr auto kRestoreLastDirectoryKey = "general/restoreLastDirectory";
constexpr auto kConfirmTrashKey = "general/confirmTrash";
constexpr auto kAutomaticUpdateChecksKey = "updates/automaticChecks";
constexpr auto kApplyEmbeddedColorProfilesKey = "color/applyEmbeddedProfiles";
constexpr auto kPreserveHighBitDepthKey = "color/preserveHighBitDepth";
constexpr auto kHonorExifOrientationKey = "display/honorExifOrientation";
constexpr auto kCanvasBackgroundKey = "display/canvasBackground";
constexpr auto kSmoothDisplayKey = "display/smoothDisplay";
constexpr auto kLastUpdateCheckKey = "updates/lastCheckUtc";

QString platformInstallerSuffix() {
#if defined(Q_OS_MACOS)
    return QStringLiteral("-macos-arm64.dmg");
#elif defined(Q_OS_WIN)
    return QStringLiteral("-windows-x64-setup.exe");
#else
    return {};
#endif
}

QString repositorySlug() { return QString::fromUtf8(ISPVIEW_GITHUB_REPOSITORY).trimmed(); }

QUrl repositoryUrl(const QString& suffix = {}) {
    const QString slug = repositorySlug();
    if (slug.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://github.com/%1%2").arg(slug, suffix));
}

QUrl latestReleaseApiUrl() {
    const QString slug = repositorySlug();
    if (slug.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(slug));
}

struct ShortcutDefinition {
    const char* id;
    const char* defaultSequence;
};

constexpr ShortcutDefinition kShortcutDefinitions[] = {
    {"openFolder", "Ctrl+O"},      {"find", "Ctrl+F"}, {"settings", "Ctrl+,"},
    {"toggleNavigator", "Ctrl+B"}, {"compare", "C"},   {"rename", "F2"},
    {"newFolder", "Ctrl+Shift+N"},
};

const ShortcutDefinition* shortcutDefinition(const QString& action) {
    for (const auto& definition : kShortcutDefinitions) {
        if (action == QLatin1String(definition.id))
            return &definition;
    }
    return nullptr;
}
} // namespace

AppSettings::AppSettings(QGuiApplication* application, QObject* parent)
    : QObject(parent), application_(application) {
    const QSettings settings;
    language_ = normalizedLanguage(
        settings.value(QLatin1String(kLanguageKey), QStringLiteral("system")).toString());
    theme_ = normalizedTheme(
        settings.value(QLatin1String(kThemeKey), QStringLiteral("system")).toString());
    if (qEnvironmentVariableIsSet("ISPVIEW_LANGUAGE_OVERRIDE")) {
        language_ = normalizedLanguage(qEnvironmentVariable("ISPVIEW_LANGUAGE_OVERRIDE"));
    }
    if (qEnvironmentVariableIsSet("ISPVIEW_THEME_OVERRIDE")) {
        theme_ = normalizedTheme(qEnvironmentVariable("ISPVIEW_THEME_OVERRIDE"));
    }
    restoreLastDirectory_ = settings.value(QLatin1String(kRestoreLastDirectoryKey), true).toBool();
    confirmTrash_ = settings.value(QLatin1String(kConfirmTrashKey), true).toBool();
    automaticUpdateChecks_ =
        settings.value(QLatin1String(kAutomaticUpdateChecksKey), true).toBool();
    applyEmbeddedColorProfiles_ =
        settings.value(QLatin1String(kApplyEmbeddedColorProfilesKey), true).toBool();
    preserveHighBitDepth_ = settings.value(QLatin1String(kPreserveHighBitDepthKey), true).toBool();
    honorExifOrientation_ = settings.value(QLatin1String(kHonorExifOrientationKey), true).toBool();
    canvasBackground_ = normalizedCanvasBackground(
        settings.value(QLatin1String(kCanvasBackgroundKey), QStringLiteral("neutral")).toString());
    smoothDisplay_ = settings.value(QLatin1String(kSmoothDisplayKey), true).toBool();
    EncodedColorManagement::setEnabled(applyEmbeddedColorProfiles_);
    QtImageDecoder::setPreserveHighBitDepth(preserveHighBitDepth_);
    QtImageDecoder::setAutoOrientationEnabled(honorExifOrientation_);
    for (const auto& definition : kShortcutDefinitions) {
        const QString action = QLatin1String(definition.id);
        const QString stored = settings
                                   .value(QStringLiteral("shortcuts/") + action,
                                          QLatin1String(definition.defaultSequence))
                                   .toString();
        const QString normalized = normalizedShortcut(stored);
        shortcuts_.insert(action, normalized.isEmpty() ? QLatin1String(definition.defaultSequence)
                                                       : normalized);
    }

    applyLanguage();
    if (application_ && application_->styleHints()) {
        connect(application_->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
            if (theme_ == QStringLiteral("system"))
                emit themeChanged();
        });
    }
}

AppSettings::~AppSettings() = default;

QString AppSettings::language() const { return language_; }

QString AppSettings::effectiveLanguage() const {
    if (language_ != QStringLiteral("system"))
        return language_;
    return QLocale::system().language() == QLocale::Chinese ? QStringLiteral("zh_CN")
                                                            : QStringLiteral("en");
}

QString AppSettings::theme() const { return theme_; }

bool AppSettings::darkTheme() const {
    if (theme_ == QStringLiteral("dark"))
        return true;
    if (theme_ == QStringLiteral("light") || !application_ || !application_->styleHints())
        return false;
    return application_->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

bool AppSettings::restoreLastDirectory() const { return restoreLastDirectory_; }

bool AppSettings::confirmTrash() const { return confirmTrash_; }

bool AppSettings::automaticUpdateChecks() const { return automaticUpdateChecks_; }
bool AppSettings::applyEmbeddedColorProfiles() const { return applyEmbeddedColorProfiles_; }
bool AppSettings::preserveHighBitDepth() const { return preserveHighBitDepth_; }
bool AppSettings::honorExifOrientation() const { return honorExifOrientation_; }
QString AppSettings::canvasBackground() const { return canvasBackground_; }
bool AppSettings::smoothDisplay() const { return smoothDisplay_; }
bool AppSettings::colorManagementAvailable() const { return EncodedColorManagement::isAvailable(); }

QString AppSettings::updateState() const { return updateState_; }

QString AppSettings::latestVersion() const { return latestVersion_; }

QUrl AppSettings::releaseUrl() const { return releaseUrl_; }

int AppSettings::updateDownloadProgress() const { return updateDownloadProgress_; }

QString AppSettings::updateError() const { return updateError_; }

QString AppSettings::downloadedUpdatePath() const { return downloadedUpdatePath_; }

QVariantList AppSettings::shortcutEntries() const {
    QVariantList entries;
    for (const auto& definition : kShortcutDefinitions) {
        const QString action = QLatin1String(definition.id);
        const QString sequence = shortcutFor(action);
        entries.push_back(QVariantMap{
            {QStringLiteral("id"), action},
            {QStringLiteral("sequence"), sequence},
            {QStringLiteral("nativeSequence"),
             QKeySequence::fromString(sequence, QKeySequence::PortableText)
                 .toString(QKeySequence::NativeText)},
        });
    }
    return entries;
}

int AppSettings::shortcutsRevision() const { return shortcutsRevision_; }

QString AppSettings::applicationVersion() const { return QCoreApplication::applicationVersion(); }

void AppSettings::setLanguage(const QString& language) {
    const QString normalized = normalizedLanguage(language);
    if (language_ == normalized)
        return;
    language_ = normalized;
    QSettings().setValue(QLatin1String(kLanguageKey), language_);
    applyLanguage();
    emit languageChanged();
}

void AppSettings::setTheme(const QString& theme) {
    const QString normalized = normalizedTheme(theme);
    if (theme_ == normalized)
        return;
    theme_ = normalized;
    QSettings().setValue(QLatin1String(kThemeKey), theme_);
    emit themeChanged();
}

void AppSettings::setRestoreLastDirectory(bool restore) {
    if (restoreLastDirectory_ == restore)
        return;
    restoreLastDirectory_ = restore;
    QSettings().setValue(QLatin1String(kRestoreLastDirectoryKey), restore);
    emit restoreLastDirectoryChanged();
}

void AppSettings::setConfirmTrash(bool confirm) {
    if (confirmTrash_ == confirm)
        return;
    confirmTrash_ = confirm;
    QSettings().setValue(QLatin1String(kConfirmTrashKey), confirm);
    emit confirmTrashChanged();
}

void AppSettings::setAutomaticUpdateChecks(bool enabled) {
    if (automaticUpdateChecks_ == enabled)
        return;
    automaticUpdateChecks_ = enabled;
    QSettings().setValue(QLatin1String(kAutomaticUpdateChecksKey), enabled);
    emit automaticUpdateChecksChanged();
}

void AppSettings::setApplyEmbeddedColorProfiles(bool enabled) {
    if (applyEmbeddedColorProfiles_ == enabled)
        return;
    applyEmbeddedColorProfiles_ = enabled;
    EncodedColorManagement::setEnabled(enabled);
    QSettings().setValue(QLatin1String(kApplyEmbeddedColorProfilesKey), enabled);
    emit colorDisplayChanged();
}

void AppSettings::setPreserveHighBitDepth(bool enabled) {
    if (preserveHighBitDepth_ == enabled)
        return;
    preserveHighBitDepth_ = enabled;
    QtImageDecoder::setPreserveHighBitDepth(enabled);
    QSettings().setValue(QLatin1String(kPreserveHighBitDepthKey), enabled);
    emit colorDisplayChanged();
}

void AppSettings::setHonorExifOrientation(bool enabled) {
    if (honorExifOrientation_ == enabled)
        return;
    honorExifOrientation_ = enabled;
    QtImageDecoder::setAutoOrientationEnabled(enabled);
    QSettings().setValue(QLatin1String(kHonorExifOrientationKey), enabled);
    emit colorDisplayChanged();
}

void AppSettings::setCanvasBackground(const QString& background) {
    const QString normalized = normalizedCanvasBackground(background);
    if (canvasBackground_ == normalized)
        return;
    canvasBackground_ = normalized;
    QSettings().setValue(QLatin1String(kCanvasBackgroundKey), normalized);
    emit colorDisplayChanged();
}

void AppSettings::setSmoothDisplay(bool enabled) {
    if (smoothDisplay_ == enabled)
        return;
    smoothDisplay_ = enabled;
    QSettings().setValue(QLatin1String(kSmoothDisplayKey), enabled);
    emit smoothDisplayChanged();
}

QString AppSettings::shortcutFor(const QString& action) const {
    if (const auto it = shortcuts_.constFind(action); it != shortcuts_.cend())
        return it.value();
    if (const auto* definition = shortcutDefinition(action))
        return QLatin1String(definition->defaultSequence);
    return {};
}

QString AppSettings::setShortcut(const QString& action, const QString& sequence) {
    if (!shortcutDefinition(action))
        return QStringLiteral("unknown");
    const QString normalized = normalizedShortcut(sequence);
    if (normalized.isEmpty())
        return QStringLiteral("invalid");
    for (auto it = shortcuts_.cbegin(); it != shortcuts_.cend(); ++it) {
        if (it.key() != action && it.value().compare(normalized, Qt::CaseInsensitive) == 0)
            return it.key();
    }
    if (shortcutFor(action) == normalized)
        return {};
    shortcuts_.insert(action, normalized);
    QSettings().setValue(QStringLiteral("shortcuts/") + action, normalized);
    ++shortcutsRevision_;
    emit shortcutsChanged();
    return {};
}

void AppSettings::resetShortcuts() {
    bool changed = false;
    QSettings settings;
    for (const auto& definition : kShortcutDefinitions) {
        const QString action = QLatin1String(definition.id);
        const QString defaultSequence = QLatin1String(definition.defaultSequence);
        changed = changed || shortcuts_.value(action) != defaultSequence;
        shortcuts_.insert(action, defaultSequence);
        settings.remove(QStringLiteral("shortcuts/") + action);
    }
    if (changed) {
        ++shortcutsRevision_;
        emit shortcutsChanged();
    }
}

void AppSettings::startAutomaticUpdateCheck() {
    if (!automaticUpdateChecks_)
        return;
#ifndef NDEBUG
    if (qEnvironmentVariableIsSet("ISPVIEW_UPDATE_API_URL")) {
        QTimer::singleShot(100, this, &AppSettings::checkForUpdates);
        return;
    }
#endif
    if (repositorySlug().isEmpty())
        return;
    const QDateTime lastCheck = QSettings().value(QLatin1String(kLastUpdateCheckKey)).toDateTime();
    if (lastCheck.isValid() && lastCheck.secsTo(QDateTime::currentDateTimeUtc()) < 24 * 60 * 60)
        return;
    QTimer::singleShot(1500, this, &AppSettings::checkForUpdates);
}

void AppSettings::checkForUpdates() {
    if (updateState_ == QStringLiteral("checking") ||
        updateState_ == QStringLiteral("downloading") ||
        updateState_ == QStringLiteral("verifying") ||
        updateState_ == QStringLiteral("installing"))
        return;
    if (!networkManager_)
        networkManager_ = new QNetworkAccessManager(this);

    cancelUpdateRequested_ = false;
    installerUrl_.clear();
    installerAssetName_.clear();
    expectedInstallerSha256_.clear();
    installerAssetSize_ = 0;
    updateError_.clear();
    updateDownloadProgress_ = -1;
    clearDownloadedUpdate();
    setUpdateState(QStringLiteral("checking"));
    QUrl endpoint = latestReleaseApiUrl();
#ifndef NDEBUG
    if (qEnvironmentVariableIsSet("ISPVIEW_UPDATE_API_URL"))
        endpoint = QUrl(qEnvironmentVariable("ISPVIEW_UPDATE_API_URL"));
#endif
    if (!endpoint.isValid()) {
        setUpdateState(QStringLiteral("error"));
        return;
    }
    QNetworkRequest request{endpoint};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("MVPImageViewer/%1").arg(applicationVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30'000);
    QNetworkReply* reply = networkManager_->get(request);
    activeUpdateReply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (activeUpdateReply_ == reply)
            activeUpdateReply_.clear();
        QSettings().setValue(QLatin1String(kLastUpdateCheckKey), QDateTime::currentDateTimeUtc());
        const auto deleteReply = qScopeGuard([reply] { reply->deleteLater(); });
        if (reply->error() != QNetworkReply::NoError) {
            setUpdateState(QStringLiteral("error"));
            return;
        }
        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        QString version = release.value(QStringLiteral("tag_name")).toString();
        while (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
            version.remove(0, 1);
        const QUrl url(release.value(QStringLiteral("html_url")).toString());
        if (version.isEmpty() || !url.isValid()) {
            setUpdateState(QStringLiteral("error"));
            return;
        }
        const bool available =
            QVersionNumber::compare(QVersionNumber::fromString(version),
                                    QVersionNumber::fromString(applicationVersion())) > 0;
        if (!available) {
            setUpdateState(QStringLiteral("latest"), version, url);
            return;
        }

        const QString suffix = platformInstallerSuffix();
        const QRegularExpression sha256DigestPattern(
            QStringLiteral("^sha256:([0-9a-fA-F]{64})$"));
        const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue& value : assets) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            const QUrl downloadUrl(
                asset.value(QStringLiteral("browser_download_url")).toString());
            if (!suffix.isEmpty() && name.endsWith(suffix, Qt::CaseInsensitive)) {
                installerAssetName_ = name;
                installerUrl_ = downloadUrl;
                installerAssetSize_ = asset.value(QStringLiteral("size")).toInteger();
                const QRegularExpressionMatch digestMatch = sha256DigestPattern.match(
                    asset.value(QStringLiteral("digest")).toString());
                expectedInstallerSha256_ =
                    digestMatch.hasMatch() ? digestMatch.captured(1).toLower() : QString{};
            }
        }
        if (installerAssetName_.isEmpty() || !isAllowedUpdateUrl(installerUrl_) ||
            expectedInstallerSha256_.isEmpty()) {
            updateError_ = QStringLiteral(
                "This release does not contain a compatible, verifiable installer.");
            setUpdateState(QStringLiteral("error"), version, url);
            return;
        }
        setUpdateState(QStringLiteral("available"), version, url);
    });
}

void AppSettings::downloadUpdate() {
    if (updateState_ != QStringLiteral("available") || !isAllowedUpdateUrl(installerUrl_) ||
        expectedInstallerSha256_.isEmpty())
        return;
    if (!networkManager_)
        networkManager_ = new QNetworkAccessManager(this);

    cancelUpdateRequested_ = false;
    updateError_.clear();
    updateDownloadProgress_ = 0;
    setUpdateState(QStringLiteral("downloading"), latestVersion_, releaseUrl_);
    beginInstallerDownload();
}

void AppSettings::beginInstallerDownload() {
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString updateDirectory = QDir(cacheRoot).filePath(QStringLiteral("updates"));
    if (cacheRoot.isEmpty() || !QDir().mkpath(updateDirectory)) {
        failUpdate(QStringLiteral("The update cache directory could not be created."));
        return;
    }
    const QString targetPath = QDir(updateDirectory).filePath(installerAssetName_);
    updateFile_ = std::make_unique<QSaveFile>(targetPath);
    if (!updateFile_->open(QIODevice::WriteOnly)) {
        failUpdate(updateFile_->errorString());
        return;
    }
    updateHash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    downloadedBytes_ = 0;

    QNetworkRequest request{installerUrl_};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("MVPImageViewer/%1").arg(applicationVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60'000);
    QNetworkReply* reply = networkManager_->get(request);
    activeUpdateReply_ = reply;
    connect(reply, &QIODevice::readyRead, this, [this, reply] {
        const QByteArray chunk = reply->readAll();
        if (chunk.isEmpty() || !updateFile_ || !updateHash_)
            return;
        if (updateFile_->write(chunk) != chunk.size()) {
            updateError_ = updateFile_->errorString();
            reply->abort();
            return;
        }
        updateHash_->addData(chunk);
        downloadedBytes_ += chunk.size();
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                const qint64 expected = total > 0 ? total : installerAssetSize_;
                if (expected > 0) {
                    updateDownloadProgress_ =
                        qBound(0, static_cast<int>(received * 100 / expected), 100);
                }
                emit updateStateChanged();
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply, targetPath] {
        if (activeUpdateReply_ == reply)
            activeUpdateReply_.clear();
        const auto deleteReply = qScopeGuard([reply] { reply->deleteLater(); });
        if (cancelUpdateRequested_) {
            cancelUpdateRequested_ = false;
            if (updateFile_)
                updateFile_->cancelWriting();
            updateFile_.reset();
            updateHash_.reset();
            updateDownloadProgress_ = -1;
            setUpdateState(QStringLiteral("available"), latestVersion_, releaseUrl_);
            return;
        }
        if (!updateError_.isEmpty() || reply->error() != QNetworkReply::NoError) {
            const QString error = !updateError_.isEmpty() ? updateError_ : reply->errorString();
            failUpdate(error);
            return;
        }
        setUpdateState(QStringLiteral("verifying"), latestVersion_, releaseUrl_);
        if (!updateFile_ || !updateHash_ ||
            (installerAssetSize_ > 0 && downloadedBytes_ != installerAssetSize_) ||
            QString::fromLatin1(updateHash_->result().toHex()) != expectedInstallerSha256_) {
            failUpdate(QStringLiteral("The downloaded installer failed SHA-256 verification."));
            return;
        }
        if (!updateFile_->commit()) {
            failUpdate(updateFile_->errorString());
            return;
        }
        updateFile_.reset();
        updateHash_.reset();
        downloadedUpdatePath_ = targetPath;
        updateDownloadProgress_ = 100;
        setUpdateState(QStringLiteral("ready"), latestVersion_, releaseUrl_);
    });
}

void AppSettings::cancelUpdateDownload() {
    if (updateState_ != QStringLiteral("downloading") &&
        updateState_ != QStringLiteral("verifying"))
        return;
    cancelUpdateRequested_ = true;
    if (activeUpdateReply_)
        activeUpdateReply_->abort();
}

void AppSettings::installUpdate() {
    if (updateState_ != QStringLiteral("ready") || downloadedUpdatePath_.isEmpty() ||
        !QFileInfo::exists(downloadedUpdatePath_))
        return;
    bool started = false;
#if defined(Q_OS_MACOS)
    started = QProcess::startDetached(QStringLiteral("/usr/bin/open"), {downloadedUpdatePath_});
#elif defined(Q_OS_WIN)
    started = QProcess::startDetached(downloadedUpdatePath_, QStringList{});
#endif
    if (!started) {
        failUpdate(QStringLiteral("The installer could not be opened."));
        return;
    }
    setUpdateState(QStringLiteral("installing"), latestVersion_, releaseUrl_);
    QTimer::singleShot(250, QCoreApplication::instance(), &QCoreApplication::quit);
}

void AppSettings::failUpdate(const QString& error) {
    if (updateFile_)
        updateFile_->cancelWriting();
    updateFile_.reset();
    updateHash_.reset();
    updateDownloadProgress_ = -1;
    updateError_ = error;
    setUpdateState(QStringLiteral("error"), latestVersion_, releaseUrl_);
}

void AppSettings::clearDownloadedUpdate() {
    downloadedUpdatePath_.clear();
    updateFile_.reset();
    updateHash_.reset();
}

bool AppSettings::isAllowedUpdateUrl(const QUrl& url) const {
    if (!url.isValid() || url.host().isEmpty())
        return false;
    if (url.scheme() == QStringLiteral("https"))
        return true;
#ifndef NDEBUG
    return qEnvironmentVariableIsSet("ISPVIEW_UPDATE_API_URL") &&
           url.scheme() == QStringLiteral("http");
#else
    return false;
#endif
}

void AppSettings::openReleasePage() const {
    const QUrl url =
        releaseUrl_.isValid() ? releaseUrl_ : repositoryUrl(QStringLiteral("/releases"));
    if (url.isValid())
        QDesktopServices::openUrl(url);
}

void AppSettings::openUserGuide() const {
    QUrl url = repositoryUrl();
    url.setFragment(QStringLiteral("readme"));
    if (url.isValid())
        QDesktopServices::openUrl(url);
}

void AppSettings::restoreDefaults() {
    setLanguage(QStringLiteral("system"));
    setTheme(QStringLiteral("system"));
    setRestoreLastDirectory(true);
    setConfirmTrash(true);
    setAutomaticUpdateChecks(true);
    setApplyEmbeddedColorProfiles(true);
    setPreserveHighBitDepth(true);
    setHonorExifOrientation(true);
    setCanvasBackground(QStringLiteral("neutral"));
    setSmoothDisplay(true);
    resetShortcuts();
}

void AppSettings::applyLanguage() {
    if (!application_)
        return;
    application_->removeTranslator(&translator_);
    if (effectiveLanguage() == QStringLiteral("zh_CN") &&
        translator_.load(QStringLiteral(":/i18n/ispimageviewer_zh_CN.qm"))) {
        application_->installTranslator(&translator_);
    }
}

void AppSettings::setUpdateState(const QString& state, const QString& latestVersion,
                                 const QUrl& releaseUrl) {
    if (updateState_ == state && latestVersion_ == latestVersion && releaseUrl_ == releaseUrl)
        return;
    updateState_ = state;
    latestVersion_ = latestVersion;
    releaseUrl_ = releaseUrl;
    emit updateStateChanged();
}

QString AppSettings::normalizedShortcut(const QString& sequence) {
    const QKeySequence parsed =
        QKeySequence::fromString(sequence.trimmed(), QKeySequence::PortableText);
    if (parsed.isEmpty())
        return {};
    return parsed.toString(QKeySequence::PortableText);
}

QString AppSettings::normalizedLanguage(const QString& language) {
    return language == QStringLiteral("zh_CN") || language == QStringLiteral("en")
               ? language
               : QStringLiteral("system");
}

QString AppSettings::normalizedTheme(const QString& theme) {
    return theme == QStringLiteral("light") || theme == QStringLiteral("dark")
               ? theme
               : QStringLiteral("system");
}

QString AppSettings::normalizedCanvasBackground(const QString& background) {
    return background == QStringLiteral("dark") || background == QStringLiteral("black") ||
                   background == QStringLiteral("white")
               ? background
               : QStringLiteral("neutral");
}

} // namespace ispview
