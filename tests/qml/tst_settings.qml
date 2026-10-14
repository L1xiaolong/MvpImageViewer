import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import "../../design" as Design
import "../../src/qml/Mvp"

Item {
    id: scene
    width: 980
    height: 640
    Design.SettingsPreview {
        id: preview
        anchors.fill: parent
    }
    TestCase {
        name: "SettingsUI"
        when: windowShown
        function init() {
            scene.width = 980; scene.height = 640
            scene.Window.window.width = 980; scene.Window.window.height = 640
            preview.settingsController = testSettings
            testSettings.restoreDefaults()
            testSettings.language = "zh_CN"
            testSettings.theme = "light"
            preview.darkMode = false
            preview.mockDiagnostics.status = "active"
            preview.mockDiagnostics.crashActive = true
            preview.mockDiagnostics.busy = false
            preview.mockDiagnostics.error = ""
            preview.mockDiagnostics.exportedPath = ""
            preview.card.currentSection = 0
            preview.card.open()
            tryCompare(preview.card, "opened", true)
            wait(30)
        }
        function cleanup() {
            preview.card.close()
            wait(200)
        }
        function page(section) {
            preview.card.selectSection(section)
            wait(150)
            verify(findChild(preview.card, "settingsPageLoader").item !== null)
        }
        function shot(name, fullPage) {
            const scroll = findChild(preview.card, "settingsScroll")
            const oldHeight = preview.card.height
            if (fullPage) {
                preview.card.height = oldHeight + Math.max(0, scroll.contentHeight - scroll.height)
                wait(30)
            }
            wait(150)
            verify(testCapture.save(preview.card.contentItem.parent, testArtifactDir + "/" + name + ".png"))
            preview.card.height = Qt.binding(function() { return Math.min(620, preview.height - 48) })
            wait(20)
        }
        function test_01_pages_and_themes() {
            const labels = ["general", "display", "shortcuts", "updates", "diagnostics"]
            for (let dark of [false, true]) {
                preview.darkMode = dark
                testSettings.theme = dark ? "dark" : "light"
                for (let i = 0; i < 5; i++) {
                    page(i)
                    const scroll = findChild(preview.card, "settingsScroll")
                    compare(scroll.contentWidth, scroll.width)
                    shot((dark ? "dark-" : "light-") + labels[i], true)
                    // At the minimum supported window size the last control is reachable.
                    scroll.contentY = Math.max(0, scroll.contentHeight - scroll.height)
                    verify(scroll.contentY >= 0)
                    scroll.contentY = 0
                }
            }
            scene.width = 1440; scene.height = 900
            scene.Window.window.width = 1440; scene.Window.window.height = 900
            page(1)
            compare(preview.card.width, 860)
            compare(preview.card.height, 620)
            shot("workspace-1440-display", false)
        }
        function test_02_live_settings_and_language() {
            const toggle = findChild(preview.card, "settingsRestoreDirectory")
            mouseClick(toggle)
            compare(testSettings.restoreLastDirectory, false)
            compare(testCapture.storedSetting("general/restoreLastDirectory"), false)
            page(1); page(0)
            compare(findChild(preview.card, "settingsRestoreDirectory").checked, false)
            testSettings.language = "en"
            wait(20)
            compare(findChild(preview.card, "settingsCloseButton").text, "Close")
            testSettings.language = "zh_CN"
            wait(20)
            compare(findChild(preview.card, "settingsCloseButton").text, "关闭")
            page(1)
            const smooth = findChild(preview.card, "settingsSmoothDisplay")
            const displayScroll = findChild(preview.card, "settingsScroll")
            displayScroll.contentY = displayScroll.contentHeight - displayScroll.height
            wait(30)
            mouseClick(smooth)
            compare(testSettings.smoothDisplay, false)
            testSettings.smoothDisplay = true
            compare(smooth.checked, true)
            testSettings.restoreDefaults()
            compare(smooth.checked, true)
            testSettings.canvasBackground = "black"
            mouseClick(findChild(preview.card, "settingsRestoreButton"))
            const restore = findChild(preview.card, "settingsRestoreDialog")
            tryCompare(restore, "opened", true)
            restore.confirmAction()
            compare(testSettings.canvasBackground, "neutral")
            compare(preview.card.currentSection, 1)
        }
        function test_03_scroll_and_close() {
            page(4)
            const scroll = findChild(preview.card, "settingsScroll")
            scroll.contentY = 140
            page(0); page(4)
            compare(scroll.contentY, 140)
            preview.card.close()
            wait(200)
            preview.card.currentSection = 0
            preview.card.open()
            tryCompare(preview.card, "opened", true)
            compare(scroll.contentY, 0)
            // The backdrop cannot dismiss the modal.
            mouseClick(preview, 4, 4)
            compare(preview.card.visible, true)
            preview.card.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            tryCompare(preview.card, "visible", false)
        }
        function test_04_shortcut_capture() {
            page(2)
            const edit = testCapture.findItem(preview.card.contentItem, "editShortcut-openFolder")
            verify(edit !== null, "Shortcut change button must exist")
            mouseClick(edit)
            const recorder = findChild(preview.card, "shortcutRecorder")
            tryCompare(recorder, "opened", true)
            const capture = findChild(preview.card, "shortcutCaptureArea")
            capture.forceActiveFocus()
            keyClick(Qt.Key_Shift)
            compare(preview.card.candidateSequence, "")
            keyClick(Qt.Key_F, Qt.ControlModifier)
            compare(preview.card.candidateSequence, "Ctrl+F")
            mouseClick(findChild(preview.card, "saveShortcutButton"))
            verify(preview.card.shortcutError.length > 0)
            compare(testSettings.shortcutFor("openFolder"), "Ctrl+O")
            verify(testCapture.save(recorder.contentItem.parent, testArtifactDir + "/state-shortcut-conflict.png"))
            capture.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            tryCompare(recorder, "visible", false)
            compare(preview.card.visible, true)
            verify(edit !== null, "Shortcut change button must exist")
            mouseClick(edit)
            tryCompare(recorder, "opened", true)
            capture.forceActiveFocus()
            keyClick(Qt.Key_K, Qt.ControlModifier | Qt.ShiftModifier)
            mouseClick(findChild(preview.card, "saveShortcutButton"))
            tryCompare(recorder, "visible", false)
            compare(testSettings.shortcutFor("openFolder"), "Ctrl+Shift+K")
            compare(testCapture.storedSetting("shortcuts/openFolder"), "Ctrl+Shift+K")
            // Conversion reserves Esc and ignores modifier-only events.
            compare(testSettings.shortcutSequenceForKey(Qt.Key_Control, 0).sequence, undefined)
            compare(testSettings.shortcutSequenceForKey(Qt.Key_Escape, 0).sequence, undefined)
        }
        function test_05_display_dropdowns_and_project_link() {
            compare(preview.card.sections.length, 5)
            compare(preview.card.sections[1].label, "显示")
            page(1)
            const theme = findChild(preview.card, "settingsTheme")
            mouseClick(theme)
            keyClick(Qt.Key_End)
            keyClick(Qt.Key_Return)
            compare(testSettings.theme, "dark")
            compare(testCapture.storedSetting("appearance/theme"), "dark")
            const background = findChild(preview.card, "settingsCanvasBackground")
            mouseClick(background)
            keyClick(Qt.Key_End)
            keyClick(Qt.Key_Return)
            compare(testSettings.canvasBackground, "white")
            page(0); page(1)
            compare(findChild(preview.card, "settingsTheme").currentIndex, 2)
            compare(findChild(preview.card, "settingsCanvasBackground").currentIndex, 3)
            mouseClick(findChild(preview.card, "settingsProjectLink"))
            verify(testCapture.lastOpenedUrl.indexOf("https://github.com/") === 0)
            verify(testCapture.lastOpenedUrl.indexOf("#") < 0)
            compare(findChild(preview.card, "settingsMoreButton"), null)
        }
        function test_06_update_and_diagnostic_states() {
            preview.settingsController = preview.mockSettings
            preview.mockSettings.updateState = "downloading"
            page(3)
            const progress = findChild(preview.card, "settingsUpdateProgress")
            compare(progress.visible, true)
            compare(progress.value, 42)
            compare(findChild(preview.card, "checkUpdateButton").enabled, false)
            shot("state-update-downloading", false)
            preview.mockSettings.updateState = "verifying"
            compare(progress.indeterminate, true)
            compare(findChild(preview.card, "downloadUpdateButton").visible, false)
            preview.mockSettings.updateState = "ready"
            compare(findChild(preview.card, "downloadUpdateButton").visible, true)
            preview.mockSettings.updateState = "error"
            preview.mockSettings.updateError = "Connection timed out. Please try again."
            verify(findChild(preview.card, "settingsUpdateStatus").text.indexOf("Connection timed out") >= 0)
            preview.mockSettings.crashReportingEnabled = false
            page(4)
            verify(findChild(preview.card, "diagnosticsRestartNotice").visible)
            shot("state-diagnostics-restart", true)
            const button = findChild(preview.card, "deleteSoftwareCacheButton")
            preview.mockDiagnostics.busy = true
            compare(button.enabled, false)
            preview.mockDiagnostics.busy = false
            const scroll = findChild(preview.card, "settingsScroll")
            scroll.contentY = scroll.contentHeight - scroll.height
            wait(20)
            mouseClick(button)
            const confirmation = findChild(preview.card, "deleteLocalDataDialog")
            tryCompare(confirmation, "opened", true)
            // Only the mock is invoked; source images and real user data are never touched.
            confirmation.confirmAction()
            compare(preview.mockSettings.deleteCalls, 1)
        }
    }
}
