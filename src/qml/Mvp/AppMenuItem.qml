import QtQuick
import QtQuick.Controls
import "."

MenuItem {
    id: control
    property string shortcutText: ""
    property bool destructive: false

    // QQuickMenu still accounts for an invisible item's implicit size when calculating its
    // content height. Conditional actions must collapse completely instead of leaving a blank row.
    implicitHeight: visible ? 32 : 0
    leftPadding: 10
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0

    contentItem: Item {
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: !control.enabled ? Theme.faintInk
                  : control.destructive ? Theme.danger : Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 13
        }
        Text {
            visible: control.shortcutText.length > 0
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: control.shortcutText
            color: Theme.faintInk
            font.family: Theme.uiFont
            font.pixelSize: 11
        }
    }

    background: Rectangle {
        radius: 5
        color: control.highlighted && control.enabled ? Theme.explorerSelectionBg : "transparent"
    }
}
