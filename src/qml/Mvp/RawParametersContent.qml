pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "."

Rectangle {
    id: root
    objectName: "rawParametersContent"
    property var controller: null
    property string iconPrefix: Theme.iconPrefix
    property Item modalParent: parent
    property bool draggable: false
    property bool inspectionActive: true
    property int selectionCount: 1
    property bool saveAndApply: false
    property string noticeText: ""
    property bool noticeError: false
    property string displayedPath: controller ? controller.path : ""
    readonly property bool hasItem: !!controller && controller.path.length > 0
    readonly property bool blockingPopupVisible: presetNameDialog.visible
    readonly property bool inputFocused: ownsFocus(root.Window.window ? root.Window.window.activeFocusItem : null)
    signal closeRequested()
    signal headerDragStarted(point position)
    signal headerDragMoved(point position)
    color: Theme.paperWhite
    clip: true
    readonly property var formatNames: ["NV12", "NV21", "I420", "P010", "MIPI RAW10", "MIPI RAW12", "RAW16"]
    readonly property var bayerNames: ["RGGB", "GRBG", "GBRG", "BGGR"]
    readonly property var bayerSamplingNames: [qsTr("2 × 2 Bayer"), qsTr("4 × 4 Quad Bayer")]
    readonly property var matrixNames: ["BT.601", "BT.709", "BT.2020"]
    readonly property var primariesNames: ["BT.709 / sRGB", "BT.2020", "BT.601 625-line / BT.470 BG", "BT.601 525-line / SMPTE-C"]
    readonly property var transferNames: ["BT.709", "sRGB", "Linear"]
    readonly property var chromaLocationNames: [qsTr("Center"), qsTr("Left / cosited")]
    readonly property var rangeNames: [qsTr("Full"), qsTr("Limited")]
    readonly property var orientationNames: [qsTr("Normal"), qsTr("Rotate 90° clockwise"), qsTr("Rotate 180°"), qsTr("Rotate 270° clockwise")]
    readonly property var byteOrderNames: [qsTr("Little endian"), qsTr("Big endian")]
    readonly property var bitAlignmentNames: [qsTr("LSB aligned"), qsTr("MSB aligned")]
    function ownsFocus(item) {
        while (item) { if (item === root) return true; item = item.parent }
        return false
    }
    function value(key, fallback) {
        return controller && controller.values[key] !== undefined ? controller.values[key] : fallback
    }
    function finishEditing() {
        const item = root.Window.window ? root.Window.window.activeFocusItem : null
        if (ownsFocus(item)) {
            if (typeof item.commit === "function") item.commit()
            root.forceActiveFocus()
        }
        if (controller) controller.finishPendingChanges()
    }
    function showNotice(message, error) { noticeText = message; noticeError = error; noticeTimer.restart() }
    function beginSavePreset(applyAfter) {
        finishEditing()
        saveAndApply = applyAfter
        presetNameDialog.openWith(controller.suggestedPresetName)
    }
    onDisplayedPathChanged: { noticeText = ""; scroll.contentY = 0; presetNameDialog.close() }
    onInspectionActiveChanged: if (!inspectionActive) presetNameDialog.close()
    Keys.onEscapePressed: { finishEditing(); closeRequested() }
    Timer { id: noticeTimer; interval: 4000; onTriggered: root.noticeText = "" }
    Connections {
        target: root.controller
        function onNotificationRequested(message, error) { if (root.inspectionActive) root.showNotice(message, error) }
    }
    InspectorHeader {
        id: header
        width: parent.width
        title: qsTr("RAW / YUV parameters")
        fileName: root.hasItem ? root.controller.fileName : qsTr("No item selected")
        iconPrefix: root.iconPrefix
        draggable: root.draggable
        dragObjectName: "rawParametersDragHeader"
        closeObjectName: "closeRawParametersButton"
        closeToolTip: qsTr("Close parameters")
        onCloseRequested: { root.finishEditing(); root.closeRequested() }
        onDragStarted: function(position) { root.headerDragStarted(position) }
        onDragMoved: function(position) { root.headerDragMoved(position) }
    }
    Rectangle {
        id: notice
        objectName: "rawParameterNotice"
        anchors.top: header.bottom
        x: 12
        visible: root.noticeText.length > 0
        width: parent.width - 24
        height: visible ? noticeLabel.implicitHeight + 24 : 0
        radius: Theme.radius
        color: root.noticeError ? Theme.dangerSurface : Theme.successSurface
        border.color: root.noticeError ? Theme.dangerBorder : Theme.successBorder
        Text {
            id: noticeLabel
            x: 12; y: 12
            width: parent.width - 24
            text: root.noticeText
            wrapMode: Text.Wrap
            color: root.noticeError ? Theme.danger : Theme.success
            font.family: Theme.uiFont
            font.pixelSize: 12
        }
    }
    Flickable {
        id: scroll
        objectName: "rawParameterScroll"
        anchors.top: notice.bottom
        anchors.topMargin: notice.visible ? 12 : 0
        anchors.bottom: parent.bottom
        width: parent.width
        clip: true
        contentWidth: width
        contentHeight: body.implicitHeight + 32
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar { }
        Column {
            id: body
            x: 12; y: 16
            width: scroll.width - 24
            spacing: 12
            RawParameterComboBox {
                id: presetCombo
                objectName: "rawPresetSelector"
                visible: root.hasItem
                width: parent.width
                model: [qsTr("None")].concat(root.controller ? root.controller.presetNames : [])
                currentIndex: root.controller && root.controller.selectedPreset.length > 0
                    ? Math.max(0, model.indexOf(root.controller.selectedPreset)) : 0
                onActivated: function(index) { root.finishEditing(); root.controller.selectPreset(index > 0 ? currentText : "") }
            }
            Text {
                visible: !root.hasItem
                width: parent.width
                text: root.selectionCount > 1 ? qsTr("%1 items selected. Select one RAW or YUV file to edit its parameters.").arg(root.selectionCount)
                    : qsTr("Select a RAW or YUV file to edit its parameters.")
                wrapMode: Text.Wrap
                color: Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: 13
            }
            Column {
                visible: root.hasItem
                width: parent.width
                spacing: 12
Column {
    visible: true
    width: parent.width
    spacing: 8
    Text { width: parent.width; text: qsTr("Frame layout"); wrapMode: Text.Wrap; color: Theme.graphiteInk; font.family: Theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
    Rectangle {
        width: parent.width
        height: layoutFields.implicitHeight
        color: Theme.raisedSurface
        radius: Theme.radius
        border.color: Theme.opticalGray
        Column {
            id: layoutFields
            width: parent.width
RawParameterRow { label: qsTr("Format"); separator: true
    editor: RawParameterComboBox { objectName: "rawField-format"; model: root.formatNames; currentIndex: Number(root.value("format", 0)); onActivated: function(index) { root.controller.setValue("format", index) } }
}
RawParameterRow { label: qsTr("Width (px)"); separator: true
    editor: RawNumberField { objectName: "rawField-width"; controller: root.controller; parameterKey: "width"; parameterValue: root.value("width", 0); minimumValue: 1 }
}
RawParameterRow { label: qsTr("Height (px)"); separator: true
    editor: RawNumberField { objectName: "rawField-height"; controller: root.controller; parameterKey: "height"; parameterValue: root.value("height", 0); minimumValue: 1 }
}
RawParameterRow { label: qsTr("Row stride (bytes; 0 = auto)"); separator: true
    editor: RawNumberField { objectName: "rawField-rowStride"; controller: root.controller; parameterKey: "rowStride"; parameterValue: root.value("rowStride", 0);  }
}
RawParameterRow { visible: root.controller && root.controller.yuvFormat; label: qsTr("Chroma stride (bytes; 0 = auto)"); separator: true
    editor: RawNumberField { objectName: "rawField-chromaStride"; controller: root.controller; parameterKey: "chromaStride"; parameterValue: root.value("chromaStride", 0);  }
}
RawParameterRow { label: qsTr("Header offset (bytes)"); separator: true
    editor: RawNumberField { objectName: "rawField-headerOffset"; controller: root.controller; parameterKey: "headerOffset"; parameterValue: root.value("headerOffset", 0);  }
}
RawParameterRow { visible: root.controller && root.controller.raw16Format; label: qsTr("Valid bits (0 = default)"); separator: true
    editor: RawNumberField { objectName: "rawField-validBits"; controller: root.controller; parameterKey: "validBits"; parameterValue: root.value("validBits", 0); maximumValue: 16 }
}
RawParameterRow { label: qsTr("Orientation"); separator: false
    editor: RawParameterComboBox { objectName: "rawField-orientation"; model: root.orientationNames; currentIndex: Number(root.value("orientation", 0)); onActivated: function(index) { root.controller.setValue("orientation", index) } }
}
        }
    }
}
Column {
    visible: root.controller && root.controller.endianControlsVisible
    width: parent.width
    spacing: 8
    Text { width: parent.width; text: qsTr("Storage"); wrapMode: Text.Wrap; color: Theme.graphiteInk; font.family: Theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
    Rectangle {
        width: parent.width
        height: storageFields.implicitHeight
        color: Theme.raisedSurface
        radius: Theme.radius
        border.color: Theme.opticalGray
        Column {
            id: storageFields
            width: parent.width
RawParameterRow { label: qsTr("Byte order"); separator: true
    editor: RawParameterComboBox { objectName: "rawField-littleEndian"; model: root.byteOrderNames; currentIndex: root.value("littleEndian", true) ? 0 : 1; onActivated: function(index) { root.controller.setValue("littleEndian", index === 0) } }
}
RawParameterRow { label: qsTr("Bit alignment"); separator: false
    editor: RawParameterComboBox { objectName: "rawField-msbAligned"; model: root.bitAlignmentNames; currentIndex: root.value("msbAligned", false) ? 1 : 0; onActivated: function(index) { root.controller.setValue("msbAligned", index === 1) } }
}
        }
    }
}
            }
            Column {
                visible: root.hasItem
                width: parent.width
                spacing: 12
Column {
    visible: root.controller && root.controller.yuvFormat
    width: parent.width
    spacing: 8
    Text { width: parent.width; text: qsTr("YUV interpretation"); wrapMode: Text.Wrap; color: Theme.graphiteInk; font.family: Theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
    Rectangle {
        width: parent.width
        height: yuvFields.implicitHeight
        color: Theme.raisedSurface
        radius: Theme.radius
        border.color: Theme.opticalGray
        Column {
            id: yuvFields
            width: parent.width
RawParameterRow { label: qsTr("Color matrix"); separator: true
    editor: RawParameterComboBox { objectName: "rawField-yuvMatrix"; model: root.matrixNames; currentIndex: Number(root.value("yuvMatrix", 1)); onActivated: function(index) { root.controller.setValue("yuvMatrix", index) } }
}
RawParameterRow { label: qsTr("Color primaries"); separator: true
    editor: RawParameterComboBox { objectName: "rawField-yuvPrimaries"; model: root.primariesNames; currentIndex: Number(root.value("yuvPrimaries", 0)); onActivated: function(index) { root.controller.setValue("yuvPrimaries", index) } }
}
RawParameterRow { label: qsTr("Transfer function"); separator: true
    editor: RawParameterComboBox { objectName: "rawField-yuvTransfer"; model: root.transferNames; currentIndex: Number(root.value("yuvTransfer", 0)); onActivated: function(index) { root.controller.setValue("yuvTransfer", index) } }
}
RawParameterRow { label: qsTr("Chroma location"); separator: true
    editor: RawParameterComboBox { objectName: "rawField-chromaLocation"; model: root.chromaLocationNames; currentIndex: Number(root.value("chromaLocation", 0)); onActivated: function(index) { root.controller.setValue("chromaLocation", index) } }
}
RawParameterRow { label: qsTr("Quantization range"); separator: false
    editor: RawParameterComboBox { objectName: "rawField-range"; model: root.rangeNames; currentIndex: Number(root.value("range", 1)); onActivated: function(index) { root.controller.setValue("range", index) } }
}
        }
    }
}
Column {
    visible: root.controller && !root.controller.yuvFormat
    width: parent.width
    spacing: 8
    Text { width: parent.width; text: qsTr("Bayer development"); wrapMode: Text.Wrap; color: Theme.graphiteInk; font.family: Theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
    Rectangle {
        width: parent.width
        height: bayerFields.implicitHeight
        color: Theme.raisedSurface
        radius: Theme.radius
        border.color: Theme.opticalGray
        Column {
            id: bayerFields
            width: parent.width
RawParameterRow { label: qsTr("Bayer pattern"); separator: true
    editor: RawParameterComboBox { objectName: "rawField-bayerPattern"; model: root.bayerNames; currentIndex: Number(root.value("bayerPattern", 0)); onActivated: function(index) { root.controller.setValue("bayerPattern", index) } }
}
RawParameterRow { label: qsTr("CFA sampling"); separator: true
    editor: RawParameterComboBox { objectName: "rawField-bayerSampling"; model: root.bayerSamplingNames; currentIndex: Number(root.value("bayerSampling", 0)); onActivated: function(index) { root.controller.setValue("bayerSampling", index) } }
}
RawParameterRow { label: qsTr("Demosaic"); separator: true
    editor: SettingsToggle { font.family: Theme.uiFont; font.pixelSize: 12; objectName: "rawField-demosaic"; checked: Boolean(root.value("demosaic", false)); Accessible.name: qsTr("Demosaic"); onToggled: root.controller.setValue("demosaic", checked) }
}
RawParameterRow { label: qsTr("Black level"); separator: true
    editor: RawNumberField { objectName: "rawField-blackLevel"; controller: root.controller; parameterKey: "blackLevel"; parameterValue: root.value("blackLevel", 0);  }
}
RawParameterRow { label: qsTr("White level (0 = maximum)"); separator: true
    editor: RawNumberField { objectName: "rawField-whiteLevel"; controller: root.controller; parameterKey: "whiteLevel"; parameterValue: root.value("whiteLevel", 0);  }
}
RawParameterRow { label: qsTr("Display gamma"); separator: false
    editor: RawNumberField { objectName: "rawField-displayGamma"; controller: root.controller; parameterKey: "displayGamma"; parameterValue: root.value("displayGamma", 0); decimals: 4; minimumValue: 0.1; maximumValue: 10 }
}
        }
    }
}
Column {
    visible: root.controller && !root.controller.yuvFormat
    width: parent.width
    spacing: 8
    Text { width: parent.width; text: qsTr("White balance gains"); wrapMode: Text.Wrap; color: Theme.graphiteInk; font.family: Theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
    Rectangle {
        width: parent.width
        height: wbFields.implicitHeight
        color: Theme.raisedSurface
        radius: Theme.radius
        border.color: Theme.opticalGray
        Column {
            id: wbFields
            width: parent.width
Item {
    width: parent.width
    height: wbRow.implicitHeight + 24
    Row {
        id: wbRow
        x: 12; y: 12
        width: parent.width - 24
        spacing: 8
        Repeater {
            model: ["R", "G", "B"]
            delegate: Column {
                required property int index
                required property string modelData
                width: (wbRow.width - 16) / 3
                spacing: 6
                Text { text: parent.modelData; width: parent.width; height: 20; verticalAlignment: Text.AlignVCenter; color: Theme.mutedInk; font.family: Theme.uiFont; font.pixelSize: 12 }
                RawNumberField { objectName: "rawWhiteBalance-" + parent.index; width: parent.width; controller: root.controller; parameterKey: "whiteBalance"; listIndex: parent.index; parameterValue: root.value("whiteBalance", [1,1,1])[parent.index]; decimals: 5; minimumValue: 0.001; maximumValue: 64 }
            }
        }
    }
}
        }
    }
}
Column {
    visible: root.controller && !root.controller.yuvFormat
    width: parent.width
    spacing: 8
    Text { width: parent.width; text: qsTr("Color correction matrix"); wrapMode: Text.Wrap; color: Theme.graphiteInk; font.family: Theme.uiFont; font.pixelSize: 13; font.weight: Font.DemiBold }
    Rectangle {
        width: parent.width
        height: ccmFields.implicitHeight
        color: Theme.raisedSurface
        radius: Theme.radius
        border.color: Theme.opticalGray
        Column {
            id: ccmFields
            width: parent.width
Item {
    width: parent.width
    height: matrixGrid.implicitHeight + 24
    Grid {
        id: matrixGrid
        x: 12; y: 12
        width: parent.width - 24
        columns: 3
        rowSpacing: 8
        columnSpacing: 8
        Repeater {
            model: 9
            delegate: RawNumberField {
                required property int index
                objectName: "rawColorMatrix-" + index
                width: (matrixGrid.width - 16) / 3
                controller: root.controller
                parameterKey: "colorMatrix"
                listIndex: index
                parameterValue: root.value("colorMatrix", [1,0,0,0,1,0,0,0,1])[index]
                decimals: 5
                minimumValue: -64
                maximumValue: 64
            }
        }
    }
}
        }
    }
}
            }
            Column {
                visible: root.hasItem
                width: parent.width
                spacing: 12
Column {
    width: parent.width
    spacing: 8
    SettingsButton {
        objectName: "rawDeletePresetButton"
        width: parent.width
        text: qsTr("Delete preset")
        destructive: true
        enabled: root.hasItem && root.controller.selectedPreset.length > 0
        onClicked: { root.finishEditing(); const error = root.controller.deleteSelectedPreset(); if (error) root.showNotice(error, true) }
    }
    SettingsButton {
        objectName: "rawSavePresetButton"
        width: parent.width
        text: qsTr("Save preset…")
        onClicked: root.beginSavePreset(false)
    }
    SettingsButton {
        objectName: "rawSaveFolderPresetButton"
        width: parent.width
        text: qsTr("Save + folder…")
        onClicked: root.beginSavePreset(true)
    }
    SettingsButton {
        objectName: "rawApplyFolderButton"
        width: parent.width
        text: qsTr("Apply to folder")
        onClicked: { root.finishEditing(); const error = root.controller.applyToFolder(); if (error) root.showNotice(error, true) }
    }
}
            }

        }
    }
    AppTextInputDialog {
        id: presetNameDialog
        objectName: "rawPresetNameDialog"
        parent: root.modalParent
        dialogTitle: qsTr("Save configuration")
        description: root.saveAndApply ? qsTr("Save this configuration, then apply it to matching files in the folder") : qsTr("Choose a reusable configuration name")
        acceptText: qsTr("Save")
        onSubmitted: function(text) { complete(root.controller.savePreset(text, root.saveAndApply)) }
    }
}
