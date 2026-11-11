import QtQuick
import QtTest
import "../../src/qml/Mvp"

Item {
    width: 360
    height: 900
    FolderNavigator {
        id: navigator
        anchors.fill: parent
        controller: testWorkspace.activePane
        iconPrefix: Qt.resolvedUrl("../../assets/icons/ui/")
    }
    TestCase {
        name: "FolderNavigationUI"
        when: windowShown

        function tree() {
            if (Qt.platform.os === "windows") {
                const loader = findChild(navigator, "driveTreeLoader-" + testDriveRoot)
                return loader ? loader.item : null
            }
            return findChild(navigator, "nativeFolderTree")
        }
        function verifyCurrentPath() {
            tryVerify(function() { return tree() !== null }, 10000)
            tryVerify(function() {
                const view = tree()
                const controller = testWorkspace.activePane
                const index = controller.folderTreeIndex(controller.currentDirectory,
                                                         view.drivePath)
                return view.rowAtIndex(index) >= 0
            }, 10000, "The active pane's full folder path must be expanded")
        }
        function test_switch_between_loaded_panes() {
            verifyCurrentPath()
            for (let round = 0; round < 3; ++round) {
                testWorkspace.activatePane(1)
                verifyCurrentPath()
                verifyOtherPathCollapsed(0)
                testWorkspace.activatePane(0)
                verifyCurrentPath()
                verifyOtherPathCollapsed(1)
            }
        }
        function test_windows_volume_label() {
            if (Qt.platform.os !== "windows") skip("Windows volume label")
            if (testVolumeName.length === 0) skip("Drive has no volume label")
            tryVerify(function() {
                const label = findChild(navigator, "sidebarPlaceLabel-" + testDriveRoot)
                return label && label.text === testVolumeName + " (" + testDriveRoot.slice(0, 2) + ")"
            }, 5000)
        }
        function verifyOtherPathCollapsed(paneIndex) {
            const view = tree()
            const path = testWorkspace.panes[paneIndex].currentDirectory
            const index = testWorkspace.activePane.folderTreeIndex(path, view.drivePath)
            compare(view.rowAtIndex(index), -1, "Inactive folder branch must be collapsed")
        }
        function test_switch_to_collapsed_drive() {
            if (Qt.platform.os !== "windows") skip("Windows drive disclosure")
            testWorkspace.activatePane(0)
            verifyCurrentPath()
            navigator.setDriveExpanded(testDriveRoot, false)
            const loader = findChild(navigator, "driveTreeLoader-" + testDriveRoot)
            compare(loader.visible, false)
            testWorkspace.activatePane(1)
            tryCompare(loader, "visible", true)
            verifyCurrentPath()
        }
    }
}
