import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import "."

Column {
    id: root
    property var settingsController: null
    property var diagnostics: null
    readonly property bool busy: diagnostics ? diagnostics.busy : false
    readonly property bool needsRestart: diagnostics && settingsController
                                            && ["active", "disabled"].indexOf(diagnostics.status) >= 0
                                            && diagnostics.crashActive !== settingsController.crashReportingEnabled
    property bool showStoragePath: false
    property int exportRange: 1
    property bool includeDumps: false

    component Description: Text {
        width: parent ? parent.width : 400
        wrapMode: Text.Wrap
        color: Theme.mutedInk
        font.family: Theme.uiFont
        font.pixelSize: 12
        lineHeight: 1.25
    }
    SettingsHeading { text: qsTr("Recording") }
    SettingsRow {
        title: qsTr("Record application logs")
        description: qsTr("Keep local logs to help investigate problems.")
        controlWidth: 40
        SettingsToggle {
            objectName: "loggingSwitch"
            text: qsTr("Record application logs")
            checked: root.settingsController ? root.settingsController.loggingEnabled : true
            onToggled: if (root.settingsController) root.settingsController.loggingEnabled = checked
        }
    }
    SettingsRow {
        title: qsTr("Minimum log level")
        description: root.settingsController && !root.settingsController.loggingEnabled ? qsTr("Enable application logs to change this setting.") : qsTr("Record events at this level and above.")
        SettingsComboBox {
            width: parent.width
            model: ["Debug", "Info", "Warning", "Error", "Fatal"]
            currentIndex: root.settingsController ? model.indexOf(root.settingsController.logLevel) : 1
            enabled: root.settingsController && root.settingsController.loggingEnabled
            onActivated: root.settingsController.logLevel = currentText
        }
    }
    SettingsRow {
        title: qsTr("Record crash reports")
        description: qsTr("Changes take effect after restart.")
        controlWidth: 40
        SettingsToggle {
            objectName: "crashSwitch"
            text: qsTr("Record crash reports")
            checked: root.settingsController ? root.settingsController.crashReportingEnabled : true
            onToggled: if (root.settingsController) root.settingsController.crashReportingEnabled = checked
        }
    }
    Item { width: 1; height: 12 }
    SettingsNotice {
        objectName: "diagnosticsSessionStatus"
        text: !root.diagnostics ? qsTr("Diagnostics unavailable")
              : root.diagnostics.status === "active" ? qsTr("Crash capture is active for this session.")
              : root.diagnostics.status === "disabled" ? qsTr("Crash capture is disabled for this session.")
              : root.diagnostics.error.length > 0 ? qsTr("Crash capture is unavailable: %1").arg(root.diagnostics.error)
              : qsTr("Crash capture is unavailable in this build or could not start. Application logs can still be recorded.")
    }
    Item { width: 1; height: root.needsRestart ? 8 : 0 }
    SettingsNotice {
        objectName: "diagnosticsRestartNotice"
        text: root.needsRestart ? qsTr("Restart the application to apply the crash recording change.") : ""
    }
    SettingsHeading { text: qsTr("Local storage") }
    Description {
        text: root.diagnostics ? qsTr("Storage used: %1 MiB").arg((root.diagnostics.diskUsage / 1048576).toFixed(1)) : qsTr("Diagnostics unavailable")
        color: Theme.graphiteInk
    }
    Item { width: 1; height: 8 }
    Description { text: root.diagnostics ? root.diagnostics.retentionSummary : "" }
    Item { width: 1; height: 8 }
    Description {
        text: root.diagnostics && root.diagnostics.recentReports.length > 0
              ? qsTr("Recent crash reports: %1. Latest: %2").arg(root.diagnostics.recentReports.length).arg(root.diagnostics.recentReports[0].captured)
              : qsTr("No retained crash reports.")
    }
    Item { width: 1; height: 12 }
    Flow {
        width: parent.width
        spacing: 8
        SettingsButton { text: qsTr("Open folder"); enabled: !!root.diagnostics; onClicked: root.diagnostics.openDirectory() }
        SettingsButton { text: (root.showStoragePath ? "▾ " : "▸ ") + qsTr("Storage location"); enabled: !!root.diagnostics; onClicked: root.showStoragePath = !root.showStoragePath }
        SettingsButton { text: qsTr("Clear history…"); enabled: root.diagnostics && !root.busy; onClicked: clearDialog.showConfirmation() }
    }
    Item { width: 1; height: root.showStoragePath ? 8 : 0 }
    Description { visible: root.showStoragePath; text: root.diagnostics ? root.diagnostics.directory : ""; font.family: Theme.monoFont; wrapMode: Text.WrapAnywhere }
    SettingsHeading { text: qsTr("Export diagnostics") }
    SettingsRow {
        title: qsTr("Export range")
        SettingsComboBox {
            width: parent.width
            model: [qsTr("Last 24 hours"), qsTr("Last 7 days"), qsTr("Last 30 days"), qsTr("All retained")]
            currentIndex: root.exportRange
            enabled: !root.busy
            onActivated: root.exportRange = currentIndex
        }
    }
    SettingsRow {
        title: qsTr("Include crash dumps")
        description: qsTr("Off by default. Dumps may contain file paths and memory fragments.")
        controlWidth: 40
        SettingsToggle {
            objectName: "includeDumpsSwitch"
            text: qsTr("Include crash dumps")
            checked: root.includeDumps
            enabled: !root.busy
            onToggled: root.includeDumps = checked
        }
    }
    Item { width: 1; height: root.includeDumps ? 12 : 0 }
    SettingsNotice { text: root.includeDumps ? qsTr("Crash dumps may contain file paths and memory fragments. Include them only when you intend to share this information.") : "" }
    Item { width: 1; height: 12 }
    Flow {
        width: parent.width
        spacing: 8
        SettingsButton { text: qsTr("Export ZIP"); primary: true; enabled: root.diagnostics && !root.busy; onClicked: exportDialog.open() }
        SettingsButton { text: qsTr("Cancel export"); visible: root.busy; onClicked: root.diagnostics.cancelExport() }
        SettingsButton { text: qsTr("Show exported file"); visible: root.diagnostics && root.diagnostics.exportedPath.length > 0; onClicked: root.diagnostics.openExportDirectory() }
    }
    Item { width: 1; height: root.busy ? 12 : 0 }
    ProgressBar { width: parent.width; visible: root.busy; from: 0; to: 100; value: root.diagnostics ? root.diagnostics.progress : 0 }
    Item { width: 1; height: 12 }
    SettingsNotice { text: root.diagnostics ? root.diagnostics.error : ""; severity: "error" }
    Description { visible: root.diagnostics && root.diagnostics.exportedPath.length > 0; text: root.diagnostics ? root.diagnostics.exportedPath : ""; font.family: Theme.monoFont; wrapMode: Text.WrapAnywhere }
    SettingsHeading { text: qsTr("Reset local data") }
    Description { text: qsTr("Delete thumbnails, update downloads, logs, crash reports, presets, history, and all settings stored by this app on this device. Your image files are not changed.") }
    Item { width: 1; height: 12 }
    SettingsButton {
        objectName: "deleteSoftwareCacheButton"
        text: qsTr("Delete all local app data…")
        destructive: true
        enabled: root.settingsController && !root.busy
        onClicked: deleteSoftwareCacheDialog.showConfirmation()
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export diagnostics")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("ZIP archives (*.zip)")]
        defaultSuffix: "zip"
        onAccepted: if (root.diagnostics && !root.busy) root.diagnostics.exportLogs(selectedFile, [1, 7, 30, 0][root.exportRange], root.includeDumps)
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
        objectName: "deleteLocalDataDialog"
        parent: Overlay.overlay
        dialogTitle: qsTr("Delete all local app data?")
        message: qsTr("MVP Image Viewer will close. All settings, presets, history, logs, crash reports, and cached files on this device will be deleted. Your image files will not be changed.")
        confirmText: qsTr("Delete and quit")
        destructive: true
        onConfirmed: deleteSoftwareCacheDialog.complete(root.settingsController.deleteSoftwareCache())
    }
}
