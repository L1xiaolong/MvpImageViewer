import QtQuick

Item {
    id: root
    property string title: ""
    property string description: ""
    property int controlWidth: 210
    default property alias controls: controlHost.data
    width: parent ? parent.width : 600
    implicitHeight: Math.max(52, labels.implicitHeight + 16, controlHost.childrenRect.height + 16)
    height: implicitHeight
    data: [Column {
        id: labels
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, parent.width - root.controlWidth - 20)
        spacing: 4
        Text {
            width: parent.width
            text: root.title
            color: Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: Theme.bodyFontSize
            font.weight: Theme.bodyFontWeight
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            text: root.description
            visible: text.length > 0
            color: Theme.mutedInk
            font.family: Theme.uiFont
            font.pixelSize: Theme.captionFontSize
            font.weight: Theme.bodyFontWeight
            wrapMode: Text.Wrap
            lineHeight: Theme.descriptionLineHeight
        }
    }, Item {
        id: controlHost
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: root.controlWidth
        height: childrenRect.height
    }
    ]
}
