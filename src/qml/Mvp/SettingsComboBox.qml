pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

ComboBox {
    id: root
    implicitHeight: 32
    implicitWidth: 210
    leftPadding: 10
    rightPadding: 28
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity
    contentItem: Text {
        text: root.displayText
        color: Theme.graphiteInk
        font.family: Theme.uiFont
        font.pixelSize: 12
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Image {
        x: root.width - width - 9
        y: (root.height - height) / 2
        width: 14
        height: 14
        source: Theme.iconPrefix + "chevron-down.svg"
    }
    background: Rectangle {
        radius: Theme.radius
        color: root.down ? Theme.pressedSurface : root.hovered ? Theme.softHover : Theme.raisedSurface
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.probeBlue : root.hovered ? Theme.searchFieldHoverBorder : Theme.opticalGray
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: root.width - 2
        height: 32
        highlighted: root.highlightedIndex === index
        contentItem: Text {
            text: option.modelData
            color: Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle { color: option.highlighted ? Theme.explorerSelectionBg : Theme.raisedSurface }
    }
    popup: Popup {
        y: root.height + 3
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 210)
        padding: 1
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            ScrollBar.vertical: ScrollBar { }
        }
        background: Rectangle {
            radius: Theme.radius
            color: Theme.raisedSurface
            border.color: Theme.opticalGray
        }
    }
}
