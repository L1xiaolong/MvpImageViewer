import QtQuick
import QtQuick.Controls
import "."

Row {
    id: root
    required property var controller
    property bool maximized: false
    height: 44
    width: 138
    Repeater {
        model: 3
        Button {
            id: captionButton
            required property int index
            objectName: ["windowMinimizeButton", "windowMaximizeButton", "windowCloseButton"][index]
            width: 46
            height: root.height
            padding: 0
            hoverEnabled: true
            readonly property bool visualHover: hovered || root.controller.hoveredCaptionButton === index
            readonly property bool visualPressed: down || root.controller.pressedCaptionButton === index
            readonly property color glyphColor: index === 2 && (visualHover || visualPressed)
                                               ? "white" : Theme.graphiteInk
            text: index === 0 ? qsTr("Minimize") : index === 1
                  ? (root.maximized ? qsTr("Restore") : qsTr("Maximize")) : qsTr("Close")
            Accessible.name: text
            ToolTip.visible: visualHover
            ToolTip.delay: 700
            ToolTip.text: text
            onClicked: root.controller.activateCaptionButton(index)
            background: Rectangle {
                color: captionButton.index === 2
                    ? captionButton.visualPressed ? "#A82319" : captionButton.visualHover ? "#C42B1C" : "transparent"
                    : captionButton.visualPressed ? Theme.explorerSelectionBg
                    : captionButton.visualHover ? Theme.softHover : "transparent"
                border.width: captionButton.visualFocus ? 1 : 0
                border.color: Theme.keyboardFocus
            }
            contentItem: Item {
                Image {
                    objectName: "windowCaptionGlyph"
                    anchors.centerIn: parent
                    width: 12
                    height: 12
                    sourceSize: Qt.size(24, 24)
                    source: Theme.iconPrefix + "caption-" + (captionButton.index === 0 ? "minimize"
                        : captionButton.index === 1 ? (root.maximized ? "restore" : "maximize")
                        : captionButton.visualHover || captionButton.visualPressed ? "close-white" : "close") + ".svg"
                }
            }
        }
    }
}
