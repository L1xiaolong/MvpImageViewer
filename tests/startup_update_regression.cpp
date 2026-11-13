#include "qml/app_settings.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHostAddress>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <functional>

static void require(bool value, const char* message) { if (!value) qFatal("%s", message); }
static void waitFor(const std::function<bool()>& condition) {
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    require(condition(), "Timed out waiting for local update fixture");
}
static void waitBriefly() {
    QEventLoop loop;
    QTimer::singleShot(250, &loop, &QEventLoop::quit);
    loop.exec();
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
        std::fprintf(stderr, "%s\n", qPrintable(message));
    });
    QCoreApplication::setOrganizationName(QStringLiteral("MvpViewUpdateRegression"));
    QCoreApplication::setApplicationName(QStringLiteral("StartupUpdates"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.19"));
    QTemporaryDir directory;
    require(directory.isValid(), "Isolated settings directory required");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());

    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), "Cannot start local update fixture");
    int requests = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        while (auto* socket = server.nextPendingConnection()) {
            socket->setParent(&server);
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("answered").toBool()) return;
                socket->setProperty("answered", true);
                ++requests;
#ifdef Q_OS_MACOS
                const QByteArray suffix = "-macos-arm64.dmg";
#else
                const QByteArray suffix = "-windows-x64-setup.exe";
#endif
                const QByteArray body = "{\"tag_name\":\"v999.0.0\",\"html_url\":\"https://github.com/L1xiaolong/MvpImageViewer/releases/tag/v999.0.0\",\"assets\":[{\"name\":\"Installer"
                    + suffix + "\",\"browser_download_url\":\"https://github.com/L1xiaolong/MvpImageViewer/releases/download/v999.0.0/Installer"
                    + suffix + "\",\"size\":1,\"digest\":\"sha256:" + QByteArray(64, 'a') + "\"}]}";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        }
    });
    qputenv("MVPVIEW_UPDATE_API_URL", QByteArray("http://127.0.0.1:")
        + QByteArray::number(server.serverPort()) + "/release");
    QSettings().setValue(QStringLiteral("updates/lastCheckUtc"), QDateTime::currentDateTimeUtc());
    {
        mvpview::AppSettings settings(&app);
        settings.setAutomaticUpdateChecks(false);
        settings.startAutomaticUpdateCheck();
        waitBriefly();
        require(requests == 0 && settings.updateState() == QStringLiteral("idle"),
                "Disabled automatic checks must not send a startup request");
    }
    for (int launch = 1; launch <= 2; ++launch) {
        mvpview::AppSettings settings(&app);
        settings.setAutomaticUpdateChecks(true);
        settings.startAutomaticUpdateCheck();
        settings.startAutomaticUpdateCheck();
        waitFor([&] { return settings.updateState() == QStringLiteral("available"); });
        require(requests == launch, "Each launch must check once even after a recent check");
        require(settings.latestVersion() == QStringLiteral("999.0.0"), "Available version must reach QML");
    }
    {
        mvpview::AppSettings settings(&app);
        settings.startAutomaticUpdateCheck();
        settings.setAutomaticUpdateChecks(false);
        waitBriefly();
        require(requests == 2, "Turning checks off during startup delay must cancel the request");
        settings.checkForUpdates();
        waitFor([&] { return settings.updateState() == QStringLiteral("available"); });
        require(requests == 3, "Manual checks must still work when automatic checks are off");
    }
    qInfo("Startup update checks: preference, every launch, deduplication, delayed opt-out, manual checks passed");
    return 0;
}
