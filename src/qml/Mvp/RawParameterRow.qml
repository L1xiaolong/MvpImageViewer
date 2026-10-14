pragma ComponentBehavior: Bound
import QtQuick
import "."

Item {
    id: root
    property string label: ""
    property Component editor
    property bool separator: true
    width: parent ? parent.width : 320
    height: Math.max(48, caption.implicitHeight + 16)
    Text {
        id: caption
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(136, (root.width - 24) * 0.34)
        text: root.label
        color: Theme.mutedInk
        font.family: Theme.uiFont
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }
    Loader {
        anchors.left: caption.right
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        height: 32
        sourceComponent: root.editor
    }
    Rectangle {
        visible: root.separator
        x: 12; width: parent.width - 24
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.opticalGray
        opacity: 0.55
    }
}
