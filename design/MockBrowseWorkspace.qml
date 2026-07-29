import QtQuick

QtObject {
    id: root
    signal transferConfirmationRequested(bool move, int itemCount, string targetDirectory)

    // Change this value from 1 through 4 in Design Studio to inspect every
    // production workspace geometry without substituting a design-only page.
    property int paneCount: 3
    property int activePaneIndex: paneCount > 0 ? Math.min(0, paneCount - 1) : -1
    readonly property bool hasActivePane: activePaneIndex >= 0 && activePaneIndex < paneCount
    readonly property bool canAddPane: paneCount < 4
    readonly property var allPanes: [pane0, pane1, pane2, pane3]
    property var paneOrder: [pane0, pane1, pane2, pane3]
    readonly property var panes: paneOrder.slice(0, paneCount)
    readonly property var activePane: hasActivePane ? panes[activePaneIndex] : emptyPane
    readonly property var workspaceSelectedPaths: collectSelectedPaths()
    readonly property int workspaceSelectionCount: workspaceSelectedPaths.length
    readonly property bool canCompare: workspaceSelectionCount >= 2 && workspaceSelectionCount <= 4
    readonly property string statusText: hasActivePane
        ? activePane.statusText + (workspaceSelectionCount > 0
          ? " · " + workspaceSelectionCount + " image(s) across " + selectedPaneCount() + " manager(s)" : "")
        : "Choose a folder for this file manager"

    signal compareRequested(var paths)
    function collectSelectedPaths() {
        const result = [];
        for (let paneIndex = 0; paneIndex < panes.length; ++paneIndex) {
            const selected = panes[paneIndex].selectedPaths;
            for (let index = 0; index < selected.length; ++index) {
                if (result.indexOf(selected[index]) < 0)
                    result.push(selected[index]);
            }
        }
        return result;
    }

    function selectedPaneCount() {
        let count = 0;
        for (let index = 0; index < panes.length; ++index) {
            if (panes[index].selectedPaths.length > 0)
                ++count;
        }
        return count;
    }

    function refreshOrdinals() {
        const order = collectSelectedPaths();
        for (let index = 0; index < panes.length; ++index)
            panes[index].setWorkspaceSelectionOrder(order);
    }

    function addFileManagerPane() {
        if (!canAddPane)
            return;
        paneCount += 1;
        activePaneIndex = paneCount - 1;
        normalizeDisplayModes();
        refreshOrdinals();
    }

    function confirmPendingTransfer() {
        activePane.confirmPendingTransfer()
    }

    function cancelPendingTransfer() {
        activePane.cancelPendingTransfer()
    }

    function activatePane(index) {
        if (index < 0 || index >= paneCount || activePaneIndex === index)
            return;
        activePaneIndex = index;
    }

    function closePane(index) {
        if (index < 0 || index >= paneCount)
            return;
        if (paneCount === 1) {
            panes[0].currentDirectory = "";
            panes[0].currentFolderName = ""
            panes[0].clearSelection()
            panes[0].statusText = "Choose a folder for this file manager"
            activePaneIndex = 0
            return
        }
        const order = paneOrder.slice();
        const removed = order.splice(index, 1)[0];
        removed.currentDirectory = "";
        removed.currentFolderName = "";
        removed.clearSelection();
        removed.statusText = "Choose a folder for this file manager";
        order.push(removed);
        paneOrder = order;
        const closedActive = activePaneIndex === index;
        paneCount -= 1;
        if (closedActive)
            activePaneIndex = Math.min(index, paneCount - 1);
        else if (index < activePaneIndex)
            activePaneIndex -= 1;
        refreshOrdinals();
    }

    function selectPath(paneIndex, path, extend, toggle) {
        if (paneIndex < 0 || paneIndex >= paneCount)
            return;
        activatePane(paneIndex);
        panes[paneIndex].selectPath(path, extend, toggle);
        refreshOrdinals();
    }

    function setActiveDisplayMode(mode) {
        if (!hasActivePane)
            return;
        if (paneCount >= 3) {
            activePane.setDisplayMode(1);
            return;
        }
        if (paneCount === 2 && mode === 2)
            return;
        activePane.setDisplayMode(mode);
    }

    function normalizeDisplayModes() {
        const currentPanes = panes;
        if (paneCount >= 3) {
            for (let index = 0; index < paneCount; ++index) {
                if (currentPanes[index])
                    currentPanes[index].setDisplayMode(1);
            }
        } else if (paneCount === 2) {
            for (let index = 0; index < paneCount; ++index) {
                if (currentPanes[index] && currentPanes[index].displayMode === 2)
                    currentPanes[index].setDisplayMode(0);
            }
        }
    }

    function compareSelected() {
        if (canCompare)
            compareRequested(workspaceSelectedPaths);
    }

    property MockBrowseController emptyPane: MockBrowseController {
        currentDirectory: ""
        currentFolderName: ""
        selectedPaths: []
        statusText: "Choose a folder for this file manager"
    }
    property MockBrowseController pane0: MockBrowseController {}
    property MockBrowseController pane1: MockBrowseController {
        currentDirectory: ""
        currentFolderName: ""
        selectedPaths: []
        statusText: "Choose a folder for this file manager"
    }
    property MockBrowseController pane2: MockBrowseController {
        currentDirectory: "/Images/Outdoor samples"
        currentFolderName: "Outdoor samples"
        selectedPaths: []
    }
    property MockBrowseController pane3: MockBrowseController {
        currentDirectory: "/Images/Reference charts"
        currentFolderName: "Reference charts"
        selectedPaths: []
    }

    onPaneCountChanged: {
        if (paneCount < 1) {
            paneCount = 1
            return
        }
        normalizeDisplayModes();
        refreshOrdinals();
    }
    Component.onCompleted: {
        normalizeDisplayModes();
        refreshOrdinals();
    }
}
