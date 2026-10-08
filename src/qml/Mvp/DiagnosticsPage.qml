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

    Item { width: 1; height: 12 }
    Rectangle { width: parent.width; height: 1; color: Theme.opticalGray }
    Text {
        topPadding: 12
        text: qsTr("Cold start")
        color: Theme.graphiteInk
        font.family: Theme.uiFont
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }
    Text {
        width: parent.width
        wrapMode: Text.Wrap
        color: Theme.mutedInk
        font.family: Theme.uiFont
        font.pixelSize: 12
        lineHeight: 1.25
        text: qsTr("Delete thumbnails, update downloads, logs, crash reports, presets, history, and all settings stored by this app on this device. Your image files are not changed.")
    }
    Button {
        id: deleteSoftwareCacheButton
        objectName: "deleteSoftwareCacheButton"
        text: qsTr("Delete software cache…")
        enabled: !!root.settingsController && (!root.diagnostics || !root.diagnostics.busy)
        onClicked: deleteSoftwareCacheDialog.showConfirmation()
        contentItem: Text {
            text: deleteSoftwareCacheButton.text
            color: deleteSoftwareCacheButton.enabled ? Theme.danger : Theme.faintInk
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.uiFont
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        background: Rectangle {
            radius: 6
            color: deleteSoftwareCacheButton.down ? Theme.pressedSurface
                  : deleteSoftwareCacheButton.hovered ? Theme.softHover : Theme.raisedSurface
            border.width: deleteSoftwareCacheButton.activeFocus ? 2 : 1
            border.color: deleteSoftwareCacheButton.activeFocus ? Theme.probeBlue : Theme.danger
            opacity: deleteSoftwareCacheButton.enabled ? 1 : Theme.disabledOpacity
        }
    }
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
    AppConfirmDialog {
        id: deleteSoftwareCacheDialog
        parent: Overlay.overlay
        dialogTitle: qsTr("Delete all local app data?")
        message: qsTr("MVP Image Viewer will close. All settings, presets, history, logs, crash reports, and cached files on this device will be deleted. Your image files will not be changed.")
        confirmText: qsTr("Delete and quit")
        destructive: true
        onConfirmed: deleteSoftwareCacheDialog.complete(root.settingsController.deleteSoftwareCache())
    }
}
