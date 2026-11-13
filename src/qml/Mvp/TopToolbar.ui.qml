import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

Rectangle {
    id: root
    width: 1440
    height: Theme.toolbarHeight
    color: Theme.raisedSurface

    readonly property int toolbarButtonSize: 28
    readonly property int toolbarIconSize: 16

    property string iconPrefix: Theme.iconPrefix
    property int displayMode: 0
    property int sortMode: 0
    property string searchShortcutText: Qt.platform.os === "osx" ? "⌘F" : "Ctrl+F"
    property string openFolderShortcutText: ""
    property string newFolderShortcutText: ""
    property string compareShortcutText: ""
    property string settingsShortcutText: ""
    property bool settingsVisible: false
    property bool compareEnabled: false
    property bool activePaneAvailable: true
    property bool activeDirectoryAvailable: true
    property bool canAddPane: true
    property bool gridEnabled: true
    property bool galleryEnabled: true
    property bool transformEnabled: false
    property bool smoothDisplay: true
    property real navigationWidth: Theme.sidebarWidth
    property real captionButtonsWidth: 0
    readonly property rect leftControlsBounds: Qt.rect(0, 0, dragSpace.x + 16, height)
    readonly property rect rightControlsBounds: settingsVisible ? Qt.rect(settingsButton.x + 16, 0,
                                                       settingsButton.width, height) : Qt.rect(0, 0, 0, 0)

    property alias openFolderControl: openFolderButton
    property alias newFolderControl: newFolderButton
    property alias folderCompareControl: folderCompareButton
    property alias gridControl: gridModeButton
    property alias listControl: listModeButton
    property alias galleryControl: galleryModeButton
    property alias sortControl: sortButton
    property alias compareControl: compareButton
    property alias transformControl: transformButton
    property alias smoothDisplayControl: smoothDisplayButton
    property alias settingsControl: settingsButton

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16 + root.captionButtonsWidth
        spacing: 4
        RowLayout {
            spacing: 2
            AppIconButton {
                id: openFolderButton
                objectName: "openFolderButton"
                controlSize: root.toolbarButtonSize
                renderedIconSize: root.toolbarIconSize
                enabled: root.activePaneAvailable
                iconSource: root.iconPrefix + "folder-open.svg"
                toolTipText: qsTr("Open folder")
                shortcutText: root.openFolderShortcutText
            }
            AppIconButton {
                id: newFolderButton
                objectName: "newFolderButton"
                controlSize: root.toolbarButtonSize
                renderedIconSize: root.toolbarIconSize
                enabled: root.activePaneAvailable && root.activeDirectoryAvailable
                iconSource: root.iconPrefix + "folder-plus.svg"
                toolTipText: qsTr("New folder")
                shortcutText: root.newFolderShortcutText
            }
            AppIconButton {
                id: folderCompareButton
                objectName: "folderCompareButton"
                controlSize: root.toolbarButtonSize
                renderedIconSize: root.toolbarIconSize
                enabled: root.canAddPane
                iconSource: root.iconPrefix + "folder-pane-plus.svg"
                toolTipText: enabled ? qsTr("Add browser pane") : qsTr("Maximum 4 browser panes")
            }
        }
        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 16
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            color: Theme.opticalGray
        }
        RowLayout {
            spacing: 2
            AppIconButton {
                id: gridModeButton
                objectName: "gridModeButton"
                controlSize: root.toolbarButtonSize
                renderedIconSize: root.toolbarIconSize
                checkable: true
                checked: root.displayMode === 0
                enabled: root.activePaneAvailable && root.activeDirectoryAvailable && root.gridEnabled
                iconSource: root.iconPrefix + "grid.svg"
                toolTipText: root.gridEnabled ? qsTr("Grid view") : qsTr("Grid view is available with up to 2 browser panes")
            }
            AppIconButton {
                id: listModeButton
                objectName: "listModeButton"
                controlSize: root.toolbarButtonSize
                renderedIconSize: root.toolbarIconSize
                checkable: true
                checked: root.displayMode === 1
                enabled: root.activePaneAvailable && root.activeDirectoryAvailable
                iconSource: root.iconPrefix + "list.svg"
                toolTipText: qsTr("List view")
            }
            AppIconButton {
                id: galleryModeButton
                objectName: "galleryModeButton"
                controlSize: root.toolbarButtonSize
                renderedIconSize: root.toolbarIconSize
                checkable: true
                checked: root.displayMode === 2
                enabled: root.activePaneAvailable && root.activeDirectoryAvailable && root.galleryEnabled
                iconSource: root.iconPrefix + "gallery.svg"
                toolTipText: root.galleryEnabled ? qsTr("Gallery view") : qsTr("Gallery view is available with 1 browser pane")
            }
        }
        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 16
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            color: Theme.opticalGray
        }
        RowLayout {
            spacing: 2
            AppIconButton {
                id: sortButton
                objectName: "sortButton"
                controlSize: root.toolbarButtonSize
                renderedIconSize: root.toolbarIconSize
                enabled: root.activePaneAvailable && root.activeDirectoryAvailable
                iconSource: root.iconPrefix + "sort.svg"
                toolTipText: [qsTr("Sort by name"), qsTr("Sort by modified time"),
                              qsTr("Sort by file size"), qsTr("Sort by file type")][root.sortMode]
            }
            AppIconButton {
                id: smoothDisplayButton
                objectName: "smoothDisplayButton"
                controlSize: root.toolbarButtonSize
                renderedIconSize: root.toolbarIconSize
                checkable: true
                checked: root.smoothDisplay
                iconSource: root.iconPrefix + "smooth-display.svg"
                toolTipText: qsTr("Smooth display")
            }
            AppIconButton {
                id: transformButton
                objectName: "imageOperationsButton"
                controlSize: root.toolbarButtonSize
                renderedIconSize: root.toolbarIconSize
                enabled: root.transformEnabled
                iconSource: root.iconPrefix + "image-resize.svg"
                toolTipText: enabled ? qsTr("Image operations") : qsTr("Select one image to edit")
            }
            AppIconButton {
                id: compareButton
                objectName: "compareButton"
                controlSize: root.toolbarButtonSize
                renderedIconSize: root.toolbarIconSize
                enabled: root.compareEnabled
                iconSource: root.iconPrefix + "compare.svg"
                toolTipText: enabled ? qsTr("Compare selected images") : qsTr("Select 2–4 images to compare")
                shortcutText: root.compareShortcutText
            }
        }
        Item { id: dragSpace; Layout.fillWidth: true; Layout.minimumWidth: 48 }
        AppIconButton {
            id: settingsButton
            objectName: "toolbarSettingsButton"
            visible: root.settingsVisible
            controlSize: root.toolbarButtonSize
            renderedIconSize: root.toolbarIconSize
            iconSource: root.iconPrefix + "settings.svg"
            toolTipText: qsTr("Settings")
            shortcutText: root.settingsShortcutText
        }

    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.opticalGray
    }
}
