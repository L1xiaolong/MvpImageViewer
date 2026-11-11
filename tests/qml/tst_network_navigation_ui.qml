import QtQuick
import QtQuick.Window
import QtTest
import "../../design" as Design

Item {
    id: scene
    width: 1280
    height: 900
    Design.DesignPreview { id: preview; anchors.fill: parent }
    TestCase {
        name: "NetworkNavigationUI"
        when: windowShown
        function init() {
            scene.Window.window.width = 1280
            scene.Window.window.height = 900
        }
        function test_network_marker() {
            const page = findChild(preview, "browsePage")
            const navigator = findChild(page, "folderNavigator")
            navigator.platformName = "windows"
            tryVerify(function() {
                const label = findChild(navigator, "sidebarPlaceLabel-Z:/")
                return label && label.text.indexOf(" · ") >= 0
            })
        }
        function test_network_error_dialog_blocks_interaction() {
            const page = findChild(preview, "browsePage")
            const message = "Opening the network folder timed out after 10 seconds."
            page.workspaceController.directoryOpenFailed("Z:/offline", message)
            const dialog = findChild(page, "directoryErrorDialog")
            tryCompare(dialog, "opened", true)
            verify(dialog.completedWithError)
            verify(!page.contentInteractionEnabled)
            verify(!page.fileShortcutsEnabled)
            dialog.confirmAction()
            tryCompare(dialog, "visible", false)
            verify(page.contentInteractionEnabled)
        }
    }
}
