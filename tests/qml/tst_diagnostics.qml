import QtQuick
import QtTest
import "../../src/qml/Isp"

TestCase {
    id: root
    name: "DiagnosticsSettings"
    when: windowShown
    width: 600
    height: 900
    visible: true
    QtObject {
        id: settings
        property bool loggingEnabled: true
        property string logLevel: "Info"
        property bool crashReportingEnabled: true
    }
    QtObject {
        id: backend
        property string status: "active"
        property bool crashActive: true
        property real diskUsage: 1024
        property string directory: "test"
        property var recentReports: []
        property bool busy: false
        property int progress: 0
        property string error: ""
        property string exportedPath: ""
        property string retentionSummary: "test policy"
    }
    DiagnosticsPage { id: page; width: 540; settingsController: settings; diagnostics: backend }
    function test_independentSwitchesAndPrivateExportDefault() {
        const logging = findChild(page, "loggingSwitch")
        const crash = findChild(page, "crashSwitch")
        const dumps = findChild(page, "includeDumpsSwitch")
        compare(dumps.checked, false)
        mouseClick(logging)
        compare(settings.loggingEnabled, false)
        compare(settings.crashReportingEnabled, true)
        mouseClick(crash)
        compare(settings.crashReportingEnabled, false)
        compare(backend.crashActive, true)
        mouseClick(logging)
        compare(settings.loggingEnabled, true)
        compare(settings.crashReportingEnabled, false)
    }
}
