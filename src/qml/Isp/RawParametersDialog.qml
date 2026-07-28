pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

Dialog {
    id: root
    objectName: "rawParametersDialog"

    property var controller: null
    property string iconPrefix: Theme.iconPrefix
    property bool saveAndApply: false
    property string noticeText: ""
    property bool noticeError: false
    property real dragOriginX: 0
    property real dragOriginY: 0

    readonly property var formatNames: ["NV12", "NV21", "I420", "P010",
        "MIPI RAW10", "MIPI RAW12", "RAW in 16-bit container"]
    readonly property var bayerNames: ["RGGB", "GRBG", "GBRG", "BGGR"]
    readonly property var matrixNames: ["BT.601", "BT.709", "BT.2020"]
    readonly property var rangeNames: ["Full", "Limited"]
    readonly property var orientationNames: ["Normal", "Rotate 90° clockwise",
        "Rotate 180°", "Rotate 270° clockwise"]

    modal: true
    focus: true
    width: parent ? Math.min(570, parent.width - 48) : 570
    height: parent ? Math.min(720, parent.height - 40) : 720
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openForPath(path) {
        noticeText = ""
        if (controller)
            controller.loadPath(path)
        open()
    }
    function value(key, fallback) {
        if (!controller || controller.values[key] === undefined)
            return fallback
        return controller.values[key]
    }
    function showNotice(message, error) {
        noticeText = message
        noticeError = error
        noticeTimer.restart()
    }

    Overlay.modal: Rectangle { color: "#330E1820" }
    background: Rectangle {
        radius: 10
        color: Theme.paperWhite
        border.width: 1
        border.color: Theme.opticalGray
    }

    Timer { id: noticeTimer; interval: 4000; onTriggered: root.noticeText = "" }

    Connections {
        target: root.controller
        function onNotificationRequested(message, error) { root.showNotice(message, error) }
    }

    contentItem: Item {
        Rectangle {
            id: header
            objectName: "rawParametersDragHeader"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 60
            color: "transparent"
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                property point pressPosition: Qt.point(0, 0)
                onPressed: function(mouse) {
                    root.dragOriginX = root.x
                    root.dragOriginY = root.y
                    pressPosition = mapToItem(root.parent, mouse.x, mouse.y)
                    mouse.accepted = true
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !root.parent)
                        return
                    const current = mapToItem(root.parent, mouse.x, mouse.y)
                    root.x = Math.max(0, Math.min(root.parent.width - root.width,
                                                 root.dragOriginX + current.x - pressPosition.x))
                    root.y = Math.max(0, Math.min(root.parent.height - root.height,
                                                 root.dragOriginY + current.y - pressPosition.y))
                }
            }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.top: parent.top
                anchors.topMargin: 10
                text: qsTr("RAW / YUV DECODER")
                color: Theme.probeBlue
                font.family: Theme.uiFont
                font.pixelSize: 8
                font.weight: Font.DemiBold
                font.letterSpacing: 1.2
            }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.right: closeButton.left
                anchors.rightMargin: 12
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 9
                text: root.controller ? root.controller.fileName : "Configuration"
                color: Theme.graphiteInk
                elide: Text.ElideMiddle
                font.family: Theme.uiFont
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            AppIconButton {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                controlSize: 28
                renderedIconSize: 14
                iconSource: root.iconPrefix + "close.svg"
                onClicked: root.close()
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.opticalGray
            }
        }

        ScrollView {
            id: scroll
            objectName: "rawParameterScroll"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.bottom: footer.top
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Column {
                width: scroll.availableWidth
                spacing: 10
                padding: 12

                Text {
                    width: parent.width - 24
                    text: qsTr("CONFIGURATION")
                    color: Theme.mutedInk
                    font.family: Theme.uiFont
                    font.pixelSize: 8
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.0
                }

                Row {
                    width: parent.width - 24
                    height: 28
                    spacing: 8
                    RawParameterComboBox {
                        id: presetCombo
                        width: parent.width - 106
                        model: ["<None>"].concat(root.controller ? root.controller.presetNames : [])
                        currentIndex: root.controller && root.controller.selectedPreset.length > 0
                                      ? Math.max(0, model.indexOf(root.controller.selectedPreset)) : 0
                        onActivated: root.controller.selectPreset(index > 0 ? currentText : "")
                    }
                    Button {
                        id: deletePresetButton
                        width: 98
                        height: 28
                        text: qsTr("Delete preset")
                        enabled: root.controller && root.controller.selectedPreset.length > 0
                        onClicked: {
                            const error = root.controller.deleteSelectedPreset()
                            if (error.length > 0) root.showNotice(error, true)
                        }
                        contentItem: Text {
                            text: deletePresetButton.text
                            color: deletePresetButton.enabled ? Theme.danger : Theme.faintInk
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.family: Theme.uiFont
                            font.pixelSize: 10
                            font.weight: Font.Medium
                        }
                        background: Rectangle {
                            radius: 5
                            color: deletePresetButton.hovered && deletePresetButton.enabled
                                   ? Theme.dangerSurface : Theme.paperWhite
                            border.width: 1
                            border.color: deletePresetButton.enabled
                                          ? Theme.dangerBorder : Theme.opticalGray
                        }
                    }
                }

                Text {
                    width: parent.width - 24
                    text: qsTr("FRAME LAYOUT")
                    color: Theme.mutedInk
                    font.family: Theme.uiFont
                    font.pixelSize: 8
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.0
                }

                GridLayout {
                    width: parent.width - 24
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 6

                    Text { text: qsTr("Format"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                    RawParameterComboBox {
                        Layout.fillWidth: true
                        model: root.formatNames
                        currentIndex: Number(root.value("format", 0))
                        onActivated: root.controller.setValue("format", index)
                    }
                    Text { text: qsTr("Width"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                    RawNumberField { Layout.fillWidth: true; controller: root.controller; parameterKey: "width"; parameterValue: root.value("width", 0); minimumValue: 1 }
                    Text { text: qsTr("Height"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                    RawNumberField { Layout.fillWidth: true; controller: root.controller; parameterKey: "height"; parameterValue: root.value("height", 0); minimumValue: 1 }
                    Text { text: qsTr("Row stride (0 = auto)"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                    RawNumberField { Layout.fillWidth: true; controller: root.controller; parameterKey: "rowStride"; parameterValue: root.value("rowStride", 0) }
                    Text { visible: root.controller && root.controller.yuvFormat; text: qsTr("Chroma stride (0 = auto)"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                    RawNumberField { visible: root.controller && root.controller.yuvFormat; Layout.fillWidth: true; controller: root.controller; parameterKey: "chromaStride"; parameterValue: root.value("chromaStride", 0) }
                    Text { text: qsTr("Header offset"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                    RawNumberField { Layout.fillWidth: true; controller: root.controller; parameterKey: "headerOffset"; parameterValue: root.value("headerOffset", 0) }
                    Text { visible: root.controller && root.controller.raw16Format; text: qsTr("Valid bits (0 = default)"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                    RawNumberField { visible: root.controller && root.controller.raw16Format; Layout.fillWidth: true; controller: root.controller; parameterKey: "validBits"; parameterValue: root.value("validBits", 0); maximumValue: 16 }
                    Text { text: qsTr("Orientation"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                    RawParameterComboBox {
                        Layout.fillWidth: true
                        model: root.orientationNames
                        currentIndex: Number(root.value("orientation", 0))
                        onActivated: root.controller.setValue("orientation", index)
                    }
                }

                Column {
                    visible: root.controller && root.controller.yuvFormat
                    width: parent.width - 24
                    spacing: 7
                    Text { text: qsTr("YUV INTERPRETATION"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1.0 }
                    GridLayout {
                        width: parent.width
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 6
                        Text { text: qsTr("Color matrix"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                        RawParameterComboBox { Layout.fillWidth: true; model: root.matrixNames; currentIndex: Number(root.value("yuvMatrix", 1)); onActivated: root.controller.setValue("yuvMatrix", index) }
                        Text { text: qsTr("Quantization range"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                        RawParameterComboBox { Layout.fillWidth: true; model: root.rangeNames; currentIndex: Number(root.value("range", 1)); onActivated: root.controller.setValue("range", index) }
                    }
                }

                Column {
                    visible: root.controller && root.controller.endianControlsVisible
                    width: parent.width - 24
                    spacing: 6
                    CheckBox {
                        text: qsTr("Little endian")
                        checked: Boolean(root.value("littleEndian", true))
                        onToggled: root.controller.setValue("littleEndian", checked)
                    }
                    CheckBox {
                        text: qsTr("Valid bits are MSB aligned")
                        checked: Boolean(root.value("msbAligned", false))
                        onToggled: root.controller.setValue("msbAligned", checked)
                    }
                }

                Column {
                    visible: root.controller && !root.controller.yuvFormat
                    width: parent.width - 24
                    spacing: 7
                    Text { text: qsTr("BAYER DEVELOPMENT"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1.0 }
                    GridLayout {
                        width: parent.width
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 6
                        Text { text: qsTr("Bayer pattern"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                        RawParameterComboBox { Layout.fillWidth: true; model: root.bayerNames; currentIndex: Number(root.value("bayerPattern", 0)); onActivated: root.controller.setValue("bayerPattern", index) }
                        Text { text: qsTr("Black level"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                        RawNumberField { Layout.fillWidth: true; controller: root.controller; parameterKey: "blackLevel"; parameterValue: root.value("blackLevel", 0) }
                        Text { text: qsTr("White level (0 = maximum)"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                        RawNumberField { Layout.fillWidth: true; controller: root.controller; parameterKey: "whiteLevel"; parameterValue: root.value("whiteLevel", 0) }
                        Text { text: qsTr("Display gamma"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 11 }
                        RawNumberField { Layout.fillWidth: true; controller: root.controller; parameterKey: "displayGamma"; parameterValue: root.value("displayGamma", 2.2); decimals: 4; minimumValue: 0.1; maximumValue: 10 }
                    }
                    CheckBox {
                        text: qsTr("Demosaic")
                        checked: Boolean(root.value("demosaic", false))
                        onToggled: root.controller.setValue("demosaic", checked)
                    }
                    Text { text: qsTr("White balance gains"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 10; font.weight: Font.Medium }
                    Row {
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: ["R", "G", "B"]
                            delegate: Column {
                                required property int index
                                required property string modelData
                                width: (parent.width - 16) / 3
                                spacing: 4
                                Text { text: modelData; color: Theme.mutedInk; font.family: Theme.monoFont; font.pixelSize: 10 }
                                RawNumberField { width: parent.width; controller: root.controller; parameterKey: "whiteBalance"; listIndex: index; parameterValue: (root.value("whiteBalance", [1,1,1]))[index]; decimals: 5; minimumValue: 0.001; maximumValue: 64 }
                            }
                        }
                    }
                    Text { text: qsTr("Color correction matrix"); color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 10; font.weight: Font.Medium }
                    Grid {
                        width: parent.width
                        columns: 3
                        rowSpacing: 7
                        columnSpacing: 7
                        Repeater {
                            model: 9
                            delegate: RawNumberField {
                                required property int index
                                width: (parent.width - 14) / 3
                                controller: root.controller
                                parameterKey: "colorMatrix"
                                listIndex: index
                                parameterValue: (root.value("colorMatrix", [1,0,0,0,1,0,0,0,1]))[index]
                                decimals: 5
                                minimumValue: -64
                                maximumValue: 64
                            }
                        }
                    }
                }

                Rectangle {
                    visible: root.noticeText.length > 0
                    width: parent.width - 24
                    height: visible ? Math.max(34, noticeLabel.implicitHeight + 16) : 0
                    radius: 5
                    color: root.noticeError ? Theme.dangerSurface : Theme.successSurface
                    border.width: 1
                    border.color: root.noticeError ? Theme.dangerBorder : Theme.successBorder
                    Text {
                        id: noticeLabel
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.noticeText
                        color: root.noticeError ? Theme.danger : Theme.success
                        wrapMode: Text.Wrap
                        font.family: Theme.uiFont
                        font.pixelSize: 10
                    }
                }
            }
        }

        Rectangle {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 50
            color: Theme.raisedSurface
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: Theme.opticalGray }
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Button {
                    width: 104; height: 28; text: qsTr("Save preset…")
                    onClicked: { root.saveAndApply = false; presetNameDialog.openWith(root.controller.suggestedPresetName) }
                }
                Button {
                    width: 114; height: 28; text: qsTr("Save + folder…")
                    onClicked: { root.saveAndApply = true; presetNameDialog.openWith(root.controller.suggestedPresetName) }
                }
                Button {
                    width: 104; height: 28; text: qsTr("Apply to folder")
                    onClicked: {
                        const error = root.controller.applyToFolder()
                        if (error.length > 0) root.showNotice(error, true)
                    }
                }
            }
        }
    }

    AppTextInputDialog {
        id: presetNameDialog
        parent: root.parent
        dialogTitle: qsTr("Save configuration")
        description: root.saveAndApply
                     ? "Save this configuration, then apply it to matching files in the folder"
                     : "Choose a reusable configuration name"
        acceptText: qsTr("Save")
        onSubmitted: function(text) {
            complete(root.controller.savePreset(text, root.saveAndApply))
        }
    }
}
