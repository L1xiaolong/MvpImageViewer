#include "browser/thumbnail_model.h"
#include "io/default_image_decoder.h"
#include "qml/browse_controller.h"
#include "qml/browse_workspace_controller.h"

#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using namespace mvpview;

static void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary directory failed");
        QCoreApplication::setOrganizationName(QStringLiteral("MvpWorkspaceRegression"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
        QStringList directories;
        for (int pane = 0; pane < 4; ++pane) {
            const QString directory = temporary.path() + QStringLiteral("/pane%1").arg(pane);
            check(QDir().mkpath(directory), "create folder failed");
            directories.append(directory);
            QImage image(8, 8, QImage::Format_RGB32);
            image.fill(Qt::red);
            for (int row = 0; row < 3; ++row)
                check(image.save(directory + QStringLiteral("/%1.png").arg(row)), "save failed");
        }
        BrowseWorkspaceController workspace(createDefaultImageDecoder(), {}, true);
        for (int pane = 1; pane < 4; ++pane) workspace.addFileManagerPane();
        QList<BrowseController*> panes;
        for (const QVariant& value : workspace.panes())
            panes.append(qobject_cast<BrowseController*>(value.value<QObject*>()));
        for (int pane = 0; pane < 4; ++pane) panes[pane]->openDirectory(directories[pane]);
        QEventLoop loop;
        QTimer poll, deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
            for (auto* pane : panes) if (pane->thumbnails()->rowCount() != 3) return;
            loop.quit();
        });
        poll.start(5);
        deadline.start(10000);
        loop.exec();
        poll.stop();
        for (auto* pane : panes) check(pane->thumbnails()->rowCount() == 3, "scan timed out");
        const auto path = [&](int pane, int row = 0) {
            return panes[pane]->thumbnails()->index(row, 0).data(ThumbnailModel::PathRole).toString();
        };
        workspace.selectPath(0, path(0));
        workspace.selectPath(1, path(1));
        check(panes[0]->selectionCount() == 0 && workspace.workspaceSelectionCount() == 1,
              "plain click retained another pane's selection");
        for (int pane = 0; pane < 4; ++pane)
            if (pane != 1) workspace.selectPath(pane, path(pane), false, true);
        check(workspace.workspaceSelectionCount() == 4, "Ctrl did not preserve cross-pane selection");
        workspace.selectPath(3, path(3), false, true);
        check(panes[3]->selectionCount() == 0 && workspace.workspaceSelectionCount() == 3,
              "Ctrl did not toggle off");
        workspace.selectPath(1, path(1, 2), true);
        check(panes[1]->selectionCount() == 3 && workspace.workspaceSelectionCount() == 3,
              "Shift lost local anchor or retained other panes");
        workspace.selectPath(0, path(0), false, true);
        workspace.clearSelection();
        check(workspace.workspaceSelectionCount() == 0, "workspace selection not cleared");
        for (auto* pane : panes) {
            check(pane->selectionCount() == 0, "pane selection not cleared");
            for (int row = 0; row < 3; ++row) {
                const auto index = pane->thumbnails()->index(row, 0);
                check(!index.data(ThumbnailModel::SelectedRole).toBool() &&
                      index.data(ThumbnailModel::SelectionOrdinalRole).toInt() == 0,
                      "thumbnail selection or ordinal not cleared");
            }
        }
        workspace.clearSelection();
        workspace.selectPath(2, path(2));
        check(workspace.workspaceSelectionCount() == 1, "selection after clear failed");
        std::cout << "Workspace selection regression passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
