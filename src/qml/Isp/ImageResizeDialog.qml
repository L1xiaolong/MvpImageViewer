import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

Dialog {
    id: root
    objectName: "imageResizeDialog"

    property var controller: null
    property bool percentageMode: false
    property bool keepAspectRatio: true
    property int sourceWidth: 0
    property int sourceHeight: 0
    property bool updatingFields: false
    property string errorText: ""

    modal: true
    focus: true
    width: 430
    height: 360
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openForSelection() {
        const size = controller ? controller.selectedImageSize : Qt.size(0, 0)
        sourceWidth = Number(size.width || 0)
        sourceHeight = Number(size.height || 0)
        percentageMode = false
        keepAspectRatio = true
        errorText = ""
        updatingFields = true
        widthField.text = sourceWidth > 0 ? String(sourceWidth) : ""
        heightField.text = sourceHeight > 0 ? String(sourceHeight) : ""
        updatingFields = false
        open()
        widthField.forceActiveFocus()
        widthField.selectAll()
    }

    function updateLinkedField(changedWidth) {
        if (updatingFields || !keepAspectRatio || sourceWidth <= 0 || sourceHeight <= 0)
            return
        updatingFields = true
        if (percentageMode) {
            if (changedWidth)
                heightField.text = widthField.text
            else
                widthField.text = heightField.text
        } else if (changedWidth) {
            const widthValue = Number(widthField.text)
            if (widthValue > 0)
                heightField.text = String(Math.max(1, Math.round(widthValue * sourceHeight / sourceWidth)))
        } else {
            const heightValue = Number(heightField.text)
            if (heightValue > 0)
                widthField.text = String(Math.max(1, Math.round(heightValue * sourceWidth / sourceHeight)))
        }
        updatingFields = false
    }

    function applyResize() {
        let targetWidth = Number(widthField.text)
        let targetHeight = Number(heightField.text)
        if (percentageMode) {
            targetWidth = Math.max(1, Math.round(sourceWidth * targetWidth / 100))
            targetHeight = Math.max(1, Math.round(sourceHeight * targetHeight / 100))
        }
        if (!Number.isFinite(targetWidth) || !Number.isFinite(targetHeight) ||
                targetWidth < 1 || targetHeight < 1) {
            errorText = "Enter positive numeric values."
            return
        }
        const error = controller.resizeSelected(Math.round(targetWidth), Math.round(targetHeight))
        if (error.length > 0) {
            errorText = error
            return
        }
        close()
    }

    Overlay.modal: Rectangle { color: "#330E1820" }
    background: Rectangle {
        radius: 10
        color: Theme.paperWhite
        border.width: 1
        border.color: Theme.opticalGray
    }

    contentItem: Item {
        Text {
            x: 22
            y: 18
            text: "Resize image"
            color: Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 17
            font.weight: Font.DemiBold
        }
        Text {
            x: 22
            y: 47
            text: sourceWidth > 0 ? "Original: " + sourceWidth + " × " + sourceHeight + " px"
                                  : "Image dimensions unavailable"
            color: Theme.mutedInk
            font.family: Theme.monoFont
            font.pixelSize: 10
        }

        Row {
            x: 22
            y: 82
            spacing: 6
            Button {
                id: pixelsButton
                width: 88
                height: 30
                text: "Pixels"
                checkable: true
                checked: !root.percentageMode
                onClicked: {
                    root.percentageMode = false
                    root.updatingFields = true
                    widthField.text = String(root.sourceWidth)
                    heightField.text = String(root.sourceHeight)
                    root.updatingFields = false
                }
            }
            Button {
                width: 88
                height: 30
                text: "Percent"
                checkable: true
                checked: root.percentageMode
                onClicked: {
                    root.percentageMode = true
                    root.updatingFields = true
                    widthField.text = "100"
                    heightField.text = "100"
                    root.updatingFields = false
                }
            }
        }

        GridLayout {
            x: 22
            y: 130
            width: parent.width - 44
            columns: 2
            columnSpacing: 14
            rowSpacing: 10

            Text {
                text: root.percentageMode ? "Horizontal (%)" : "Width (px)"
                color: Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: 11
            }
            TextField {
                id: widthField
                objectName: "resizeWidthField"
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: DoubleValidator { bottom: 0.01; top: 100000 }
                onTextEdited: root.updateLinkedField(true)
                onAccepted: root.applyResize()
            }
            Text {
                text: root.percentageMode ? "Vertical (%)" : "Height (px)"
                color: Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: 11
            }
            TextField {
                id: heightField
                objectName: "resizeHeightField"
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: DoubleValidator { bottom: 0.01; top: 100000 }
                onTextEdited: root.updateLinkedField(false)
                onAccepted: root.applyResize()
            }
        }

        CheckBox {
            x: 18
            y: 234
            text: "Keep aspect ratio"
            checked: root.keepAspectRatio
            onToggled: root.keepAspectRatio = checked
            font.family: Theme.uiFont
            font.pixelSize: 12
        }
        Text {
            x: 22
            y: 270
            width: parent.width - 44
            text: root.errorText.length > 0 ? root.errorText : "Interpolation: bicubic"
            color: root.errorText.length > 0 ? Theme.danger : Theme.mutedInk
            elide: Text.ElideRight
            font.family: Theme.uiFont
            font.pixelSize: 11
        }

        Button {
            x: parent.width - 204
            y: parent.height - 50
            width: 82
            height: 34
            text: "Cancel"
            onClicked: root.close()
        }
        Button {
            x: parent.width - 114
            y: parent.height - 50
            width: 92
            height: 34
            text: "Resize"
            enabled: root.sourceWidth > 0 && root.sourceHeight > 0
            onClicked: root.applyResize()
        }
    }
}
