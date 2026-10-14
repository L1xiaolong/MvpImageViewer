pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "."

Row {
    id: root
    property var labels: []
    property int currentIndex: 0
    property string tabObjectNamePrefix: "inspectorTab-"
    signal selected(int index)
    height: visible ? 36 : 0
    Repeater {
        model: root.labels
        delegate: Button {
            id: tab
            required property int index
            required property string modelData
            objectName: root.tabObjectNamePrefix + index
            width: root.width / Math.max(1, root.labels.length)
            height: 36
            text: modelData
            hoverEnabled: true
            Accessible.name: text
            Accessible.role: Accessible.PageTab
            Accessible.selected: root.currentIndex === index
            onClicked: root.selected(index)
            contentItem: Text {
                text: tab.text
                color: root.currentIndex === tab.index ? Theme.probeBlue : Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: 12
                font.weight: root.currentIndex === tab.index ? Font.DemiBold : Font.Normal
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: tab.hovered ? Theme.softHover : "transparent"
                border.width: tab.activeFocus ? 2 : 0
                border.color: Theme.probeBlue
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: root.currentIndex === tab.index ? 2 : 1
                    color: root.currentIndex === tab.index ? Theme.probeBlue : Theme.opticalGray
                }
            }
            Keys.onLeftPressed: root.selected((root.currentIndex + root.labels.length - 1) % root.labels.length)
            Keys.onRightPressed: root.selected((root.currentIndex + 1) % root.labels.length)
        }
    }
}
