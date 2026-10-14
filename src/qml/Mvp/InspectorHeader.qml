pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "."

Item {
    id: root
    property string title: ""
    property string fileName: ""
    property string closeToolTip: qsTr("Close")
    property string iconPrefix: Theme.iconPrefix
    property bool draggable: false
    property string dragObjectName: "inspectorDragHeader"
    property string closeObjectName: "closeInspectorButton"
    signal closeRequested()
    signal dragStarted(point position)
    signal dragMoved(point position)
    implicitHeight: 78
    height: implicitHeight
    MouseArea {
        objectName: root.dragObjectName
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: parent.width - 48
        enabled: root.draggable
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        cursorShape: Qt.SizeAllCursor
        onPressed: function(mouse) { root.dragStarted(mapToItem(null, mouse.x, mouse.y)) }
        onPositionChanged: function(mouse) { if (pressed) root.dragMoved(mapToItem(null, mouse.x, mouse.y)) }
    }
    Text {
        x: 16; y: 6; height: 32
        width: parent.width - 64
        text: root.title
        verticalAlignment: Text.AlignVCenter
        color: Theme.graphiteInk
        font.family: Theme.uiFont
        font.pixelSize: 16
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    Text {
        id: nameLabel
        x: 16; y: 40; height: 38
        width: parent.width - 32
        text: root.fileName
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideMiddle
        color: Theme.mutedInk
        font.family: Theme.uiFont
        font.pixelSize: 12
        HoverHandler { id: nameHover }
        ToolTip.visible: nameHover.hovered && truncated
        ToolTip.text: text
    }
    AppIconButton {
        objectName: root.closeObjectName
        anchors.right: parent.right; anchors.rightMargin: 8
        y: 6
        controlSize: 32
        renderedIconSize: 16
        iconSource: root.iconPrefix + "close.svg"
        toolTipText: root.closeToolTip
        Accessible.name: toolTipText
        onClicked: root.closeRequested()
    }
}
