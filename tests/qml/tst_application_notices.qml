import QtQuick
import QtTest
import "../../src/qml/Mvp"

Rectangle {
    id: scene
    width: 980
    height: 640
    color: Theme.sensorWhite
    QtObject {
        id: updates
        property string updateState: "idle"
        property string latestVersion: "0.2.20"
    }
    QtObject { id: mockDiagnostics; property string previousExit: "" }
    ApplicationNotices {
        id: notices
        width: Math.min(380, parent.width - 32)
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 16
        anchors.bottomMargin: 48
        settingsController: updates
        diagnostics: mockDiagnostics
    }
    SignalSpy { id: updateAction; target: notices; signalName: "updatesRequested" }
    SignalSpy { id: diagnosticAction; target: notices; signalName: "diagnosticsRequested" }
    TestCase {
        name: "ApplicationNotices"
        when: windowShown
        function init() {
            testSettings.language = "en"
            Theme.darkMode = false
            updates.updateState = "idle"
            mockDiagnostics.previousExit = ""
            notices.updateDismissed = false
            notices.diagnosticDismissed = false
            updateAction.clear()
            diagnosticAction.clear()
        }
        function cleanup() { Theme.darkMode = false }
        function test_update_appears_only_when_available() {
            verify(!notices.hasUpdate)
            for (const state of ["checking", "latest", "error", "downloading", "ready"]) {
                updates.updateState = state
                verify(!notices.hasUpdate)
            }
            updates.updateState = "available"
            verify(notices.hasUpdate)
            waitForRendering(notices)
            mouseClick(findChild(notices, "dismissUpdateNotice"))
            verify(!notices.hasUpdate)
            updates.updateState = "checking"
            updates.updateState = "available"
            verify(notices.hasUpdate)
            waitForRendering(notices)
            mouseClick(findChild(notices, "noticeUpdateButton"))
            compare(updateAction.count, 1)
            verify(!notices.hasUpdate)
        }
        function test_diagnostics_action_and_dismiss() {
            mockDiagnostics.previousExit = "crash"
            verify(notices.hasDiagnostic)
            waitForRendering(notices)
            mouseClick(findChild(notices, "noticeDiagnosticsButton"))
            compare(diagnosticAction.count, 1)
            verify(!notices.hasDiagnostic)
        }
        function test_stacked_bottom_right_data() {
            return [{tag:"light", dark:false}, {tag:"dark", dark:true}]
        }
        function test_stacked_bottom_right(data) {
            Theme.darkMode = data.dark
            mockDiagnostics.previousExit = "crash"
            updates.updateState = "available"
            wait(200) // Let the existing button color transition finish in the new theme.
            waitForRendering(notices)
            const crash = findChild(notices, "diagnosticNotice")
            const update = findChild(notices, "updateNotice")
            compare(notices.x + notices.width, scene.width - 16)
            compare(notices.y + notices.height, scene.height - 48)
            verify(crash.y + crash.height + 8 <= update.y)
            verify(notices.y > 0)
            verify(testCapture.save(scene, testArtifactDir + "/notices-" + data.tag + ".png"))
            testSettings.language = "zh_CN"
            tryCompare(findChild(notices, "updateNoticeTitle"), "text", "发现新版本")
            tryCompare(findChild(notices, "diagnosticNoticeTitle"), "text", "诊断报告可用")
        }
    }
}
