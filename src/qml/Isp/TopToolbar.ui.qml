import QtQuick
import QtQuick.Controls
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
    property bool compareEnabled: false
    property bool activePaneAvailable: true
    property bool activeDirectoryAvailable: true
    property bool canAddPane: true
    property bool gridEnabled: true
    property bool galleryEnabled: true
    property bool transformEnabled: false
    property real navigationWidth: Theme.sidebarWidth

    property alias openFolderControl: openFolderButton
    property alias newFolderControl: newFolderButton
    property alias folderCompareControl: folderCompareButton
    property alias gridControl: gridModeButton
    property alias listControl: listModeButton
    property alias galleryControl: galleryModeButton
    property alias sortControl: sortButton
    property alias compareControl: compareButton
    property alias rotateClockwiseControl: rotateClockwiseButton
    property alias rotateCounterClockwiseControl: rotateCounterClockwiseButton
    property alias resizeImageControl: resizeImageButton
    property alias searchControl: fileSearch
    property alias clearSearchControl: clearMouse
    property alias settingsControl: settingsButton

    Text {
        id: productName
        anchors.left: parent.left
        anchors.right: brandDivider.left
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("MVP Image Viewer")
        horizontalAlignment: Text.AlignHCenter
        color: Theme.graphiteInk
        font.family: Theme.uiFont
        font.pixelSize: 17
        font.weight: Font.DemiBold
    }

    Rectangle {
        id: brandDivider
        x: Math.round(root.navigationWidth)
        anchors.verticalCenter: parent.verticalCenter
        width: 1
        height: 17
        color: Theme.opticalGray
    }

    AppIconButton {
        id: openFolderButton
        objectName: "openFolderButton"
        x: brandDivider.x + 14
        anchors.verticalCenter: parent.verticalCenter
        controlSize: root.toolbarButtonSize
        renderedIconSize: root.toolbarIconSize
        enabled: root.activePaneAvailable
        iconSource: root.iconPrefix + "folder-open.svg"
        toolTipText: qsTr("Open folder")
    }

    AppIconButton {
        id: newFolderButton
        objectName: "newFolderButton"
        x: brandDivider.x + 44
        anchors.verticalCenter: parent.verticalCenter
        controlSize: root.toolbarButtonSize
        renderedIconSize: root.toolbarIconSize
        enabled: root.activePaneAvailable && root.activeDirectoryAvailable
        iconSource: root.iconPrefix + "folder-plus.svg"
        toolTipText: qsTr("New folder")
    }

    AppIconButton {
        id: folderCompareButton
        objectName: "folderCompareButton"
        x: brandDivider.x + 74
        anchors.verticalCenter: parent.verticalCenter
        controlSize: root.toolbarButtonSize
        renderedIconSize: root.toolbarIconSize
        enabled: root.canAddPane
        iconSource: root.iconPrefix + "folder-pane-plus.svg"
        toolTipText: enabled ? qsTr("Add file manager") : qsTr("Maximum 4 file managers")
    }

    Rectangle {
        id: fileDivider
        x: brandDivider.x + 116
        anchors.verticalCenter: parent.verticalCenter
        width: 1
        height: 17
        color: Theme.opticalGray
    }

    AppIconButton {
        id: gridModeButton
        objectName: "gridModeButton"
        x: brandDivider.x + 130
        anchors.verticalCenter: parent.verticalCenter
        controlSize: root.toolbarButtonSize
        renderedIconSize: root.toolbarIconSize
        checkable: true
        checked: root.displayMode === 0
        enabled: root.activePaneAvailable && root.activeDirectoryAvailable && root.gridEnabled
        iconSource: root.iconPrefix + "grid.svg"
        toolTipText: root.gridEnabled ? "Grid view" : "Grid view is available with up to 2 file managers"
    }

    AppIconButton {
        id: listModeButton
        objectName: "listModeButton"
        x: brandDivider.x + 160
        anchors.verticalCenter: parent.verticalCenter
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
        x: brandDivider.x + 190
        anchors.verticalCenter: parent.verticalCenter
        controlSize: root.toolbarButtonSize
        renderedIconSize: root.toolbarIconSize
        checkable: true
        checked: root.displayMode === 2
        enabled: root.activePaneAvailable && root.activeDirectoryAvailable && root.galleryEnabled
        iconSource: root.iconPrefix + "gallery.svg"
        toolTipText: root.galleryEnabled ? "Gallery view" : "Gallery view is available with 1 file manager"
    }

    Rectangle {
        id: activeViewRail
        visible: root.activePaneAvailable && root.activeDirectoryAvailable
        x: root.displayMode === 0 ? brandDivider.x + 136 : root.displayMode === 1 ? brandDivider.x + 166 : brandDivider.x + 196
        y: 33
        width: 16
        height: 2
        radius: 1
        color: Theme.primaryButton
        Behavior on x {
            NumberAnimation {
                duration: 120
                easing.type: Easing.OutCubic
            }
        }
    }

    Rectangle {
        id: viewDivider
        x: brandDivider.x + 232
        anchors.verticalCenter: parent.verticalCenter
        width: 1
        height: 17
        color: Theme.opticalGray
    }

    AppIconButton {
        id: sortButton
        objectName: "sortButton"
        x: brandDivider.x + 246
        anchors.verticalCenter: parent.verticalCenter
        controlSize: root.toolbarButtonSize
        renderedIconSize: root.toolbarIconSize
        enabled: root.activePaneAvailable && root.activeDirectoryAvailable
        iconSource: root.iconPrefix + "sort.svg"
        toolTipText: [qsTr("Sort by name"), qsTr("Sort by modified time"),
                      qsTr("Sort by file size"), qsTr("Sort by file type")][root.sortMode]
    }

    AppIconButton {
        id: rotateCounterClockwiseButton
        objectName: "rotateCounterClockwiseButton"
        x: brandDivider.x + 276
        anchors.verticalCenter: parent.verticalCenter
        controlSize: root.toolbarButtonSize
        renderedIconSize: root.toolbarIconSize
        enabled: root.transformEnabled
        iconSource: root.iconPrefix + "rotate-ccw-square.svg"
        toolTipText: enabled ? qsTr("Rotate 90° counterclockwise")
                             : qsTr("Select one image to rotate")
    }

    AppIconButton {
        id: rotateClockwiseButton
        objectName: "rotateClockwiseButton"
        x: brandDivider.x + 306
        anchors.verticalCenter: parent.verticalCenter
        controlSize: root.toolbarButtonSize
        renderedIconSize: root.toolbarIconSize
        enabled: root.transformEnabled
        iconSource: root.iconPrefix + "rotate-cw-square.svg"
        toolTipText: enabled ? qsTr("Rotate 90° clockwise")
                             : qsTr("Select one image to rotate")
    }

    AppIconButton {
        id: resizeImageButton
        objectName: "resizeImageButton"
        x: brandDivider.x + 336
        anchors.verticalCenter: parent.verticalCenter
        controlSize: root.toolbarButtonSize
        renderedIconSize: root.toolbarIconSize
        enabled: root.transformEnabled
        iconSource: root.iconPrefix + "image-resize.svg"
        toolTipText: enabled ? qsTr("Resize image…") : qsTr("Select one image to resize")
    }

    AppIconButton {
        id: compareButton
        objectName: "compareButton"
        x: brandDivider.x + 366
        anchors.verticalCenter: parent.verticalCenter
        controlSize: root.toolbarButtonSize
        renderedIconSize: root.toolbarIconSize
        enabled: root.compareEnabled
        iconSource: root.iconPrefix + "compare.svg"
        toolTipText: enabled ? "Compare selected images" : "Select 2–4 images to compare"
    }

    TextField {
        id: fileSearch
        objectName: "browserSearchField"
        x: root.width - width - 18
        anchors.verticalCenter: parent.verticalCenter
        width: 236
        height: root.toolbarButtonSize
        placeholderText: qsTr("Search files")
        enabled: root.activePaneAvailable && root.activeDirectoryAvailable
        selectByMouse: true
        leftPadding: 34
        rightPadding: 40
        color: Theme.graphiteInk
        placeholderTextColor: Theme.faintInk
        font.family: Theme.uiFont
        font.pixelSize: 13

        background: Rectangle {
            color: Theme.searchFieldSurface
            radius: 6
            border.color: !fileSearch.enabled ? Theme.opticalGray
                          : fileSearch.activeFocus ? Theme.searchFieldFocusBorder
                          : fileSearch.hovered ? Theme.searchFieldHoverBorder
                          : Theme.searchFieldBorder
            border.width: fileSearch.activeFocus ? 2 : 1
            Image {
                x: 9
                anchors.verticalCenter: parent.verticalCenter
                width: root.toolbarIconSize
                height: root.toolbarIconSize
                source: root.iconPrefix + "search.svg"
                sourceSize: Qt.size(root.toolbarIconSize * 2,
                                    root.toolbarIconSize * 2)
            }
        }

        Text {
            visible: !fileSearch.activeFocus && fileSearch.text.length === 0
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("⌘F")
            color: Theme.faintInk
            font.family: Theme.uiFont
            font.pixelSize: 11
        }

        Text {
            id: clearSearch
            visible: fileSearch.text.length > 0
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("×")
            color: clearMouse.containsMouse ? Theme.graphiteInk : Theme.mutedInk
            font.pixelSize: 16
            MouseArea {
                id: clearMouse
                anchors.fill: parent
                anchors.margins: -7
                hoverEnabled: true
            }
        }
    }

    AppIconButton {
        id: settingsButton
        objectName: "settingsButton"
        x: fileSearch.x - 36
        anchors.verticalCenter: parent.verticalCenter
        controlSize: root.toolbarButtonSize
        renderedIconSize: root.toolbarIconSize
        iconSource: root.iconPrefix + "settings.svg"
        toolTipText: qsTr("Settings")
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.opticalGray
    }
}
