import QtQuick

Rectangle {
    id: root
    property string text: ""
    property string severity: "info"
    width: parent ? parent.width : 400
    implicitHeight: message.implicitHeight + 24
    height: visible ? implicitHeight : 0
    visible: text.length > 0
    radius: Theme.radius
    color: severity === "error" ? Theme.dangerSurface : severity === "success" ? Theme.successSurface : Theme.paperWhite
    border.color: severity === "error" ? Theme.dangerBorder : severity === "success" ? Theme.successBorder : Theme.opticalGray
    Text {
        id: message
        x: 12
        y: 12
        width: parent.width - 24
        text: root.text
        color: root.severity === "error" ? Theme.danger : Theme.graphiteInk
        font.family: Theme.uiFont
        font.pixelSize: 12
        wrapMode: Text.Wrap
        lineHeight: 1.25
    }
}
