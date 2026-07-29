#include "qml/app_settings.h"
#include "io/encoded_color_management.h"
#include "io/qt_image_decoder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScopeGuard>
#include <QSettings>
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
constexpr auto kLastUpdateCheckKey = "updates/lastCheckUtc";

QString repositorySlug() {
    return QString::fromUtf8(ISPVIEW_GITHUB_REPOSITORY).trimmed();
}

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
    {"openFolder", "Ctrl+O"},
    {"find", "Ctrl+F"},
    {"settings", "Ctrl+,"},
    {"toggleNavigator", "Ctrl+B"},
    {"compare", "C"},
    {"rename", "F2"},
    {"newFolder", "Ctrl+Shift+N"},
};

const ShortcutDefinition* shortcutDefinition(const QString& action) {
    for (const auto& definition : kShortcutDefinitions) {
        if (action == QLatin1String(definition.id))
            return &definition;
    }
    return nullptr;
}
}

AppSettings::AppSettings(QGuiApplication* application, QObject* parent)
    : QObject(parent), application_(application) {
    const QSettings settings;
    language_ = normalizedLanguage(settings.value(QLatin1String(kLanguageKey),
                                                  QStringLiteral("system")).toString());
    theme_ = normalizedTheme(settings.value(QLatin1String(kThemeKey),
                                            QStringLiteral("system")).toString());
    if (qEnvironmentVariableIsSet("ISPVIEW_LANGUAGE_OVERRIDE")) {
        language_ = normalizedLanguage(qEnvironmentVariable("ISPVIEW_LANGUAGE_OVERRIDE"));
    }
    if (qEnvironmentVariableIsSet("ISPVIEW_THEME_OVERRIDE")) {
        theme_ = normalizedTheme(qEnvironmentVariable("ISPVIEW_THEME_OVERRIDE"));
    }
    restoreLastDirectory_ =
        settings.value(QLatin1String(kRestoreLastDirectoryKey), true).toBool();
    confirmTrash_ = settings.value(QLatin1String(kConfirmTrashKey), true).toBool();
    automaticUpdateChecks_ =
        settings.value(QLatin1String(kAutomaticUpdateChecksKey), true).toBool();
    applyEmbeddedColorProfiles_ =
        settings.value(QLatin1String(kApplyEmbeddedColorProfilesKey), true).toBool();
    preserveHighBitDepth_ =
        settings.value(QLatin1String(kPreserveHighBitDepthKey), true).toBool();
    honorExifOrientation_ =
        settings.value(QLatin1String(kHonorExifOrientationKey), true).toBool();
    canvasBackground_ = normalizedCanvasBackground(
        settings.value(QLatin1String(kCanvasBackgroundKey), QStringLiteral("neutral")).toString());
    EncodedColorManagement::setEnabled(applyEmbeddedColorProfiles_);
    QtImageDecoder::setPreserveHighBitDepth(preserveHighBitDepth_);
    QtImageDecoder::setAutoOrientationEnabled(honorExifOrientation_);
    for (const auto& definition : kShortcutDefinitions) {
        const QString action = QLatin1String(definition.id);
        const QString stored = settings.value(QStringLiteral("shortcuts/") + action,
                                              QLatin1String(definition.defaultSequence)).toString();
        const QString normalized = normalizedShortcut(stored);
        shortcuts_.insert(action, normalized.isEmpty()
                                      ? QLatin1String(definition.defaultSequence) : normalized);
    }

    applyLanguage();
    if (application_ && application_->styleHints()) {
        connect(application_->styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this] {
                    if (theme_ == QStringLiteral("system"))
                        emit themeChanged();
                });
    }
}

QString AppSettings::language() const { return language_; }

QString AppSettings::effectiveLanguage() const {
    if (language_ != QStringLiteral("system"))
        return language_;
    return QLocale::system().language() == QLocale::Chinese
               ? QStringLiteral("zh_CN")
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
bool AppSettings::colorManagementAvailable() const {
    return EncodedColorManagement::isAvailable();
}

QString AppSettings::updateState() const { return updateState_; }

QString AppSettings::latestVersion() const { return latestVersion_; }

QUrl AppSettings::releaseUrl() const { return releaseUrl_; }

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

QString AppSettings::applicationVersion() const {
    return QCoreApplication::applicationVersion();
}

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
    const QDateTime lastCheck =
        QSettings().value(QLatin1String(kLastUpdateCheckKey)).toDateTime();
    if (lastCheck.isValid() && lastCheck.secsTo(QDateTime::currentDateTimeUtc()) < 24 * 60 * 60)
        return;
    QTimer::singleShot(1500, this, &AppSettings::checkForUpdates);
}

void AppSettings::checkForUpdates() {
    if (updateState_ == QStringLiteral("checking"))
        return;
    if (!networkManager_)
        networkManager_ = new QNetworkAccessManager(this);

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
    QNetworkReply* reply = networkManager_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        QSettings().setValue(QLatin1String(kLastUpdateCheckKey),
                             QDateTime::currentDateTimeUtc());
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
        setUpdateState(available ? QStringLiteral("available") : QStringLiteral("latest"),
                       version, url);
    });
}

void AppSettings::openReleasePage() const {
    const QUrl url = releaseUrl_.isValid() ? releaseUrl_ : repositoryUrl(QStringLiteral("/releases"));
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
    resetShortcuts();
}

void AppSettings::applyLanguage() {
    if (!application_)
        return;
    application_->removeTranslator(&translator_);
    if (effectiveLanguage() == QStringLiteral("zh_CN")
        && translator_.load(QStringLiteral(":/i18n/ispimageviewer_zh_CN.qm"))) {
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
