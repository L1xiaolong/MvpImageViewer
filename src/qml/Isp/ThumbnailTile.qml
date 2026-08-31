pragma ComponentBehavior: Bound
// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

Item {
    id: root
    required property var controller
    property var workspaceController: controller
    property string path
    property string fileName
    property string technicalLabel
    property string fileType
    property var dimensions
    property int bitDepth: 0
    property string fileSizeText
    property url thumbnailUrl
    property bool directory: false
    property bool selected: false
    property int selectionOrdinal: 0
    property int displayMode: 0
    readonly property string pathToolTipText: ToolTip.text
    readonly property bool pathToolTipVisible: ToolTip.visible
    readonly property int pathToolTipDelay: ToolTip.delay
    readonly property bool pathHoverActive: tileMouse.containsMouse
    signal activated
    signal selectionRequested(bool extend, bool toggle)

    component MetadataStrip: Item {
        id: metadataStrip
        required property string typeText
        required property var imageDimensions
        required property int sourceBitDepth
        required property string sizeText
        required property bool directory
        property string labelObjectName
        property string badgeObjectName

        readonly property string normalizedType: typeText.toUpperCase()
        readonly property bool yuvType: normalizedType === "YUV"
        readonly property bool rawType: normalizedType === "RAW" || normalizedType === "DNG" ||
                                        normalizedType === "CR2" || normalizedType === "CR3" ||
                                        normalizedType === "NEF" || normalizedType === "NRW" ||
                                        normalizedType === "ARW" || normalizedType === "SR2" ||
                                        normalizedType === "SRF" || normalizedType === "RAF" ||
                                        normalizedType === "ORF" || normalizedType === "ORW" ||
                                        normalizedType === "RW2" || normalizedType === "PEF" ||
                                        normalizedType === "IIQ" || normalizedType === "3FR" ||
                                        normalizedType === "ERF" || normalizedType === "MEF" ||
                                        normalizedType === "MOS" || normalizedType === "MRW" ||
                                        normalizedType === "X3F"
        readonly property color badgeSurface: yuvType ? Theme.yuvBadgeSurface
                                                     : rawType ? Theme.rawBadgeSurface
                                                               : Theme.encodedBadgeSurface
        readonly property color badgeBorder: yuvType ? Theme.yuvBadgeBorder
                                                    : rawType ? Theme.rawBadgeBorder
                                                              : Theme.encodedBadgeBorder
        readonly property color badgeInk: yuvType ? Theme.yuvBadgeText
                                                 : rawType ? Theme.rawBadgeText
                                                           : Theme.encodedBadgeText
        readonly property string dimensionText:
            imageDimensions && imageDimensions.width > 0 && imageDimensions.height > 0
                ? imageDimensions.width + "×" + imageDimensions.height
                : qsTr("Reading size…")
        readonly property string depthText: sourceBitDepth > 0
                                                ? sourceBitDepth + " bit"
                                                : qsTr("Reading…")

        height: 18

        RowLayout {
            anchors.fill: parent
            spacing: 5

            Rectangle {
                id: typeBadge
                objectName: metadataStrip.badgeObjectName
                visible: !metadataStrip.directory
                Layout.preferredWidth: typeLabel.implicitWidth + 10
                Layout.preferredHeight: 17
                radius: 4
                color: metadataStrip.badgeSurface
                border.width: 1
                border.color: metadataStrip.badgeBorder

                Text {
                    id: typeLabel
                    anchors.centerIn: parent
                    text: metadataStrip.normalizedType
                    color: metadataStrip.badgeInk
                    font.family: Theme.uiFont
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
            }

            Text {
                id: technicalText
                objectName: metadataStrip.labelObjectName
                Layout.fillWidth: true
                text: metadataStrip.directory
                      ? qsTr("Folder")
                      : "| " + metadataStrip.dimensionText + " | " +
                        metadataStrip.depthText + " | " + metadataStrip.sizeText
                elide: Text.ElideRight
                color: Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: Theme.metadataFontSize
            }
        }
    }

    Drag.active: tileDragHandler.active && root.selected
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.mimeData: ({ "text/uri-list": root.controller.selectedUriList })
    Drag.imageSource: root.thumbnailUrl
    Drag.hotSpot.x: Math.min(root.width / 2, 80)
    Drag.hotSpot.y: Math.min(root.height / 2, 60)

    width: GridView.view ? GridView.view.cellWidth - 16 : 196
    height: displayMode === 1 ? 68 : width * 0.75 + (displayMode === 2 ? 36 : 58)

    ToolTip.delay: 500
    ToolTip.timeout: 8000
    ToolTip.visible: tileMouse.containsMouse && !tileDragHandler.active &&
                     root.path.length > 0
    ToolTip.text: root.path

    Loader {
        anchors.fill: parent
        sourceComponent: root.displayMode === 1 ? listVisual : gridVisual
    }

    Component {
        id: gridVisual
        Item {
            Rectangle {
                anchors.fill: parent
                color: tileMouse.containsMouse ? Theme.paperWhite : "transparent"
                radius: Theme.radius
                border.width: 1
                border.color: tileMouse.containsMouse ? Theme.opticalGray : "transparent"
            }
            Rectangle {
                id: imageWell
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 6
                height: Math.round(width * 0.75)
                color: Theme.softHover
                border.color: Theme.opticalGray
                border.width: 1
                clip: true
                Image {
                    id: gridPreview
                    anchors.fill: parent
                    // Keep both URL and requested size stable while the GridView moves. Changing
                    // either one cancels the current async response and briefly clears the texture,
                    // which appears as periodic flashing during a long scroll.
                    source: root.thumbnailUrl
                    visible: !root.directory
                    asynchronous: true
                    cache: true
                    sourceSize: Qt.size(384, 384)
                    fillMode: Image.PreserveAspectCrop
                }
                Image {
                    id: gridFolderIcon
                    objectName: "gridFolderIcon"
                    anchors.centerIn: parent
                    width: Math.round(Math.min(imageWell.width, imageWell.height) * 0.58)
                    height: width
                    source: root.thumbnailUrl
                    visible: root.directory
                    asynchronous: true
                    cache: true
                    sourceSize: Qt.size(192, 192)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }
            }
            Text {
                id: gridName
                objectName: "gridFileName"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: imageWell.bottom
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 5
                text: root.fileName
                elide: Text.ElideMiddle
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 13
            }
            MetadataStrip {
                visible: root.displayMode !== 2
                anchors.left: gridName.left
                anchors.right: gridName.right
                anchors.top: gridName.bottom
                anchors.topMargin: 1
                typeText: root.fileType
                imageDimensions: root.dimensions
                sourceBitDepth: root.bitDepth
                sizeText: root.fileSizeText
                directory: root.directory
                labelObjectName: "gridTechnicalLabel"
                badgeObjectName: "gridTypeBadge"
            }
        }
    }

    Component {
        id: listVisual
        Item {
            Rectangle {
                anchors.fill: parent
                color: tileMouse.containsMouse ? Theme.paperWhite : "transparent"
                radius: Theme.radius
                border.width: 1
                border.color: tileMouse.containsMouse ? Theme.opticalGray : "transparent"
            }
            Rectangle {
                id: listImageWell
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 72
                height: 52
                color: Theme.softHover
                border.color: Theme.opticalGray
                border.width: 1
                clip: true
                Image {
                    id: listPreview
                    anchors.fill: parent
                    source: root.thumbnailUrl
                    visible: !root.directory
                    asynchronous: true
                    cache: true
                    sourceSize: Qt.size(256, 256)
                    fillMode: Image.PreserveAspectCrop
                }
                Image {
                    id: listFolderIcon
                    objectName: "listFolderIcon"
                    anchors.centerIn: parent
                    width: 36
                    height: 36
                    source: root.thumbnailUrl
                    visible: root.directory
                    asynchronous: true
                    cache: true
                    sourceSize: Qt.size(128, 128)
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                }
            }
            Text {
                id: listName
                anchors.left: listImageWell.right
                anchors.leftMargin: 14
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.top: parent.top
                anchors.topMargin: 15
                text: root.fileName
                elide: Text.ElideMiddle
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            MetadataStrip {
                anchors.left: listName.left
                anchors.right: listName.right
                anchors.top: listName.bottom
                anchors.topMargin: 2
                typeText: root.fileType
                imageDimensions: root.dimensions
                sourceBitDepth: root.bitDepth
                sizeText: root.fileSizeText
                directory: root.directory
                labelObjectName: "listTechnicalLabel"
                badgeObjectName: "listTypeBadge"
            }
        }
    }

    Rectangle {
        visible: root.selected
        width: 2
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: Theme.probeBlue
    }

    Rectangle {
        visible: root.selected
        x: root.displayMode === 1 ? 12 : 14
        y: root.displayMode === 1 ? 10 : 14
        width: 18
        height: 18
        radius: 9
        color: Theme.probeBlue
        Text {
            anchors.centerIn: parent
            text: root.selectionOrdinal > 0 ? root.selectionOrdinal : "✓"
            color: "white"
            font.family: Theme.monoFont
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }
    }

    DropArea {
        id: folderDropArea
        anchors.fill: parent
        enabled: root.directory
        onEntered: function(drag) {
            if (drag.hasUrls)
                drag.acceptProposedAction()
        }
        onDropped: function(drop) {
            if (!drop.hasUrls)
                return
            root.controller.copyDroppedUrlsInto(drop.urls, root.path)
            drop.acceptProposedAction()
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 3
            visible: folderDropArea.containsDrag
            radius: Theme.radius
            color: "#197893A6"
            border.color: Theme.primaryButton
            border.width: 2
        }
    }

    MouseArea {
        id: tileMouse
        objectName: "thumbnailMouseArea"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        onClicked: function (mouse) {
            if (mouse.button === Qt.RightButton) {
                if (!root.selected)
                    root.selectionRequested(false, false);
                if (root.directory)
                    folderContextMenu.popup();
                else if (root.path.toLowerCase().endsWith(".raw") ||
                         root.path.toLowerCase().endsWith(".yuv"))
                    rawContextMenu.popup();
                else
                    imageContextMenu.popup();
                return;
            }
            const toggle = (mouse.modifiers & Qt.ControlModifier) || (mouse.modifiers & Qt.MetaModifier);
            const extend = (mouse.modifiers & Qt.ShiftModifier);
            root.selectionRequested(extend, toggle);
        }
        onDoubleClicked: root.activated()
    }

    DragHandler {
        id: tileDragHandler
        acceptedButtons: Qt.LeftButton
        target: null
        enabled: root.path.length > 0
        onActiveChanged: {
            if (active && !root.selected)
                root.selectionRequested(false, false);
        }
    }

    AppMenu {
        id: imageContextMenu
        AppMenuItem {
            text: qsTr("Open full screen")
            onTriggered: root.activated()
        }
        AppMenuItem {
            text: qsTr("Compare selected")
            enabled: root.workspaceController.canCompare
            onTriggered: root.workspaceController.compareSelected()
        }
        AppMenuItem {
            visible: root.controller.canRestoreSelected
            text: qsTr("Restore original")
            onTriggered: root.controller.restoreSelected()
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Cut")
            shortcutText: Qt.platform.os === "osx" ? "⌘X" : "Ctrl+X"
            onTriggered: root.controller.copySelected(true)
        }
        AppMenuItem {
            text: qsTr("Copy")
            shortcutText: Qt.platform.os === "osx" ? "⌘C" : "Ctrl+C"
            onTriggered: root.controller.copySelected(false)
        }
        AppMenuItem {
            text: qsTr("Rename…")
            enabled: root.controller.selectionCount === 1
            onTriggered: root.controller.renameSelected()
        }
        AppMenuItem {
            text: qsTr("Move to Trash")
            destructive: true
            onTriggered: root.controller.moveSelectedToTrash()
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Reveal in Finder / Explorer")
            enabled: root.controller.selectionCount === 1
            onTriggered: root.controller.revealSelected()
        }
        AppMenuItem {
            text: qsTr("Properties")
            enabled: root.controller.selectionCount === 1
            onTriggered: root.controller.showSelectedProperties()
        }
    }

    AppMenu {
        id: rawContextMenu
        AppMenuItem { text: qsTr("Open full screen"); onTriggered: root.activated() }
        AppMenuItem {
            text: qsTr("Compare selected")
            enabled: root.workspaceController.canCompare
            onTriggered: root.workspaceController.compareSelected()
        }
        AppMenuItem {
            text: qsTr("RAW/YUV parameters…")
            enabled: root.controller.canEditRaw
            onTriggered: root.controller.editSelectedRawParameters()
        }
        AppMenuItem {
            visible: root.controller.canRestoreSelected
            text: qsTr("Restore original")
            onTriggered: root.controller.restoreSelected()
        }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Cut"); shortcutText: Qt.platform.os === "osx" ? "⌘X" : "Ctrl+X"; onTriggered: root.controller.copySelected(true) }
        AppMenuItem { text: qsTr("Copy"); shortcutText: Qt.platform.os === "osx" ? "⌘C" : "Ctrl+C"; onTriggered: root.controller.copySelected(false) }
        AppMenuItem { text: qsTr("Rename…"); enabled: root.controller.selectionCount === 1; onTriggered: root.controller.renameSelected() }
        AppMenuItem { text: qsTr("Move to Trash"); destructive: true; onTriggered: root.controller.moveSelectedToTrash() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Reveal in Finder / Explorer"); enabled: root.controller.selectionCount === 1; onTriggered: root.controller.revealSelected() }
        AppMenuItem { text: qsTr("Properties"); enabled: root.controller.selectionCount === 1; onTriggered: root.controller.showSelectedProperties() }
    }

    AppMenu {
        id: folderContextMenu
        AppMenuItem { text: qsTr("Open folder"); onTriggered: root.activated() }
        AppMenuItem { text: qsTr("Paste into folder"); enabled: root.controller.canPaste; onTriggered: root.controller.pasteItemsInto(root.path) }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Cut"); shortcutText: Qt.platform.os === "osx" ? "⌘X" : "Ctrl+X"; onTriggered: root.controller.copySelected(true) }
        AppMenuItem { text: qsTr("Copy"); shortcutText: Qt.platform.os === "osx" ? "⌘C" : "Ctrl+C"; onTriggered: root.controller.copySelected(false) }
        AppMenuItem { text: qsTr("Rename…"); enabled: root.controller.selectionCount === 1; onTriggered: root.controller.renameSelected() }
        AppMenuItem { text: qsTr("Move to Trash"); destructive: true; onTriggered: root.controller.moveSelectedToTrash() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Reveal in Finder / Explorer"); enabled: root.controller.selectionCount === 1; onTriggered: root.controller.revealSelected() }
        AppMenuItem { text: qsTr("Properties"); enabled: root.controller.selectionCount === 1; onTriggered: root.controller.showSelectedProperties() }
    }
}
