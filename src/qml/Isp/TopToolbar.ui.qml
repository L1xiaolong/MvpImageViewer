import QtQuick
import QtQuick.Controls
import "."

Rectangle {
    id: root
    width: 1440
    height: 52
    color: "#FFFFFF"

    property string iconPrefix: "qrc:/icons/ui/"
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

    Text {
        id: productName
        anchors.left: parent.left
        anchors.right: brandDivider.left
        anchors.verticalCenter: parent.verticalCenter
        text: "ISP Image Viewer"
        horizontalAlignment: Text.AlignHCenter
        color: "#2E3A43"
        font.family: Theme.uiFont
        font.pixelSize: 17
        font.weight: Font.DemiBold
    }

    Rectangle {
        id: brandDivider
        x: Math.round(root.navigationWidth)
        y: 14
        width: 1
        height: 24
        color: "#DDE3E7"
    }

    AppIconButton {
        id: openFolderButton
        objectName: "openFolderButton"
        x: brandDivider.x + 14
        y: 8
        width: 36
        height: 36
        enabled: root.activePaneAvailable
        iconSource: root.iconPrefix + "folder-open.svg"
        toolTipText: "Open folder"
    }

    AppIconButton {
        id: newFolderButton
        objectName: "newFolderButton"
        x: brandDivider.x + 52
        y: 8
        width: 36
        height: 36
        enabled: root.activePaneAvailable && root.activeDirectoryAvailable
        iconSource: root.iconPrefix + "folder-plus.svg"
        toolTipText: "New folder"
    }

    AppIconButton {
        id: folderCompareButton
        objectName: "folderCompareButton"
        x: brandDivider.x + 90
        y: 8
        width: 36
        height: 36
        enabled: root.canAddPane
        iconSource: root.iconPrefix + "folder-pane-plus.svg"
        toolTipText: enabled ? "Add file manager" : "Maximum 4 file managers"
    }

    Rectangle {
        id: fileDivider
        x: brandDivider.x + 140
        y: 14
        width: 1
        height: 24
        color: "#DDE3E7"
    }

    AppIconButton {
        id: gridModeButton
        objectName: "gridModeButton"
        x: brandDivider.x + 154
        y: 8
        width: 36
        height: 36
        checkable: true
        checked: root.displayMode === 0
        enabled: root.activePaneAvailable && root.activeDirectoryAvailable && root.gridEnabled
        iconSource: root.iconPrefix + "grid.svg"
        toolTipText: root.gridEnabled ? "Grid view" : "Grid view is available with up to 2 file managers"
    }

    AppIconButton {
        id: listModeButton
        objectName: "listModeButton"
        x: brandDivider.x + 192
        y: 8
        width: 36
        height: 36
        checkable: true
        checked: root.displayMode === 1
        enabled: root.activePaneAvailable && root.activeDirectoryAvailable
        iconSource: root.iconPrefix + "list.svg"
        toolTipText: "List view"
    }

    AppIconButton {
        id: galleryModeButton
        objectName: "galleryModeButton"
        x: brandDivider.x + 230
        y: 8
        width: 36
        height: 36
        checkable: true
        checked: root.displayMode === 2
        enabled: root.activePaneAvailable && root.activeDirectoryAvailable && root.galleryEnabled
        iconSource: root.iconPrefix + "gallery.svg"
        toolTipText: root.galleryEnabled ? "Gallery view" : "Gallery view is available with 1 file manager"
    }

    Rectangle {
        id: activeViewRail
        visible: root.activePaneAvailable && root.activeDirectoryAvailable
        x: root.displayMode === 0 ? brandDivider.x + 164 : root.displayMode === 1 ? brandDivider.x + 202 : brandDivider.x + 240
        y: 47
        width: 16
        height: 2
        radius: 1
        color: "#58778E"
        Behavior on x {
            NumberAnimation {
                duration: 120
                easing.type: Easing.OutCubic
            }
        }
    }

    Rectangle {
        id: viewDivider
        x: brandDivider.x + 276
        y: 14
        width: 1
        height: 24
        color: "#DDE3E7"
    }

    AppIconButton {
        id: sortButton
        objectName: "sortButton"
        x: brandDivider.x + 290
        y: 8
        width: 36
        height: 36
        enabled: root.activePaneAvailable && root.activeDirectoryAvailable
        iconSource: root.iconPrefix + "sort.svg"
        toolTipText: ["Sort by name", "Sort by modified time", "Sort by file size",
                      "Sort by file type"][root.sortMode]
    }

    AppIconButton {
        id: rotateCounterClockwiseButton
        objectName: "rotateCounterClockwiseButton"
        x: brandDivider.x + 328
        y: 8
        width: 36
        height: 36
        enabled: root.transformEnabled
        iconSource: root.iconPrefix + "rotate-ccw-square.svg"
        toolTipText: enabled ? "Rotate 90° counterclockwise" : "Select one image to rotate"
    }

    AppIconButton {
        id: rotateClockwiseButton
        objectName: "rotateClockwiseButton"
        x: brandDivider.x + 366
        y: 8
        width: 36
        height: 36
        enabled: root.transformEnabled
        iconSource: root.iconPrefix + "rotate-cw-square.svg"
        toolTipText: enabled ? "Rotate 90° clockwise" : "Select one image to rotate"
    }

    AppIconButton {
        id: resizeImageButton
        objectName: "resizeImageButton"
        x: brandDivider.x + 404
        y: 8
        width: 36
        height: 36
        enabled: root.transformEnabled
        iconSource: root.iconPrefix + "image-resize.svg"
        toolTipText: enabled ? "Resize image…" : "Select one image to resize"
    }

    AppIconButton {
        id: compareButton
        objectName: "compareButton"
        x: brandDivider.x + 442
        y: 8
        width: 36
        height: 36
        enabled: root.compareEnabled
        iconSource: root.iconPrefix + "compare.svg"
        toolTipText: enabled ? "Compare selected images" : "Select 2–4 images to compare"
    }

    TextField {
        id: fileSearch
        objectName: "browserSearchField"
        x: root.width - width - 18
        y: 9
        width: 236
        height: 34
        placeholderText: "Search files"
        enabled: root.activePaneAvailable && root.activeDirectoryAvailable
        selectByMouse: true
        leftPadding: 34
        rightPadding: 40
        color: "#33414B"
        font.family: Theme.uiFont
        font.pixelSize: 13

        background: Rectangle {
            color: fileSearch.activeFocus ? "#FFFFFF" : "#FAFBFC"
            radius: 6
            border.color: fileSearch.activeFocus ? "#7893A6" : "#D7DEE3"
            border.width: 1
            Image {
                x: 9
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: 20
                source: root.iconPrefix + "search.svg"
                sourceSize: Qt.size(40, 40)
            }
        }

        Text {
            visible: !fileSearch.activeFocus && fileSearch.text.length === 0
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: "⌘F"
            color: "#9AA5AD"
            font.family: Theme.uiFont
            font.pixelSize: 11
        }

        Text {
            id: clearSearch
            visible: fileSearch.text.length > 0
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: "×"
            color: clearMouse.containsMouse ? "#33414B" : "#7B8891"
            font.pixelSize: 16
            MouseArea {
                id: clearMouse
                anchors.fill: parent
                anchors.margins: -7
                hoverEnabled: true
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: "#DDE3E7"
    }
}
