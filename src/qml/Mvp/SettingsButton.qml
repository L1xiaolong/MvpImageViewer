import QtQuick
import QtQuick.Controls

Button {
    id: root
    property bool primary: false
    property bool destructive: false
    implicitHeight: 32
    implicitWidth: Math.max(72, label.implicitWidth + 24)
    leftPadding: 12
    rightPadding: 12
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity
    contentItem: Text {
        id: label
        text: root.text
        color: root.primary ? Theme.primaryButtonText : root.destructive ? Theme.danger : Theme.graphiteInk
        font.family: Theme.uiFont
        font.pixelSize: 12
        font.weight: root.primary ? Font.DemiBold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: Theme.radius
        color: root.primary ? (root.down ? Theme.primaryButton : root.hovered ? Theme.primaryButtonHover : Theme.primaryButton)
               : root.down ? Theme.pressedSurface : root.hovered ? Theme.softHover : Theme.raisedSurface
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.probeBlue : root.destructive ? Theme.dangerBorder : Theme.opticalGray
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
