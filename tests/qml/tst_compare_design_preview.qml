import QtQuick
import QtCore
import QtTest
import "../../design"

TestCase {
    id: testCase
    name: "CompareDesignPreview"
    when: windowShown
    width: 1440
    height: 900
    visible: true

    ComparePreview {
        id: preview
        anchors.fill: parent
    }

    function test_productionComparePageLoadsWithDesignCanvas() {
        const page = findChild(preview, "comparePage")
        const canvas = findChild(preview, "designCompareCanvas")
        const saveButton = findChild(preview, "saveComparisonScreenshotButton")
        const saveDialog = findChild(preview, "comparisonScreenshotSaveDialog")
        verify(page !== null)
        verify(canvas !== null)
        verify(saveButton !== null)
        verify(saveDialog !== null)
        compare(page.paths.length, 2)
        compare(saveButton.toolTipText, "Save screenshot")
    }

    function test_smoothDisplayButtonFollowsAndChangesTheSharedSetting() {
        const page = findChild(preview, "comparePage")
        const canvas = findChild(preview, "designCompareCanvas")
        const button = findChild(preview, "compareSmoothDisplayButton")
        verify(page !== null)
        verify(canvas !== null)
        verify(button !== null)
        verify(button.checked)
        verify(canvas.smoothDisplay)

        mouseClick(button, button.width / 2, button.height / 2, Qt.LeftButton)
        compare(page.smoothDisplay, false)
        compare(button.checked, false)
        compare(canvas.smoothDisplay, false)

        page.setSmoothDisplay(true)
    }

    function test_screenshotButtonOpensSaveDialog() {
        const saveButton = findChild(preview, "saveComparisonScreenshotButton")
        const saveDialog = findChild(preview, "comparisonScreenshotSaveDialog")
        mouseClick(saveButton, saveButton.width / 2, saveButton.height / 2, Qt.LeftButton)
        tryCompare(saveDialog, "visible", true, 1000)
        saveDialog.close()
    }

    function test_comparisonStageCanBeSavedAsPng() {
        const page = findChild(preview, "comparePage")
        let finished = false
        let succeeded = false
        function recordResult(success, destination) {
            succeeded = success
            finished = true
        }
        page.screenshotFinished.connect(recordResult)
        const destination = StandardPaths.writableLocation(StandardPaths.TempLocation)
                + "/ispview_compare_screenshot_test.png"
        page.captureScreenshot(destination)
        tryVerify(function() { return finished }, 3000)
        verify(succeeded)
        compare(page.transientMessage, "Screenshot saved")
        page.screenshotFinished.disconnect(recordResult)
    }

    function test_escapeIsTheOnlyComparisonPageExitControl() {
        const page = findChild(preview, "comparePage")
        const closeButton = findChild(preview, "closeComparisonButton")
        compare(closeButton, null)
        let closeCount = 0
        function countClose() { closeCount += 1 }
        page.closeRequested.connect(countClose)
        page.forceActiveFocus()
        keyClick(Qt.Key_Escape)
        compare(closeCount, 1)
        page.closeRequested.disconnect(countClose)
    }
}
