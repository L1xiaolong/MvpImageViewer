import QtQuick
import QtQuick.Controls

ToolTip {
    id: control
    property string path: ""
    property string fileName: path.split(/[\\/]/).pop() || path
    readonly property int separatorIndex: Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
    readonly property string directoryPath: separatorIndex >= 0 ? path.slice(0, separatorIndex) : ""
    text: path
    delay: 800
    timeout: 8000
    padding: 12
    margins: 8
    implicitWidth: Math.min(480, Math.max(240, Math.max(nameLabel.implicitWidth, directoryLabel.implicitWidth) + 24))
    contentItem: Column {
        spacing: 6
        Text {
            id: nameLabel
            objectName: "pathTooltipName"
            width: parent.width
            text: control.fileName
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 12
            font.weight: Font.Medium
        }
        Text {
            id: directoryLabel
            objectName: "pathTooltipDirectory"
            width: parent.width
            text: control.directoryPath
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            visible: text.length > 0
            color: Theme.mutedInk
            font.family: Theme.uiFont
            font.pixelSize: 11
        }
    }
    background: Rectangle {
        color: Theme.raisedSurface
        radius: 6
        border.width: 1
        border.color: Theme.opticalGray
    }
}
