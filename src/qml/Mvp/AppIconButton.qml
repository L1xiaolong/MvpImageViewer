import QtQuick
import QtQuick.Controls
import "."

Button {
    id: control
    property url iconSource
    property string toolTipText: text
    property bool compact: text.length === 0
    property int controlSize: Theme.touchTarget
    property int renderedIconSize: Theme.iconSize

    implicitWidth: compact ? controlSize : contentRow.implicitWidth + 20
    implicitHeight: controlSize
    padding: 0
    opacity: enabled ? 1 : Theme.disabledOpacity

    background: Rectangle {
        color: control.checked ? Theme.explorerSelectionBg : control.down ? Theme.pressedSurface : control.hovered ? Theme.softHover : "transparent"
        radius: 6
        border.width: control.activeFocus ? 1 : 0
        border.color: Theme.probeBlue
    }

    contentItem: Item {
        Row {
            id: contentRow
            spacing: control.compact ? 0 : 6
            anchors.centerIn: parent
            Image {
                width: control.renderedIconSize
                height: control.renderedIconSize
                source: control.iconSource
                sourceSize: Qt.size(control.renderedIconSize * 2,
                                    control.renderedIconSize * 2)
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }
            Text {
                visible: !control.compact
                text: control.text
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    ToolTip.visible: hovered && toolTipText.length > 0
    ToolTip.text: toolTipText
    ToolTip.delay: 500
}
