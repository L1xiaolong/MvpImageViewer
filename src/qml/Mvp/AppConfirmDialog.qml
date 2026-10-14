import QtQuick
import QtQuick.Controls
import "."

Dialog {
    id: root
    property string dialogTitle: qsTr("")
    property string message: qsTr("")
    property string confirmText: qsTr("Confirm")
    property bool destructive: false
    property bool completedWithError: false
    signal confirmed()

    modal: true
    focus: true
    width: Math.min(440, parent ? parent.width - 48 : 440)
    height: dialogContent.implicitHeight + 40
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 20
    closePolicy: Popup.CloseOnEscape
    function showConfirmation() {
        completedWithError = false
        errorLabel.text = ""
        open()
    }
    function confirmAction() {
        if (completedWithError) close()
        else confirmed()
    }
    function complete(errorMessage) {
        const error = String(errorMessage || "")
        if (error.length === 0) close()
        else {
            errorLabel.text = error
            completedWithError = true
        }
    }
    Overlay.modal: Rectangle { color: Theme.overlay }
    background: Rectangle { color: Theme.paperWhite; radius: 8; border.color: Theme.opticalGray }
    contentItem: Column {
        id: dialogContent
        spacing: 16
        Text {
            width: parent.width
            text: root.completedWithError ? qsTr("Operation incomplete") : root.dialogTitle
            color: Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 18
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            text: root.completedWithError ? errorLabel.text : root.message
            color: root.completedWithError ? Theme.danger : Theme.mutedInk
            wrapMode: Text.Wrap
            font.family: Theme.uiFont
            font.pixelSize: 12
            lineHeight: 1.25
        }
        Text { id: errorLabel; visible: false }
        Flow {
            width: parent.width
            layoutDirection: Qt.RightToLeft
            spacing: 8
            SettingsButton {
                text: root.completedWithError ? qsTr("Close") : root.confirmText
                primary: !root.destructive || root.completedWithError
                destructive: root.destructive && !root.completedWithError
                onClicked: root.confirmAction()
            }
            SettingsButton { visible: !root.completedWithError; text: qsTr("Cancel"); onClicked: root.close() }
        }
    }
}
