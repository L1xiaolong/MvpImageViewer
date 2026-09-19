import QtQuick
import QtCore
import QtTest
import "../../design"
import "../../src/qml/Isp"

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

    function test_imageInformationMatchesFullScreenHud() {
        const first = findChild(preview, "compareFileInformation_0")
        const second = findChild(preview, "compareFileInformation_1")
        const firstExif = findChild(preview, "compareExifInformation_0")
        const firstPixel = findChild(preview, "comparePixelValue_0")
        const histogram = findChild(preview, "compareLumaHistogram_0")
        verify(first !== null)
        verify(second !== null)
        verify(firstExif !== null)
        verify(firstPixel !== null)
        verify(histogram !== null)
        compare(first.text, "sample_0001.jpg,4000*3000,13.9MB,84%")
        compare(second.text, "sample_0002.jpg,6000*4000,18.2MB,84%")
        compare(first.color, "#00ff00")
        compare(second.color, "#00ff00")
        compare(first.font.family, Theme.monoFont)
        compare(first.font.pixelSize, 12)
        compare(first.font.weight, Font.Bold)
        compare(firstExif.color, "#00ff00")
        compare(firstExif.font.family, Theme.monoFont)
        compare(firstExif.font.pixelSize, 12)
        compare(firstExif.font.weight, Font.Bold)
        compare(firstPixel.color, "#00ff00")
        compare(firstPixel.font.family, Theme.monoFont)
        compare(firstPixel.font.pixelSize, 12)
        compare(firstPixel.font.weight, Font.Bold)
        compare(histogram.color, "#ffffff")
        compare(histogram.plotColor, "#000000")
    }

    function test_candidateCoverAlsoCoversHistogram() {
        const page = findChild(preview, "comparePage")
        const firstHistogram = findChild(preview, "compareLumaHistogram_0")
        const secondHistogram = findChild(preview, "compareLumaHistogram_1")
        verify(page !== null)
        verify(firstHistogram !== null)
        verify(secondHistogram !== null)
        compare(firstHistogram.slot, 0)
        compare(secondHistogram.slot, 1)

        page.controller.setHoldCandidate(true)
        tryCompare(firstHistogram, "slot", 1)
        compare(secondHistogram.slot, 1)

        page.controller.setHoldCandidate(false)
        tryCompare(firstHistogram, "slot", 0)
        compare(secondHistogram.slot, 1)
    }

    function test_thumbnailButtonControlsNavigationOverlays() {
        const button = findChild(preview, "compareThumbnailButton")
        const firstThumbnail = findChild(preview, "compareNavigationOverlay_0")
        const secondThumbnail = findChild(preview, "compareNavigationOverlay_1")
        verify(button !== null)
        verify(firstThumbnail !== null)
        verify(secondThumbnail !== null)
        compare(button.toolTipText, "Thumbnail")
        verify(button.iconSource.toString().endsWith("thumbnail-preview.svg"))
        verify(button.checked)
        verify(firstThumbnail.visible)
        verify(secondThumbnail.visible)

        mouseClick(button, button.width / 2, button.height / 2, Qt.LeftButton)
        verify(!button.checked)
        verify(!firstThumbnail.visible)
        verify(!secondThumbnail.visible)

        mouseClick(button, button.width / 2, button.height / 2, Qt.LeftButton)
        verify(button.checked)
        verify(firstThumbnail.visible)
        verify(secondThumbnail.visible)
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
