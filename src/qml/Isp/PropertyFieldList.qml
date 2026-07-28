pragma ComponentBehavior: Bound

import QtQuick
import "."

Item {
    id: root

    property var fields: []
    implicitHeight: rows.implicitHeight

    Column {
        id: rows
        width: parent.width

        Repeater {
            model: root.fields || []

            delegate: Rectangle {
                id: fieldRow
                required property int index
                required property var modelData

                width: rows.width
                height: Math.max(28, Math.max(fieldLabel.implicitHeight,
                                               fieldValue.implicitHeight) + 10)
                color: index % 2 === 0 ? Theme.raisedSurface : Theme.softHover

                Text {
                    id: fieldLabel
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    width: Math.min(154, parent.width * 0.34)
                    text: fieldRow.modelData.label || ""
                    color: Theme.mutedInk
                    font.family: Theme.uiFont
                    font.pixelSize: 10
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }

                Text {
                    id: fieldValue
                    anchors.left: fieldLabel.right
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.top: parent.top
                    anchors.topMargin: 6
                    text: fieldRow.modelData.value && String(fieldRow.modelData.value).length > 0
                          ? fieldRow.modelData.value : "—"
                    color: fieldRow.modelData.value && String(fieldRow.modelData.value).length > 0
                           ? Theme.graphiteInk : Theme.faintInk
                    font.family: Theme.monoFont
                    font.pixelSize: 10
                    wrapMode: Text.WrapAnywhere
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.pressedSurface
                }
            }
        }
    }
}
