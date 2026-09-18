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
    property var settingsController: null
    property bool active: workspaceController.activePaneIndex === paneIndex
    property bool contentInteractionEnabled: true
    property string iconPrefix: Theme.iconPrefix
    property int displayMode: controller.displayMode
    signal newFolderRequested

    color: Theme.sensorWhite
    border.width: 1
    border.color: active ? Theme.accentBorder : Theme.opticalGray
    clip: true

    function activate() { workspaceController.activatePane(paneIndex) }

    function locationLabel(path) {
        const parts = String(path).split(/[\\/]/).filter(function(part) { return part.length > 0 })
        return parts.length > 0 ? parts[parts.length - 1] : String(path)
    }

    function submitLocation() {
        root.activate()
        const error = root.controller.navigateToTypedPath(locationField.text)
        locationField.locationError = error
        if (error.length === 0) {
            locationField.text = root.controller.currentDirectory
            locationField.focus = false
        } else {
            locationField.forceActiveFocus()
            locationField.selectAll()
        }
    }

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
        height: 38
        color: root.active ? Theme.raisedSurface : Theme.paperWhite

        MouseArea {
            anchors.fill: parent
            onClicked: root.activate()
        }

        Row {
            id: navigationButtons
            anchors.left: parent.left
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0

            AppIconButton {
                objectName: "paneBackButton-" + root.paneIndex
                width: 26
                height: 28
                controlSize: 26
                renderedIconSize: 16
                enabled: root.controller.canGoBack
                iconSource: root.iconPrefix + "back.svg"
                toolTipText: qsTr("Back")
                onClicked: {
                    root.activate()
                    root.controller.navigateBack()
                }
            }
            AppIconButton {
                objectName: "paneForwardButton-" + root.paneIndex
                width: 26
                height: 28
                controlSize: 26
                renderedIconSize: 16
                enabled: root.controller.canGoForward
                iconSource: root.iconPrefix + "forward.svg"
                toolTipText: qsTr("Forward")
                onClicked: {
                    root.activate()
                    root.controller.navigateForward()
                }
            }
            AppIconButton {
                objectName: "paneUpButton-" + root.paneIndex
                width: 26
                height: 28
                controlSize: 26
                renderedIconSize: 16
                enabled: root.controller.canGoUp
                iconSource: root.iconPrefix + "up.svg"
                toolTipText: qsTr("Parent folder")
                onClicked: {
                    root.activate()
                    root.controller.navigateUp()
                }
            }
        }

        Rectangle {
            id: locationFrame
            objectName: "paneLocationFrame-" + root.paneIndex
            anchors.left: navigationButtons.right
            anchors.leftMargin: 4
            anchors.right: selectionCount.visible ? selectionCount.left : closeButton.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            height: 28
            radius: 5
            color: Theme.searchFieldSurface
            border.width: 1
            border.color: locationField.locationError.length > 0 ? Theme.danger
                        : locationField.activeFocus ? Theme.searchFieldFocusBorder
                        : locationHover.hovered ? Theme.searchFieldHoverBorder
                                                : Theme.searchFieldBorder

            HoverHandler { id: locationHover }

            TextField {
                id: locationField
                objectName: "paneLocationField-" + root.paneIndex
                property string locationError: ""
                anchors.fill: parent
                anchors.rightMargin: 25
                placeholderText: qsTr("Enter a folder path")
                selectByMouse: true
                leftPadding: 9
                rightPadding: 5
                topPadding: 0
                bottomPadding: 0
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.graphiteInk
                selectionColor: Theme.probeBlue
                selectedTextColor: "white"
                font.family: Theme.monoFont
                font.pixelSize: 11
                font.weight: Font.DemiBold
                background: Item {}
                onTextEdited: locationError = ""
                onAccepted: root.submitLocation()
                Keys.onEscapePressed: function(event) {
                    text = root.controller.currentDirectory
                    locationError = ""
                    focus = false
                    event.accepted = true
                }
                ToolTip.visible: activeFocus && locationError.length > 0
                ToolTip.text: locationError
                ToolTip.delay: 0
            }

            Binding {
                target: locationField
                property: "text"
                value: root.controller.currentDirectory
                when: !locationField.activeFocus
                restoreMode: Binding.RestoreBindingOrValue
            }

            AppIconButton {
                id: locationDropButton
                objectName: "paneLocationDropButton-" + root.paneIndex
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 25
                height: 26
                controlSize: 25
                renderedIconSize: 14
                enabled: root.controller.recentLocations.length > 0
                checked: locationPopup.opened
                iconSource: root.iconPrefix + "chevron-down.svg"
                toolTipText: qsTr("Recent folders")
                onClicked: {
                    root.activate()
                    if (locationPopup.opened)
                        locationPopup.close()
                    else
                        locationPopup.open()
                }
            }

            Connections {
                target: root.controller
                function onCurrentDirectoryChanged() {
                    locationField.locationError = ""
                }
            }

            Popup {
                id: locationPopup
                objectName: "paneLocationPopup-" + root.paneIndex
                x: 0
                y: locationFrame.height + 4
                width: Math.max(locationFrame.width, Math.min(360, root.width - 8))
                height: Math.min(286, recentLocationColumn.implicitHeight + 10)
                padding: 5
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                background: Rectangle {
                    color: Theme.raisedSurface
                    border.color: Theme.searchFieldBorder
                    radius: 6
                }

                contentItem: Flickable {
                    contentWidth: width
                    contentHeight: recentLocationColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: recentLocationColumn
                        width: parent.width
                        spacing: 2

                        Text {
                            width: parent.width
                            height: 25
                            leftPadding: 8
                            verticalAlignment: Text.AlignVCenter
                            text: qsTr("Recent folders")
                            color: Theme.mutedInk
                            font.family: Theme.uiFont
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }

                        Repeater {
                            model: root.controller.recentLocations
                            delegate: Button {
                                id: recentLocationButton
                                required property string modelData
                                width: recentLocationColumn.width
                                height: 42
                                hoverEnabled: true

                                background: Rectangle {
                                    color: recentLocationButton.hovered || recentLocationButton.activeFocus
                                           ? Theme.softHover : "transparent"
                                    radius: 4
                                }
                                contentItem: Item {
                                    Column {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 1
                                        Text {
                                            width: parent.width
                                            text: root.locationLabel(modelData)
                                            elide: Text.ElideRight
                                            color: Theme.graphiteInk
                                            font.family: Theme.uiFont
                                            font.pixelSize: 12
                                            font.weight: Font.Medium
                                        }
                                        Text {
                                            width: parent.width
                                            text: modelData
                                            elide: Text.ElideMiddle
                                            color: Theme.mutedInk
                                            font.family: Theme.monoFont
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                                onClicked: {
                                    root.activate()
                                    root.controller.openDirectory(modelData)
                                    locationPopup.close()
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            height: 1
                            color: Theme.opticalGray
                        }
                        Button {
                            id: clearRecentButton
                            objectName: "clearRecentLocations-" + root.paneIndex
                            width: parent.width
                            height: 29
                            flat: true
                            text: qsTr("Clear recent folders")
                            onClicked: {
                                root.controller.clearRecentLocations()
                                locationPopup.close()
                            }
                            background: Rectangle {
                                color: clearRecentButton.hovered ? Theme.softHover : "transparent"
                                radius: 4
                            }
                            contentItem: Text {
                                text: clearRecentButton.text
                                color: Theme.mutedInk
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: Text.AlignLeft
                                leftPadding: 8
                                font.family: Theme.uiFont
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }

        Text {
            id: selectionCount
            visible: root.width >= 520 && root.controller.selectionCount > 0
            anchors.right: closeButton.left
            anchors.rightMargin: 7
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%1 selected").arg(root.controller.selectionCount)
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
                settingsController: root.settingsController
                contentInteractionEnabled: root.contentInteractionEnabled
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
