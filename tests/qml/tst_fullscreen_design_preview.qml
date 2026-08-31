import QtQuick
import QtTest
import "../../design"

TestCase {
    id: testCase
    name: "FullScreenDesignPreview"
    when: windowShown
    width: 1440
    height: 900
    visible: true

    FullScreenPreview {
        id: preview
        anchors.fill: parent
    }

    function test_productionPageUsesOnlyFixedCornerInformation() {
        const page = findChild(preview, "fullScreenPage")
        const canvas = findChild(page, "designFullScreenCanvas")
        const imageInfo = findChild(page, "fullScreenImageInfo")
        const pixelInfo = findChild(page, "fullScreenPixelInfo")
        const oldTopPanel = findChild(page, "fullScreenTopPanel")
        const oldBottomPanel = findChild(page, "fullScreenBottomPanel")
        const navigation = findChild(page, "fullScreenNavigationOverlay")
        const zoomLabel = findChild(page, "fullScreenZoomLabel")
        verify(page !== null)
        verify(canvas !== null)
        verify(imageInfo !== null)
        verify(pixelInfo !== null)
        compare(oldTopPanel, null)
        compare(oldBottomPanel, null)
        verify(navigation !== null)
        verify(zoomLabel !== null)
        verify(imageInfo.visible)
        verify(pixelInfo.visible)
        verify(navigation.visible)
        compare(zoomLabel.text, "135%")
    }

    function test_propertyTabsExposeAllInspectorSections() {
        const dialog = findChild(preview, "imagePropertiesDialog")
        verify(dialog !== null)
        dialog.openForPath("/Images/Demo/sample_0001.jpg")
        tryCompare(dialog, "opened", true)
        compare(dialog.tabs.length, 3)
        compare(dialog.tabs[0], "EXIF")
        compare(dialog.tabs[1], "Histogram")
        compare(dialog.tabs[2], "RAW parameters")
        dialog.currentTab = 1
        compare(dialog.currentTab, 1)
        tryVerify(function() {
            const histogram = findChild(dialog, "propertiesHistogram")
            return histogram !== null && histogram.visible
        }, 1000)
        dialog.currentTab = 2
        compare(dialog.currentTab, 2)
    }

    function test_escapeClosesFullScreenAfterPropertiesDialogCloses() {
        const page = findChild(preview, "fullScreenPage")
        const dialog = findChild(page, "imagePropertiesDialog")
        const closeButton = findChild(page, "closeFullScreenButton")
        compare(closeButton, null)

        page.forceActiveFocus()
        dialog.openForPath("/Images/Demo/sample_0001.jpg")
        tryCompare(dialog, "opened", true)
        dialog.close()
        tryCompare(dialog, "opened", false)
        tryCompare(page, "activeFocus", true)

        let closeCount = 0
        function countClose() { closeCount += 1 }
        page.closeRequested.connect(countClose)
        keyClick(Qt.Key_Escape)
        compare(closeCount, 1)
        page.closeRequested.disconnect(countClose)
    }

    function test_escapeClosesFullScreenAfterContextMenuCloses() {
        const page = findChild(preview, "fullScreenPage")
        const menu = findChild(page, "fullScreenContextMenu")
        verify(menu !== null)

        page.forceActiveFocus()
        menu.popup()
        tryCompare(menu, "opened", true)
        menu.close()
        tryCompare(menu, "opened", false)
        tryCompare(page, "activeFocus", true)

        let closeCount = 0
        function countClose() { closeCount += 1 }
        page.closeRequested.connect(countClose)
        keyClick(Qt.Key_Escape)
        compare(closeCount, 1)
        page.closeRequested.disconnect(countClose)
    }

    function test_propertiesOpenExplicitlyAndCardCanBeDragged() {
        const page = findChild(preview, "fullScreenPage")
        const dialog = findChild(page, "imagePropertiesDialog")
        const action = findChild(page, "fullScreenPropertiesAction")
        const header = findChild(dialog, "imagePropertiesDragHeader")
        verify(dialog !== null)
        verify(action !== null)
        verify(header !== null)

        dialog.close()
        action.triggered()
        tryCompare(dialog, "opened", true)
        const oldX = dialog.x
        const oldY = dialog.y
        mouseDrag(header, header.width / 2, header.height / 2, -100, 50,
                  Qt.LeftButton)
        verify(dialog.x < oldX)
        verify(dialog.y > oldY)
    }
}
