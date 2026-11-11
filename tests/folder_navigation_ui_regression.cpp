#include "io/default_image_decoder.h"
#include "qml/browse_controller.h"
#include "qml/browse_workspace_controller.h"

#include <QDir>
#include <QFontDatabase>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QSettings>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

class FolderNavigationSetup : public QObject {
    Q_OBJECT
public slots:
    void applicationAvailable() {
        if (!directory_.isValid()) qFatal("Cannot create navigation test directory");
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QCoreApplication::setOrganizationName(QStringLiteral("MvpFolderNavigationRegression"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory_.path());
    }
    void qmlEngineAvailable(QQmlEngine* engine) {
        auto* workspace = new mvpview::BrowseWorkspaceController(
            mvpview::createDefaultImageDecoder(), {}, true, engine);
        const QString first = directory_.path() + QStringLiteral("/first/nested/photos");
        const QString second = directory_.path() + QStringLiteral("/second/other/images");
        if (!QDir().mkpath(first) || !QDir().mkpath(second)) qFatal("Cannot create test folders");
        workspace->activeBrowsePane()->openDirectory(first);
        workspace->addFileManagerPane();
        workspace->activeBrowsePane()->openDirectory(second);
        workspace->activatePane(0);
        auto* context = engine->rootContext();
        context->setContextProperty(QStringLiteral("testWorkspace"), workspace);
        context->setContextProperty(QStringLiteral("testDriveRoot"),
                                   QStorageInfo(directory_.path()).rootPath());
        QString volumeName;
#ifdef Q_OS_WIN
        wchar_t volume[MAX_PATH + 1]{};
        const QString drive = QDir::toNativeSeparators(QStorageInfo(directory_.path()).rootPath());
        if (GetVolumeInformationW(reinterpret_cast<LPCWSTR>(drive.utf16()), volume,
                                  MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0))
            volumeName = QString::fromWCharArray(volume);
#endif
        context->setContextProperty(QStringLiteral("testVolumeName"), volumeName);
        context->setContextProperty(QStringLiteral("systemUiFontFamily"),
                                   QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
        context->setContextProperty(QStringLiteral("systemFixedFontFamily"),
                                   QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
    }
private:
    QTemporaryDir directory_;
};

QUICK_TEST_MAIN_WITH_SETUP(folder_navigation, FolderNavigationSetup)
#include "folder_navigation_ui_regression.moc"
