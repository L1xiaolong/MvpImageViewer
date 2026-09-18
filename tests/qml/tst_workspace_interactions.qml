// qmllint disable unqualified
import QtQuick
import QtTest
import "../../design"
import "../../src/qml/Pages"

TestCase {
    id: testCase
    name: "WorkspaceInteractions"
    when: windowShown
    width: 1440
    height: 900
    visible: true

    MockBrowseWorkspace {
        id: workspace
        paneCount: 4
    }

    BrowsePage {
        id: page
        anchors.fill: parent
        controller: workspace.activePane
        workspaceController: workspace
        designMode: true
        iconPrefix: Qt.resolvedUrl("../../assets/icons/ui/").toString()
    }

    function init() {
        workspace.paneOrder = workspace.allPanes.slice();
        workspace.paneCount = 4;
        workspace.activePaneIndex = 0;
        workspace.pane0.setDisplayMode(0);
        wait(120);
    }

    function test_fourPanesUseEqualTwoByTwoGrid() {
        const first = findChild(page, "browserPane-0");
        const second = findChild(page, "browserPane-1");
        const third = findChild(page, "browserPane-2");
        const fourth = findChild(page, "browserPane-3");
        verify(first && second && third && fourth);
        for (let paneIndex = 0; paneIndex < 4; ++paneIndex) {
            verify(findChild(page, "paneBackButton-" + paneIndex) !== null)
            verify(findChild(page, "paneForwardButton-" + paneIndex) !== null)
            verify(findChild(page, "paneUpButton-" + paneIndex) !== null)
            verify(findChild(page, "paneLocationField-" + paneIndex) !== null)
            verify(findChild(page, "paneLocationDropButton-" + paneIndex) !== null)
        }
        compare(Math.round(first.width), Math.round(second.width));
        verify(Math.abs(third.width - fourth.width) < 2);
        verify(Math.abs(first.height - third.height) < 2);
    }

    function test_threePanesUseEqualHorizontalColumns() {
        workspace.paneCount = 3;
        workspace.normalizeDisplayModes();
        wait(120);
        const first = findChild(page, "browserPane-0");
        const second = findChild(page, "browserPane-1");
        const third = findChild(page, "browserPane-2");
        verify(first && second && third);
        verify(Math.abs(first.width - second.width) < 2);
        verify(Math.abs(second.width - third.width) < 2);
        compare(workspace.panes[0].displayMode, 1);
        compare(workspace.panes[1].displayMode, 1);
        compare(workspace.panes[2].displayMode, 1);
    }

    function test_splitterDragAndResetAction() {
        workspace.paneCount = 2;
        wait(120);
        const first = findChild(page, "browserPane-0");
        const second = findChild(page, "browserPane-1");
        const handle = findChild(page, "horizontalPaneHandle");
        verify(first && second && handle);
        mouseDrag(handle, handle.width / 2, handle.height / 2, 100, 0, Qt.LeftButton);
        verify(Math.abs(first.width - second.width) > 100);
        mouseClick(handle, 1, handle.height / 2, Qt.LeftButton);
        mouseClick(handle, 1, handle.height / 2, Qt.LeftButton);
        tryVerify(function() { return Math.abs(first.width - second.width) < 2 }, 1000,
                  "Double-clicking the splitter should restore equal pane widths");
    }

    function test_closeAllPanesLeavesOneTreeControlledManager() {
        workspace.closePane(3);
        workspace.closePane(2);
        workspace.closePane(1);
        workspace.closePane(0);
        wait(120);
        compare(workspace.paneCount, 1);
        verify(workspace.hasActivePane);
        compare(workspace.activePane.currentDirectory, "")
        verify(findChild(page, "browserPane-0") !== null)
        verify(findChild(page, "emptyWorkspace") === null)
        const locationField = findChild(page, "paneLocationField-0")
        verify(locationField !== null)
        tryCompare(locationField, "text", "")
    }

    function test_addFocusesNewManagerAndTwoPanesDisableGallery() {
        workspace.paneCount = 1;
        workspace.activePaneIndex = 0;
        workspace.panes[0].setDisplayMode(2);
        workspace.addFileManagerPane();
        compare(workspace.activePaneIndex, 1);
        compare(workspace.panes[0].displayMode, 0);
    }

    function test_crossPaneSelectionsRemainIndependent() {
        workspace.paneCount = 2;
        const firstPath = workspace.panes[0].thumbnails.get(0).path;
        const secondPath = workspace.panes[1].thumbnails.get(1).path;
        workspace.selectPath(0, firstPath, false, false);
        workspace.selectPath(1, secondPath, false, false);
        compare(workspace.workspaceSelectedPaths.length, 2);
        compare(workspace.workspaceSelectedPaths[0], firstPath);
        compare(workspace.workspaceSelectedPaths[1], secondPath);
        workspace.selectPath(0, firstPath, false, true);
        compare(workspace.workspaceSelectedPaths.length, 1);
        compare(workspace.workspaceSelectedPaths[0], secondPath);
        workspace.selectPath(0, firstPath, false, true);
        compare(workspace.workspaceSelectedPaths.length, 2);
    }
}
