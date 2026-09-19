import QtQuick
import "."

Item {
    id: root
    objectName: "designCompareCanvas"

    property var controller: null
    property var settingsController: null
    property bool smoothDisplay: !settingsController ||
                                 settingsController.smoothDisplay === undefined
                                 ? true : settingsController.smoothDisplay
    property real dividerPosition: width * 0.5
    property int navigationRevision: 0
    property real compareAmount: controller ? controller.splitAmount : 0.5

    signal pixelHovered(int sourceSlot, point pixel, string valueText, bool valid)

    function navigationState(slot) {
        return { "visible": true, "width": 82, "height": 58,
            "viewport": Qt.rect(0.22, 0.18, 0.52, 0.56), "zoom": "84%" }
    }

    Rectangle {
        anchors.fill: parent
        color: "#A0A0A0"
    }

    Repeater {
        model: controller && controller.previewUrls ? controller.previewUrls : []
        delegate: Item {
            required property int index
            required property url modelData
            readonly property int count: root.controller.previewUrls.length
            readonly property int columns: count === 4 ? 2 : Math.max(1, count)
            readonly property int rows: count === 4 ? 2 : 1
            x: (index % columns) * root.width / columns
            y: Math.floor(index / columns) * root.height / rows
            width: root.width / columns
            height: root.height / rows
            clip: true

            Image {
                anchors.fill: parent
                anchors.margins: 18
                source: modelData
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                smooth: root.smoothDisplay
                mipmap: root.smoothDisplay
            }
        }
    }
}
