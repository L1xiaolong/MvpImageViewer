pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "."

Rectangle {
    id: root
    required property var controller
    required property var workspaceController
    required property int paneIndex
    property var settingsController: null
    property var searchReturnTarget: null
    property string iconPrefix: Theme.iconPrefix
    readonly property bool multiplePanes: workspaceController.paneCount !== undefined && workspaceController.paneCount > 1
    readonly property bool active: workspaceController.activePaneIndex === paneIndex
    readonly property bool searchVisible: searchPopup.visible
    readonly property bool searchInputFocused: paneSearch.activeFocus
    objectName: "paneHeader-" + paneIndex
    function activate() { workspaceController.activatePane(paneIndex) }
    function locationLabel(path) {
        const parts = String(path).split(/[\\/]/).filter(function(part) { return part.length > 0 })
        return parts.length > 0 ? parts[parts.length - 1] : String(path)
    }
    function submitLocation() {
        activate()
        const error = controller.navigateToTypedPath(locationField.text)
        locationField.locationError = error
        if (error.length === 0) { locationField.text = controller.currentDirectory; locationField.focus = false }
        else { locationField.forceActiveFocus(); locationField.selectAll() }
    }
    function focusSearch() {
        if (controller.currentDirectory.length === 0) return
        activate()
        searchPopup.open()
        paneSearch.forceActiveFocus()
        paneSearch.selectAll()
    }
    function closeSearch() {
        searchPopup.close()
        // Popup's default restoration can focus a different pane's search.
        // Return explicitly to the file view belonging to this header.
        if (root.visible && searchReturnTarget && searchReturnTarget.visible) {
            activate()
            searchReturnTarget.forceActiveFocus(Qt.ShortcutFocusReason)
        }
    }
    onVisibleChanged: if (!visible) searchPopup.close()
        height: 38
        color: root.multiplePanes && root.active ? Theme.activePaneHeader : Theme.sensorWhite

        MouseArea {
            anchors.fill: parent
            onClicked: root.activate()
        }

        Row {
            id: navigationButtons
            y: 5
            anchors.left: parent.left
            anchors.leftMargin: 4
            spacing: 0

            AppIconButton {
                objectName: "paneBackButton-" + root.paneIndex
                width: 26
                height: 28
                controlSize: 26
                renderedIconSize: 16
                enabled: root.controller.canGoBack
                iconSource: root.iconPrefix + "back.svg"
                toolTipText: qsTranslate("BrowserPane", "Back")
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
                toolTipText: qsTranslate("BrowserPane", "Forward")
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
                toolTipText: qsTranslate("BrowserPane", "Parent folder")
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
            anchors.verticalCenter: navigationButtons.verticalCenter
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
                placeholderText: qsTranslate("BrowserPane", "Enter a folder path")
                selectByMouse: true
                leftPadding: 9
                rightPadding: 5
                topPadding: 0
                bottomPadding: 0
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.graphiteInk
                selectionColor: Theme.probeBlue
                selectedTextColor: "white"
                font.family: Theme.uiFont
                font.pixelSize: 13
                font.weight: Font.Normal
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
                restoreMode: Binding.RestoreNone
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
                toolTipText: qsTranslate("BrowserPane", "Recent folders")
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
                            text: qsTranslate("BrowserPane", "Recent folders")
                            color: Theme.mutedInk
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.metadataFontSize
                            font.weight: Theme.emphasisFontWeight
                        }

                        Repeater {
                            model: root.controller.recentLocations
                            delegate: Button {
                                id: recentLocationButton
                                required property string modelData
                                width: recentLocationColumn.width
                                height: 42
                                hoverEnabled: true
                                ToolTip.visible: hovered
                                ToolTip.delay: 500
                                ToolTip.text: modelData

                                background: Rectangle {
                                    color: recentLocationButton.hovered || recentLocationButton.visualFocus
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
                            text: qsTranslate("BrowserPane", "Clear recent folders")
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

        Popup {
            id: searchPopup
            objectName: "paneSearchPopup-" + root.paneIndex
            parent: root
            popupType: Popup.Item
            x: Math.max(8, root.width - width - 8)
            y: root.height + 6
            width: Math.max(0, Math.min(360, root.width - 16))
            height: 44
            padding: 6
            modal: false
            dim: false
            focus: true
            closePolicy: Popup.NoAutoClose
            onOpened: { paneSearch.forceActiveFocus(); paneSearch.selectAll() }
            onClosed: {
                searchDebounce.stop()
                paneSearch.clear()
                if (root.controller) root.controller.setFilterText("")
            }
            background: Rectangle {
                radius: 6
                color: Theme.raisedSurface
                border.width: 1
                border.color: Theme.searchFieldBorder
            }
            contentItem: Item {
                TextField {
                    id: paneSearch
                    objectName: "paneSearchField-" + root.paneIndex
                    anchors.left: parent.left
                    anchors.right: closeSearchButton.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    height: 30
                    enabled: root.controller.currentDirectory.length > 0
                    selectByMouse: true
                    placeholderText: qsTranslate("TopToolbar.ui", "Search files")
                    Accessible.name: placeholderText
                    leftPadding: 28
                    rightPadding: 8
                    topPadding: 0
                    bottomPadding: 0
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.graphiteInk
                    placeholderTextColor: Theme.faintInk
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    onActiveFocusChanged: if (activeFocus) root.activate()
                    onTextEdited: { root.activate(); searchDebounce.restart() }
                    Keys.onEscapePressed: function(event) {
                        root.closeSearch()
                        event.accepted = true
                    }
                    background: Rectangle {
                        radius: 4
                        color: Theme.searchFieldSurface
                        border.width: paneSearch.activeFocus ? 2 : 1
                        border.color: paneSearch.activeFocus ? Theme.searchFieldFocusBorder
                            : paneSearch.hovered ? Theme.searchFieldHoverBorder : Theme.searchFieldBorder
                        Image {
                            x: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 14; height: 14
                            source: root.iconPrefix + "search.svg"
                        }
                    }
                }
                AppIconButton {
                    id: closeSearchButton
                    objectName: "paneCloseSearchButton-" + root.paneIndex
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    controlSize: 28
                    renderedIconSize: 14
                    iconSource: root.iconPrefix + "close.svg"
                    toolTipText: qsTr("Close search") + " · Esc"
                    onClicked: root.closeSearch()
                }
            }
        }
        Shortcut {
            sequence: "Esc"
            enabled: root.visible && root.active && searchPopup.visible
            onActivated: root.closeSearch()
        }
        Timer {
            id: searchDebounce
            interval: 120
            onTriggered: root.controller.setFilterText(paneSearch.text)
        }
        Connections {
            target: root.controller
            function onFilterTextChanged() {
                if (paneSearch.text !== root.controller.filterText) paneSearch.text = root.controller.filterText
            }
        }
        Component.onCompleted: paneSearch.text = root.controller.filterText

        Text {
            id: selectionCount
            visible: root.multiplePanes && root.width >= 520 && root.controller.selectionCount > 0
            anchors.right: closeButton.left
            anchors.rightMargin: 7
            anchors.verticalCenter: navigationButtons.verticalCenter
            text: qsTranslate("BrowserPane", "%1 selected").arg(root.controller.selectionCount)
            color: Theme.mutedInk
            font.family: Theme.monoFont
            font.pixelSize: Theme.metadataFontSize
        }
        AppIconButton {
            id: closeButton
            objectName: "closePaneButton-" + root.paneIndex
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: navigationButtons.verticalCenter
            width: 26
            height: 26
            iconSource: root.iconPrefix + "close.svg"
            toolTipText: qsTranslate("BrowserPane", "Close browser pane")
            onClicked: root.workspaceController.closePane(root.paneIndex)
        }
}
