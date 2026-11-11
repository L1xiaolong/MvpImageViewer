#include "browser/thumbnail_model.h"
#include "io/default_image_decoder.h"
#include "qml/browse_controller.h"

#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace mvpview;
static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static bool waitUntil(const std::function<bool()>& predicate, int timeout = 3000) {
    if (predicate()) return true;
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (predicate()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(5);
    deadline.start(timeout);
    loop.exec();
    return predicate();
}

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary directory failed");
        QCoreApplication::setOrganizationName(QStringLiteral("MvpNetworkRegression"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        const QString local = temporary.path() + "/local";
        const QString remote = temporary.path() + "/remote";
        const QString other = temporary.path() + "/other";
        for (const auto& path : {local, remote, other}) check(QDir().mkpath(path), "mkdir failed");
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::green);
        check(image.save(remote + "/test.png"), "image save failed");
        auto access = std::make_shared<NetworkFolderAccess>();
        access->isRemote = [remote](const QString& path) { return path == remote; };
        access->read = [](const QString& path) { return std::optional(DirectoryScanner::scan(path)); };
        BrowseController browser(createDefaultImageDecoder(), local, nullptr, access);
        int errors = 0;
        QString error;
        QObject::connect(&browser, &BrowseController::directoryOpenFailed, &app,
                         [&](const QString& path, const QString& message) {
            check(path == remote, "wrong error path");
            ++errors; error = message;
        });
        browser.openDirectory(remote);
        check(waitUntil([&] { return browser.currentDirectory() == remote; }), "network open failed");
        check(browser.thumbnails()->rowCount() == 1, "preloaded network images missing");
        browser.openDirectory(other);
        access->read = [](const QString&) -> std::optional<QVector<ImageFileRecord>> { return {}; };
        browser.navigateBack(); // Unavailable network entry in browsing history.
        check(waitUntil([&] { return errors == 1; }), "unavailable share did not report error");
        check(browser.currentDirectory() == other && browser.canGoBack() && !browser.canGoForward(),
              "failed navigation changed the current folder/history");

        auto gate = std::make_shared<std::promise<void>>();
        const auto ready = gate->get_future().share();
        access->read = [ready](const QString& path) {
            ready.wait();
            return std::optional(DirectoryScanner::scan(path));
        };
        browser.openDirectory(remote);
        auto* timer = browser.findChild<QTimer*>(QStringLiteral("networkDirectoryTimer"));
        check(timer && timer->interval() == 10000, "timeout must be 10 seconds");
        int ticks = 0;
        QTimer heartbeat;
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++ticks; });
        heartbeat.start(100);
        QElapsedTimer elapsed;
        elapsed.start();
        check(waitUntil([&] { return errors == 2; }, 11500), "network timeout did not report error");
        check(elapsed.elapsed() >= 9900 && ticks >= 50, "timeout froze the UI or fired too early");
        check(browser.currentDirectory() == other && error.contains("10"), "timeout did not retain previous folder");
        gate->set_value();
        check(waitUntil([&] { return !timer->isActive(); }), "timer still active");
        // A delayed success must not overwrite a later local navigation.
        browser.openDirectory(local);
        QEventLoop settle;
        QTimer::singleShot(150, &settle, &QEventLoop::quit);
        settle.exec();
        check(browser.currentDirectory() == local && errors == 2, "late success replaced local folder");

        auto cancelGate = std::make_shared<std::promise<void>>();
        const auto cancelReady = cancelGate->get_future().share();
        access->read = [cancelReady](const QString& path) {
            cancelReady.wait();
            return std::optional(DirectoryScanner::scan(path));
        };
        browser.openDirectory(remote);
        browser.openDirectory(other);
        check(!timer->isActive(), "local navigation did not cancel network timeout");
        cancelGate->set_value();
        QTimer::singleShot(150, &settle, &QEventLoop::quit);
        settle.exec();
        check(browser.currentDirectory() == other && errors == 2, "cancelled request changed current folder");
        std::cout << "Network timeout, recovery, history and stale-result checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
