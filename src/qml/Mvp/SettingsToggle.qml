import QtQuick
import QtQuick.Controls

Switch {
    id: root
    implicitWidth: 40
    implicitHeight: 32
    padding: 0
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity
    indicator: Rectangle {
        x: root.width - width
        y: (root.height - height) / 2
        width: 36
        height: 20
        radius: 10
        color: root.checked ? Theme.primaryButton : Theme.opticalGray
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.probeBlue : root.hovered ? Theme.mutedInk : color
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        Rectangle {
            x: root.checked ? parent.width - width - 3 : 3
            y: 3
            width: 14
            height: 14
            radius: 7
            color: root.checked ? Theme.primaryButtonText : Theme.raisedSurface
            Behavior on x { NumberAnimation { duration: Theme.fast } }
        }
    }
    contentItem: Item { }
}
