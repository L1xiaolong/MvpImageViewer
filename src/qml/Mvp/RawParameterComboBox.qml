pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "."

ComboBox {
    id: root

    height: 28
    leftPadding: 8
    rightPadding: 26
    font.family: Theme.uiFont
    font.pixelSize: 10
    contentItem: Text {
        leftPadding: 0
        rightPadding: 0
        text: root.displayText
        color: root.enabled ? Theme.graphiteInk : Theme.faintInk
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font: root.font
    }
    indicator: Text {
        x: root.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("⌄")
        color: Theme.mutedInk
        font.family: Theme.uiFont
        font.pixelSize: 13
    }
    background: Rectangle {
        radius: 5
        color: root.enabled ? Theme.paperWhite : Theme.softHover
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.probeBlue : Theme.opticalGray
    }
    popup: Popup {
        y: root.height + 3
        width: root.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 260)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            radius: 6
            color: Theme.paperWhite
            border.width: 1
            border.color: Theme.opticalGray
        }
    }
    delegate: ItemDelegate {
        id: optionDelegate
        required property int index
        required property var modelData
        width: root.width - 8
        height: 27
        text: modelData
        highlighted: root.highlightedIndex === index
        contentItem: Text {
            text: optionDelegate.text
            color: Theme.graphiteInk
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font.family: Theme.uiFont
            font.pixelSize: 10
        }
        background: Rectangle {
            radius: 4
            color: optionDelegate.highlighted ? Theme.softHover : "transparent"
        }
    }
}
