pragma ComponentBehavior: Bound
// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import "."

Rectangle {
    id: root
    objectName: "browserPane-" + paneIndex
    required property var controller
    required property var workspaceController
    required property int paneIndex
    property bool active: workspaceController.activePaneIndex === paneIndex
    property bool contentInteractionEnabled: true
    property string iconPrefix: Theme.iconPrefix
    property int displayMode: controller.displayMode
    readonly property string platformFolderIcon:
        iconPrefix + (Qt.platform.os === "osx" ? "macos-folder.svg"
                                               : Qt.platform.os === "windows"
                                                 ? "windows-folder.svg" : "folder.svg")
    signal newFolderRequested

    color: Theme.sensorWhite
    border.width: 1
    border.color: active ? Theme.accentBorder : Theme.opticalGray
    clip: true

    function activate() { workspaceController.activatePane(paneIndex) }

    Rectangle {
        id: focusRail
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 2
        color: Theme.probeBlue
        opacity: root.active ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.fast } }
    }

    Rectangle {
        id: paneHeader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: focusRail.bottom
        height: 32
        color: root.active ? Theme.raisedSurface : Theme.paperWhite

        Image {
            objectName: "paneFolderIcon-" + root.paneIndex
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            source: root.controller.currentDirectory.length > 0
                    ? root.platformFolderIcon
                    : root.iconPrefix + "folder-pane-plus.svg"
            sourceSize: Qt.size(32, 32)
            opacity: root.controller.currentDirectory.length > 0 ? 0.78 : 0.58
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 33
            anchors.right: selectionCount.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.controller.currentDirectory.length > 0
                  ? root.controller.currentDirectory : "New file manager"
            elide: Text.ElideMiddle
            color: Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 12
            font.weight: root.active ? Font.DemiBold : Font.Medium
        }
        Text {
            id: selectionCount
            anchors.right: closeButton.left
            anchors.rightMargin: 7
            anchors.verticalCenter: parent.verticalCenter
            text: root.controller.selectionCount > 0 ? root.controller.selectionCount + " selected" : ""
            color: Theme.mutedInk
            font.family: Theme.monoFont
            font.pixelSize: Theme.metadataFontSize
        }
        AppIconButton {
            id: closeButton
            objectName: "closePaneButton-" + root.paneIndex
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            width: 26
            height: 26
            iconSource: root.iconPrefix + "close.svg"
            toolTipText: qsTr("Close file manager")
            onClicked: root.workspaceController.closePane(root.paneIndex)
        }
        MouseArea {
            anchors.left: parent.left
            anchors.right: closeButton.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            onClicked: root.activate()
        }
    }

    Item {
        id: blankState
        visible: root.controller.currentDirectory.length === 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: paneHeader.bottom
        anchors.bottom: parent.bottom

        Column {
            anchors.centerIn: parent
            width: Math.min(parent.width - 32, 300)
            spacing: 10
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 34
                height: 34
                source: root.iconPrefix + "folder-pane-plus.svg"
                sourceSize: Qt.size(68, 68)
                opacity: 0.58
            }
            Text {
                width: parent.width
                text: blankDropArea.containsDrag ? qsTr("Drop to open here")
                      : root.active ? qsTr("Choose a folder from the sidebar")
                                    : qsTr("Select this area to choose a folder")
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: root.active ? "The file tree and toolbar now control this area"
                                  : "Its folder and view settings stay independent"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: 11
            }
        }
        MouseArea {
            anchors.fill: parent
            onClicked: root.activate()
        }
        DropArea {
            id: blankDropArea
            anchors.fill: parent
            onEntered: function(drag) {
                if (drag.hasUrls) {
                    root.activate()
                    drag.acceptProposedAction()
                }
            }
            onDropped: function(drop) {
                if (drop.hasUrls) {
                    root.controller.openDroppedUrls(drop.urls)
                    drop.acceptProposedAction()
                }
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 7
                visible: blankDropArea.containsDrag
                radius: 6
                color: "#127893A6"
                border.color: Theme.primaryButton
                border.width: 1
            }
        }
    }

    DropArea {
        id: paneDropArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: paneHeader.bottom
        anchors.bottom: parent.bottom
        enabled: root.controller.currentDirectory.length > 0
        onEntered: function(drag) {
            if (drag.hasUrls) {
                root.activate()
                drag.acceptProposedAction()
            }
        }
        onDropped: function(drop) {
            if (drop.hasUrls) {
                root.controller.copyDroppedUrls(drop.urls)
                drop.acceptProposedAction()
            }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 7
            visible: paneDropArea.containsDrag
            color: "#0A7893A6"
            border.color: Theme.primaryButton
            border.width: 1
            radius: 6
        }
    }

    GridView {
        id: contactSheet
        objectName: "paneContactSheet-" + root.paneIndex
        visible: root.controller.currentDirectory.length > 0
        z: 1
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: paneHeader.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 10
        clip: true
        model: root.controller.thumbnails
        readonly property int visualCellWidth: Math.min(
            root.controller.gridCellWidth,
            width >= 380 ? Math.max(144, Math.floor((width - 24) / 2))
                         : Math.max(144, width - 16))
        cellWidth: root.displayMode === 1 ? width : visualCellWidth + 12
        cellHeight: root.displayMode === 1 ? 76 : Math.round(visualCellWidth * 0.75) + 62
        boundsBehavior: Flickable.StopAtBounds
        interactive: root.contentInteractionEnabled
        reuseItems: true
        cacheBuffer: Math.max(0, height)
        keyNavigationEnabled: root.active
        onInteractiveChanged: {
            if (!interactive)
                cancelFlick()
        }
        delegate: Item {
            required property string path
            required property string fileName
            required property string technicalLabel
            required property string fileType
            required property var dimensions
            required property int bitDepth
            required property string fileSizeText
            required property url thumbnailUrl
            required property bool isDirectory
            required property bool isSelected
            required property int selectionOrdinal
            width: contactSheet.cellWidth
            height: contactSheet.cellHeight
            ThumbnailTile {
                width: root.displayMode === 1 ? Math.min(parent.width - 12, 760)
                                              : contactSheet.visualCellWidth
                height: root.displayMode === 1 ? 68 : parent.height - 6
                displayMode: root.displayMode
                controller: root.controller
                workspaceController: root.workspaceController
                path: parent.path
                fileName: parent.fileName
                technicalLabel: parent.technicalLabel
                fileType: parent.fileType
                dimensions: parent.dimensions
                bitDepth: parent.bitDepth
                fileSizeText: parent.fileSizeText
                thumbnailUrl: parent.thumbnailUrl
                directory: parent.isDirectory
                selected: parent.isSelected
                selectionOrdinal: parent.selectionOrdinal
                onSelectionRequested: function(extend, toggle) {
                    root.workspaceController.selectPath(root.paneIndex, path, extend, toggle)
                }
                onActivated: {
                    root.activate()
                    root.controller.activatePath(path)
                }
            }
        }

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Text {
            anchors.centerIn: parent
            visible: contactSheet.count === 0
            text: qsTr("No supported images in this folder\nChoose another folder or drop images here")
            horizontalAlignment: Text.AlignHCenter
            color: Theme.mutedInk
            font.family: Theme.uiFont
            font.pixelSize: 12
            lineHeight: 1.45
        }

        MouseArea {
            anchors.fill: parent
            enabled: root.contentInteractionEnabled
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.BackButton | Qt.ForwardButton
            propagateComposedEvents: true
            onClicked: function(mouse) {
                root.activate()
                if (mouse.button === Qt.BackButton) {
                    root.controller.navigateBack()
                    return
                }
                if (mouse.button === Qt.ForwardButton) {
                    root.controller.navigateForward()
                    return
                }
                const itemIndex = contactSheet.indexAt(mouse.x + contactSheet.contentX,
                                                       mouse.y + contactSheet.contentY)
                const listBlank = root.displayMode === 1 &&
                                  mouse.x > Math.min(contactSheet.width - 12, 760)
                if ((itemIndex < 0 || listBlank) && mouse.button === Qt.RightButton) {
                    workspaceMenu.popup(mouse.x, mouse.y)
                    return
                }
                if (itemIndex < 0 || listBlank) root.controller.clearSelection()
                mouse.accepted = listBlank
            }
        }
    }

    AppMenu {
        id: workspaceMenu
        AppMenuItem {
            text: Qt.platform.os === "osx" ? qsTr("Open in Finder")
                                          : qsTr("Open in File Explorer")
            onTriggered: root.controller.openCurrentDirectoryInFileManager()
        }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Refresh"); onTriggered: root.controller.refresh() }
        AppMenuItem { text: qsTr("Select all"); onTriggered: root.controller.selectAll() }
        AppMenuItem { text: qsTr("Paste"); enabled: root.controller.canPaste; onTriggered: root.controller.pasteItems() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("New folder…"); onTriggered: root.newFolderRequested() }
    }
}
