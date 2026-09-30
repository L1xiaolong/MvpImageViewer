pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "."

Dialog {
    id: root
    objectName: "imagePropertiesDialog"

    property var controller: null
    property string iconPrefix: Theme.iconPrefix
    property int currentTab: 0
    property real dragOriginX: 0
    property real dragOriginY: 0
    readonly property var tabs: controller && controller.hasRawParameters
                                ? ["EXIF", "Histogram", "RAW parameters"]
                                : ["EXIF", "Histogram"]

    modal: true
    focus: true
    width: parent ? Math.min(600, parent.width - 48) : 600
    height: parent ? Math.min(680, parent.height - 40) : 680
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    closePolicy: Popup.CloseOnEscape

    function openForPath(path) {
        currentTab = 0
        if (controller)
            controller.loadPath(path)
        open()
    }

    Overlay.modal: Rectangle { color: "#330E1820" }

    background: Rectangle {
        color: Theme.paperWhite
        radius: 10
        border.width: 1
        border.color: Theme.opticalGray
    }

    contentItem: Item {
        Rectangle {
            id: header
            objectName: "imagePropertiesDragHeader"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 60
            color: "transparent"

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                preventStealing: true
                property point pressPosition: Qt.point(0, 0)
                onPressed: function(mouse) {
                    root.dragOriginX = root.x
                    root.dragOriginY = root.y
                    pressPosition = mapToItem(root.parent, mouse.x, mouse.y)
                    mouse.accepted = true
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !root.parent)
                        return
                    const current = mapToItem(root.parent, mouse.x, mouse.y)
                    root.x = Math.max(0, Math.min(root.parent.width - root.width,
                                                 root.dragOriginX + current.x - pressPosition.x))
                    root.y = Math.max(0, Math.min(root.parent.height - root.height,
                                                 root.dragOriginY + current.y - pressPosition.y))
                }
            }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.top: parent.top
                anchors.topMargin: 10
                text: root.controller && root.controller.directory
                      ? "FOLDER" : "IMAGE INSPECTION"
                color: Theme.probeBlue
                font.family: Theme.uiFont
                font.pixelSize: 8
                font.weight: Font.DemiBold
                font.letterSpacing: 1.2
            }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.right: closeButton.left
                anchors.rightMargin: 12
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 9
                text: root.controller ? root.controller.fileName : "Properties"
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideMiddle
            }
            AppIconButton {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                controlSize: 28
                renderedIconSize: 14
                iconSource: root.iconPrefix + "close.svg"
                toolTipText: qsTr("Close")
                onClicked: root.close()
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.opticalGray
            }
        }

        ScrollView {
            id: scroll
            objectName: "imagePropertiesScroll"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.bottom: parent.bottom
            anchors.margins: 0
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Column {
                width: scroll.availableWidth
                spacing: 10
                padding: 12

                Text {
                    width: parent.width - 24
                    text: qsTr("FILE")
                    color: Theme.mutedInk
                    font.family: Theme.uiFont
                    font.pixelSize: 8
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.0
                }

                Rectangle {
                    width: parent.width - 24
                    height: basicFields.implicitHeight
                    radius: 6
                    color: Theme.raisedSurface
                    border.width: 1
                    border.color: Theme.opticalGray
                    clip: true
                    PropertyFieldList {
                        id: basicFields
                        width: parent.width
                        fields: root.controller ? root.controller.basicFields : []
                    }
                }

                Text {
                    visible: root.controller && root.controller.loading
                    width: parent.width - 24
                    text: qsTr("Reading full-resolution metadata…")
                    color: Theme.mutedInk
                    font.family: Theme.uiFont
                    font.pixelSize: 10
                }

                Text {
                    visible: root.controller && root.controller.errorText.length > 0
                    width: parent.width - 24
                    text: root.controller ? root.controller.errorText : ""
                    color: Theme.danger
                    wrapMode: Text.Wrap
                    font.family: Theme.uiFont
                    font.pixelSize: 10
                }

                Item {
                    visible: root.controller && !root.controller.directory
                    width: parent.width - 24
                    height: visible ? 30 : 0
                    Row {
                        anchors.fill: parent
                        spacing: 4
                        Repeater {
                            model: root.tabs
                            delegate: Button {
                                id: tabButton
                                required property int index
                                required property string modelData
                                objectName: "imagePropertiesTab-" + index
                                height: 28
                                width: Math.max(72, tabLabel.implicitWidth + 20)
                                text: modelData
                                onClicked: root.currentTab = index
                                contentItem: Text {
                                    id: tabLabel
                                    text: tabButton.text
                                    color: root.currentTab === tabButton.index
                                           ? Theme.graphiteInk : Theme.mutedInk
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.family: Theme.uiFont
                                    font.pixelSize: 10
                                    font.weight: root.currentTab === tabButton.index
                                                 ? Font.DemiBold : Font.Medium
                                }
                                background: Item {
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: root.currentTab === tabButton.index ? 2 : 1
                                        color: root.currentTab === tabButton.index
                                               ? Theme.probeBlue : Theme.opticalGray
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    visible: root.controller && !root.controller.directory && root.currentTab === 0
                    width: parent.width - 24
                    height: visible ? exifFields.implicitHeight : 0
                    radius: 6
                    color: Theme.raisedSurface
                    border.width: 1
                    border.color: Theme.opticalGray
                    clip: true
                    PropertyFieldList {
                        id: exifFields
                        width: parent.width
                        fields: root.controller ? root.controller.exifFields : []
                    }
                }

                Loader {
                    visible: active
                    active: root.controller && !root.controller.directory && root.currentTab === 1
                    width: parent.width - 24
                    height: active && item ? item.implicitHeight : 0
                    sourceComponent: PropertiesHistogram {
                        controller: root.controller
                        width: parent ? parent.width : 0
                    }
                }

                Rectangle {
                    visible: root.controller && !root.controller.directory && root.currentTab === 2
                    width: parent.width - 24
                    height: visible ? rawFields.implicitHeight : 0
                    radius: 6
                    color: Theme.raisedSurface
                    border.width: 1
                    border.color: Theme.opticalGray
                    clip: true
                    PropertyFieldList {
                        id: rawFields
                        width: parent.width
                        fields: root.controller ? root.controller.rawFields : []
                    }
                }
            }
        }

    }
}
