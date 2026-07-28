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
    width: 390
    height: completedWithError ? 248 : 218
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function showConfirmation() {
        completedWithError = false
        errorLabel.text = ""
        open()
    }

    function confirmAction() {
        if (completedWithError) {
            close()
            return
        }
        confirmed()
    }

    function complete(errorMessage) {
        const error = String(errorMessage || "")
        if (error.length === 0) {
            close()
        } else {
            errorLabel.text = error
            completedWithError = true
        }
    }

    Overlay.modal: Rectangle {
        color: "#330E1820"
    }

    background: Rectangle {
        color: Theme.paperWhite
        radius: 10
        border.color: Theme.opticalGray
        border.width: 1
    }

    contentItem: Item {
        Text {
            x: 22
            y: 20
            text: root.completedWithError ? "Operation incomplete" : root.dialogTitle
            color: Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 17
            font.weight: Font.DemiBold
        }

        Text {
            x: 22
            y: 56
            width: parent.width - 44
            text: root.completedWithError ? errorLabel.text : root.message
            color: root.completedWithError ? Theme.danger : Theme.mutedInk
            wrapMode: Text.Wrap
            font.family: Theme.uiFont
            font.pixelSize: 12
            lineHeight: 1.25
        }

        Text {
            id: errorLabel
            visible: false
        }

        Button {
            id: cancelButton
            visible: !root.completedWithError
            x: parent.width - 204
            y: parent.height - 50
            width: 82
            height: 34
            text: qsTr("Cancel")
            onClicked: root.close()
            contentItem: Text {
                text: cancelButton.text
                color: Theme.graphiteInk
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.uiFont
                font.pixelSize: 13
            }
            background: Rectangle {
                radius: 6
                color: cancelButton.down ? Theme.pressedSurface : cancelButton.hovered ? Theme.softHover : Theme.paperWhite
                border.color: Theme.opticalGray
                border.width: 1
            }
        }

        Button {
            id: confirmButton
            x: parent.width - 114
            y: parent.height - 50
            width: 92
            height: 34
            text: root.completedWithError ? "Close" : root.confirmText
            onClicked: root.confirmAction()
            contentItem: Text {
                text: confirmButton.text
                color: Theme.primaryButtonText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            background: Rectangle {
                radius: 6
                color: root.destructive && !root.completedWithError
                      ? (confirmButton.down ? "#9F3B3B" : confirmButton.hovered ? "#C44E4E" : Theme.danger)
                      : (confirmButton.down ? Theme.primaryButton : confirmButton.hovered ? Theme.primaryButtonHover : Theme.primaryButton)
            }
        }
    }
}
