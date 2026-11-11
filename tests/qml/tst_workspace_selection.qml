import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import "../../design" as Design
import "../../src/qml/Mvp"

Item {
    id: scene
    width: 1280
    height: 640
    Design.MockBrowseWorkspace { id: workspace; paneCount: 2 }
    BrowserPane {
        id: pane
        width: 1060
        height: parent.height
        controller: workspace.panes[0]
        workspaceController: workspace
        paneIndex: 0
    }
    FolderNavigator {
        id: navigator
        x: 1070
        width: 210
        height: parent.height
        controller: workspace.panes[0]
    }
    TestCase {
        name: "WorkspaceSelectionUI"
        when: windowShown
        function init() {
            scene.Window.window.width = 1280
            scene.Window.window.height = 640
            workspace.clearSelection()
            workspace.panes[0].displayMode = 0
            wait(50)
        }
        function grid() { return findChild(pane, "paneContactSheet-0") }
        function selectAcrossPanes() {
            workspace.selectPath(0, workspace.panes[0].thumbnails.get(0).path, false, false)
            workspace.selectPath(1, "/Other/image.png", false, true)
            compare(workspace.workspaceSelectionCount, 2)
        }
        function test_blank_click_data() {
            return [{tag: "grid-gap", mode: 0, gap: true},
                    {tag: "grid-empty", mode: 0, gap: false},
                    {tag: "list-gap", mode: 1, gap: true},
                    {tag: "list-empty", mode: 1, gap: false}]
        }
        function test_blank_click(data) {
            workspace.panes[0].displayMode = data.mode
            const view = grid()
            wait(50)
            view.positionViewAtBeginning()
            wait(50)
            selectAcrossPanes()
            if (data.gap) {
                mouseClick(view, 20, view.cellHeight - 2)
            } else {
                view.positionViewAtEnd()
                wait(50)
                mouseClick(view, view.width - 18, view.height - 12)
            }
            compare(workspace.workspaceSelectionCount, 0)
            compare(workspace.panes[0].selectionCount, 0)
            compare(workspace.panes[1].selectionCount, 0)
        }
        function test_thumbnail_click_and_ctrl() {
            const view = grid()
            view.positionViewAtBeginning()
            wait(50)
            workspace.selectPath(1, "/Other/image.png", false, false)
            mouseClick(view, 30, 30, Qt.LeftButton, Qt.ControlModifier)
            compare(workspace.workspaceSelectionCount, 2)
            mouseClick(view, view.cellWidth + 30, 30)
            compare(workspace.workspaceSelectionCount, 1)
            compare(workspace.panes[1].selectionCount, 0)
        }
        function test_recent_tooltip() {
            // The first recent entry follows the section heading.
            mouseMove(navigator, 65, 56)
            tryVerify(function() {
                return navigator.ToolTip.toolTip && navigator.ToolTip.toolTip.visible
            }, 1500)
            compare(navigator.ToolTip.toolTip.text, workspace.panes[0].recentFolders[0])
        }
    }
}
