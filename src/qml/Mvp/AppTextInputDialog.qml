import QtQuick
import QtQuick.Controls
import "."

Dialog {
    id: root

    property string dialogTitle: qsTr("")
    property string description: qsTr("")
    property string initialText: ""
    property string acceptText: qsTr("Save")
    readonly property alias inputText: inputField.text

    signal submitted(string text)

    modal: true
    focus: true
    width: 380
    height: 224
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openWith(value) {
        initialText = value
        errorLabel.text = ""
        open()
    }

    function submit() {
        submitted(inputField.text)
    }

    function complete(errorMessage) {
        const error = String(errorMessage || "")
        if (error.length === 0)
            close()
        else
            errorLabel.text = error
    }

    onOpened: {
        inputField.text = initialText
        errorLabel.text = ""
        inputField.forceActiveFocus()
        inputField.selectAll()
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
            text: root.dialogTitle
            color: Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 17
            font.weight: Font.DemiBold
        }

        Text {
            x: 22
            y: 51
            width: parent.width - 44
            text: root.description
            color: Theme.mutedInk
            elide: Text.ElideRight
            font.family: Theme.uiFont
            font.pixelSize: 12
        }

        TextField {
            id: inputField
            x: 22
            y: 79
            width: parent.width - 44
            height: 38
            leftPadding: 11
            rightPadding: 11
            selectByMouse: true
            color: Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 13
            onAccepted: root.submit()
            onTextEdited: errorLabel.text = ""
            background: Rectangle {
                color: Theme.paperWhite
                radius: 6
                border.color: inputField.activeFocus ? Theme.probeBlue : Theme.opticalGray
                border.width: inputField.activeFocus ? 2 : 1
            }
        }

        Text {
            id: errorLabel
            x: 22
            y: 124
            width: parent.width - 44
            color: Theme.danger
            elide: Text.ElideRight
            font.family: Theme.uiFont
            font.pixelSize: 11
        }

        Button {
            id: cancelButton
            x: parent.width - 204
            y: 168
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
            id: acceptButton
            x: parent.width - 114
            y: 168
            width: 92
            height: 34
            text: root.acceptText
            enabled: inputField.text.trim().length > 0
            onClicked: root.submit()
            contentItem: Text {
                text: acceptButton.text
                color: acceptButton.enabled ? Theme.paperWhite : Theme.faintInk
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            background: Rectangle {
                radius: 6
                color: !acceptButton.enabled ? Theme.pressedSurface
                      : acceptButton.down ? Theme.primaryButton
                      : acceptButton.hovered ? Theme.primaryButtonHover : Theme.primaryButton
            }
        }
    }
}
