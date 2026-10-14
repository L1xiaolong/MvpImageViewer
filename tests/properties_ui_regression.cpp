#include "qml/app_settings.h"
#include <QClipboard>
#include <QDir>
#include <QEventLoop>
#include <QFontDatabase>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QTimer>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QSettings>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

class PropertiesCapture : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QString clipboardText() const { return QGuiApplication::clipboard()->text(); }
    Q_INVOKABLE bool save(QQuickItem* item, const QString& path) {
        if (!item) return false;
        auto result = item->grabToImage();
        if (!result) return false;
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(result.data(), &QQuickItemGrabResult::ready, &loop, &QEventLoop::quit);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        deadline.start(5000);
        loop.exec();
        return !result->image().isNull() && result->image().save(path);
    }
    Q_INVOKABLE QQuickItem* findItem(QQuickItem* root, const QString& name) {
        if (!root) return nullptr;
        if (root->objectName() == name) return root;
        for (auto* child : root->childItems())
            if (auto* found = findItem(child, name)) return found;
        return nullptr;
    }
};

class PropertiesTestSetup : public QObject {
    Q_OBJECT
public slots:
    void applicationAvailable() {
        if (!directory_.isValid()) qFatal("Cannot create isolated settings test directory");
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QCoreApplication::setOrganizationName(QStringLiteral("MvpViewPropertiesRegression"));
        QCoreApplication::setApplicationName(QStringLiteral("PropertiesTests"));
        QCoreApplication::setApplicationVersion(QStringLiteral("0.2.17"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory_.path());
    }
    void qmlEngineAvailable(QQmlEngine* engine) {
        engine->rootContext()->setContextProperty(QStringLiteral("systemUiFontFamily"),
            #ifdef Q_OS_WIN
            QStringLiteral("Segoe UI")
#else
            QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()
#endif
        );
        engine->rootContext()->setContextProperty(QStringLiteral("systemFixedFontFamily"),
            #ifdef Q_OS_WIN
            QStringLiteral("Consolas")
#else
            QFontDatabase::systemFont(QFontDatabase::FixedFont).family()
#endif
        );
        auto* capture = new PropertiesCapture(engine);
        engine->rootContext()->setContextProperty(QStringLiteral("testCapture"), capture);
        settings_ = new mvpview::AppSettings(qobject_cast<QGuiApplication*>(qApp), engine);
        settings_->setLanguage(QStringLiteral("zh_CN"));
        QObject::connect(settings_, &mvpview::AppSettings::languageChanged, engine, &QQmlEngine::retranslate);
        engine->rootContext()->setContextProperty(QStringLiteral("testSettings"), settings_);
        QDir().mkpath(QStringLiteral(PROPERTIES_ARTIFACT_DIR));
        engine->rootContext()->setContextProperty(QStringLiteral("testArtifactDir"), QStringLiteral(PROPERTIES_ARTIFACT_DIR));
    }
private:
    QTemporaryDir directory_{QDir::currentPath() + QStringLiteral("/properties-ui-settings-XXXXXX")};
    mvpview::AppSettings* settings_ = nullptr;
};
QUICK_TEST_MAIN_WITH_SETUP(properties_ui, PropertiesTestSetup)
#include "properties_ui_regression.moc"

