import QtQuick
import QtQuick.Controls
import "."

Column {
    id: root
    property var settingsController: null
    property var diagnostics: null
    property bool diagnosticDismissed: false
    property bool updateDismissed: false
    readonly property bool hasDiagnostic: !diagnosticDismissed && diagnostics
                                          && diagnostics.previousExit.length > 0
    readonly property bool hasUpdate: !updateDismissed && settingsController
                                      && settingsController.updateState === "available"
    signal diagnosticsRequested()
    signal updatesRequested()
    spacing: 8

    Connections {
        target: root.settingsController
        function onUpdateStateChanged() {
            if (root.settingsController.updateState === "available") root.updateDismissed = false
        }
    }

    Rectangle {
        id: diagnosticNotice
        objectName: "diagnosticNotice"
        width: root.width
        height: diagnosticContent.implicitHeight + 28
        visible: root.hasDiagnostic
        radius: 6
        color: Theme.raisedSurface
        border.color: Theme.opticalGray
        Column {
            id: diagnosticContent
            x: 14; y: 14
            width: parent.width - 28
            spacing: 8
            Text {
                objectName: "diagnosticNoticeTitle"
                width: parent.width - 28
                text: qsTr("Diagnostic report available")
                wrapMode: Text.Wrap
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: root.diagnostics && root.diagnostics.previousExit === "crash"
                    ? qsTr("A crash report from the previous session is available.")
                    : qsTr("The previous session ended unexpectedly. Diagnostic information may be available.")
                wrapMode: Text.Wrap
                color: Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: Theme.captionFontSize
                lineHeight: Theme.descriptionLineHeight
            }
            SettingsButton {
                objectName: "noticeDiagnosticsButton"
                text: qsTr("Diagnostics / Export")
                onClicked: { root.diagnosticDismissed = true; root.diagnosticsRequested() }
            }
        }
        AppIconButton {
            objectName: "dismissDiagnosticNotice"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 8
            controlSize: 24
            renderedIconSize: 14
            iconSource: Theme.iconPrefix + "close.svg"
            toolTipText: qsTr("Dismiss")
            onClicked: root.diagnosticDismissed = true
        }
    }

    Rectangle {
        objectName: "updateNotice"
        width: root.width
        height: updateContent.implicitHeight + 28
        visible: root.hasUpdate
        radius: 6
        color: Theme.raisedSurface
        border.color: Theme.opticalGray
        Column {
            id: updateContent
            x: 14; y: 14
            width: parent.width - 28
            spacing: 8
            Text {
                objectName: "updateNoticeTitle"
                width: parent.width - 28
                text: qsTr("Update available")
                wrapMode: Text.Wrap
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: qsTr("Version %1 is available.").arg(root.settingsController ? root.settingsController.latestVersion : "")
                wrapMode: Text.Wrap
                color: Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: Theme.captionFontSize
                lineHeight: Theme.descriptionLineHeight
            }
            SettingsButton {
                objectName: "noticeUpdateButton"
                text: qsTr("View update")
                onClicked: { root.updateDismissed = true; root.updatesRequested() }
            }
        }
        AppIconButton {
            objectName: "dismissUpdateNotice"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 8
            controlSize: 24
            renderedIconSize: 14
            iconSource: Theme.iconPrefix + "close.svg"
            toolTipText: qsTr("Dismiss")
            onClicked: root.updateDismissed = true
        }
    }
}
