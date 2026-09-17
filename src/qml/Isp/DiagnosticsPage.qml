import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import "."

Column {
    id: root
    property var settingsController: null
    property var diagnostics: null
    spacing: 8
    Text { text: qsTr("Logs & diagnostics"); color: Theme.graphiteInk; font.pixelSize: 22; font.bold: true }
    Text {
        width: parent.width; wrapMode: Text.Wrap; color: Theme.mutedInk
        text: qsTr("Diagnostics stay on this device. Nothing is uploaded automatically.")
    }
    CheckBox {
        objectName: "loggingSwitch"
        text: qsTr("Record application logs")
        checked: root.settingsController ? root.settingsController.loggingEnabled : true
        onClicked: if (root.settingsController) root.settingsController.loggingEnabled = checked
    }
    Row {
        spacing: 12
        Label { text: qsTr("Minimum log level"); color: Theme.graphiteInk; anchors.verticalCenter: parent.verticalCenter }
        ComboBox {
            model: ["Debug", "Info", "Warning", "Error", "Fatal"]
            currentIndex: root.settingsController ? model.indexOf(root.settingsController.logLevel) : 1
            enabled: root.settingsController && root.settingsController.loggingEnabled
            onActivated: if (root.settingsController) root.settingsController.logLevel = currentText
        }
    }
    CheckBox {
        objectName: "crashSwitch"
        text: qsTr("Record crash reports (takes effect after restart)")
        checked: root.settingsController ? root.settingsController.crashReportingEnabled : true
        onClicked: if (root.settingsController) root.settingsController.crashReportingEnabled = checked
    }
    Text {
        width: parent.width; wrapMode: Text.Wrap; color: Theme.mutedInk
        text: !root.diagnostics ? qsTr("Diagnostics unavailable")
              : root.diagnostics.status === "active" ? qsTr("Crash capture is active for this session.")
              : root.diagnostics.status === "disabled" ? qsTr("Crash capture is disabled for this session.")
              : qsTr("Crash capture is unavailable. See the error below.")
    }
    Text {
        visible: root.diagnostics && root.settingsController
                 && root.diagnostics.crashActive !== root.settingsController.crashReportingEnabled
        text: qsTr("Restart the application to apply the crash recording change.")
        width: parent.width; wrapMode: Text.Wrap; color: Theme.exposureAmber
    }
    Text {
        width: parent.width; wrapMode: Text.Wrap; color: Theme.mutedInk
        text: root.diagnostics ? qsTr("Storage: %1 MiB\n%2").arg((root.diagnostics.diskUsage / 1048576).toFixed(1)).arg(root.diagnostics.directory) : ""
    }
    Text {
        width: parent.width; wrapMode: Text.Wrap; color: Theme.mutedInk
        text: root.diagnostics ? root.diagnostics.retentionSummary : qsTr("Diagnostics unavailable")
    }
    Text {
        width: parent.width; wrapMode: Text.Wrap; color: Theme.mutedInk
        text: root.diagnostics && root.diagnostics.recentReports.length > 0
              ? qsTr("Recent crash reports: %1. Latest: %2").arg(root.diagnostics.recentReports.length).arg(root.diagnostics.recentReports[0].captured)
              : qsTr("No retained crash reports.")
    }
    Row {
        spacing: 8
        Button { text: qsTr("Open folder"); enabled: !!root.diagnostics; onClicked: root.diagnostics.openDirectory() }
        Button { text: qsTr("Clear history"); enabled: root.diagnostics && !root.diagnostics.busy; onClicked: clearDialog.showConfirmation() }
    }
    Row {
        spacing: 12
        Label { text: qsTr("Export range"); color: Theme.graphiteInk; anchors.verticalCenter: parent.verticalCenter }
        ComboBox { id: range; model: [qsTr("Last 24 hours"), qsTr("Last 7 days"), qsTr("Last 30 days"), qsTr("All retained")]; currentIndex: 1 }
    }
    CheckBox { id: dumps; objectName: "includeDumpsSwitch"; text: qsTr("Include crash dumps"); checked: false }
    Text {
        visible: dumps.checked; width: parent.width; wrapMode: Text.Wrap; color: Theme.exposureAmber
        text: qsTr("Crash dumps may contain file paths and memory fragments. Include them only when you intend to share this information.")
    }
    Row {
        spacing: 8
        Button { text: qsTr("Export ZIP"); enabled: root.diagnostics && !root.diagnostics.busy; onClicked: exportDialog.open() }
        Button { text: qsTr("Cancel export"); visible: root.diagnostics && root.diagnostics.busy; onClicked: root.diagnostics.cancelExport() }
    }
    ProgressBar { width: parent.width; visible: root.diagnostics && root.diagnostics.busy; from: 0; to: 100; value: root.diagnostics ? root.diagnostics.progress : 0 }
    Text { width: parent.width; wrapMode: Text.Wrap; color: Theme.danger; text: root.diagnostics ? root.diagnostics.error : ""; visible: text.length > 0 }
    Text { width: parent.width; wrapMode: Text.Wrap; color: Theme.mutedInk; text: root.diagnostics ? root.diagnostics.exportedPath : ""; visible: text.length > 0 }
    Button { text: qsTr("Show exported file"); visible: root.diagnostics && root.diagnostics.exportedPath.length > 0; onClicked: root.diagnostics.openExportDirectory() }
    FileDialog {
        id: exportDialog
        title: qsTr("Export diagnostics")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("ZIP archives (*.zip)")]
        defaultSuffix: "zip"
        onAccepted: root.diagnostics.exportLogs(selectedFile, [1, 7, 30, 0][range.currentIndex], dumps.checked)
    }
    AppConfirmDialog {
        id: clearDialog
        parent: Overlay.overlay
        dialogTitle: qsTr("Clear diagnostic history?")
        message: qsTr("This removes completed session logs and crash reports. Running sessions are kept.")
        destructive: true
        onConfirmed: clearDialog.complete(root.diagnostics.clearHistory())
    }
}
