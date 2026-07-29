import QtQuick

Rectangle {
    id: root
    objectName: "designFullScreenCanvas"

    property var controller: null
    property int navigationRevision: 1
    signal pixelHovered(int sourceSlot, point pixel, color colorValue, bool valid)
    signal contextMenuRequested(point position)

    color: "#A0A0A0"

    function navigationState(slot) {
        return { "visible": true, "width": 82, "height": 58,
            "viewport": Qt.rect(0.22, 0.18, 0.52, 0.56), "zoom": "135%" }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 54
        color: "#52616B"

        Text {
            anchors.centerIn: parent
            text: qsTr("Demo image")
            color: "#F4F5F2"
            font.pixelSize: 28
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        hoverEnabled: true
        onPositionChanged: root.pixelHovered(0, Qt.point(Math.round(mouseX), Math.round(mouseY)),
                                             "#5078A0", true)
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton)
                root.contextMenuRequested(Qt.point(mouse.x, mouse.y))
        }
    }
}
