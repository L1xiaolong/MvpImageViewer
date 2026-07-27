pragma ComponentBehavior: Bound
// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

Rectangle {
    id: root
    required property var controller
    property bool designMode: false
    property bool browsingEnabled: true
    property string iconPrefix: "qrc:/icons/ui/"
    // Injectable so both native variants can be covered by QML tests on one host.
    property string platformName: Qt.platform.os

    readonly property bool macStyle: platformName === "osx"
    readonly property int indentWidth: macStyle ? 16 : 20
    readonly property int itemHeight: macStyle ? 28 : 26
    readonly property int iconSize_: macStyle ? 16 : 18
    readonly property color selectionBg: macStyle ? "#0A64D8" : "#DCEBFA"
    readonly property color hoverBg: macStyle ? "#12000000" : "#E8F2FC"
    readonly property color sidebarText: macStyle ? "#252525" : "#202020"
    readonly property string nativeFont: macStyle ? ".AppleSystemUIFont" : "Segoe UI"

    objectName: "folderNavigatorSurface"
    color: macStyle ? "#F2F2F2" : "#FAFAFA"
    enabled: browsingEnabled
    opacity: browsingEnabled ? 1 : 0.45
    clip: true

    function pathLabel(path) {
        const parts = String(path).split(/[\\/]/)
        return parts.length > 0 && parts[parts.length - 1].length > 0
                ? parts[parts.length - 1] : path
    }

    function folderIcon(expanded) {
        if (root.macStyle)
            return root.iconPrefix + (expanded ? "macos-folder-open.svg" : "macos-folder.svg")
        return root.iconPrefix + (expanded ? "windows-folder-open.svg" : "windows-folder.svg")
    }

    function quickAccessEntries() {
        const entries = []
        const seen = ({})
        const places = root.controller.nativeSidebarPlaces || []
        for (let index = 0; index < places.length; ++index) {
            const place = places[index]
            const key = root.platformName === "windows"
                    ? String(place.path).toLowerCase() : String(place.path)
            if (!seen[key]) {
                entries.push(place)
                seen[key] = true
            }
        }
        return entries
    }

    function recentEntries() {
        const entries = []
        const seen = ({})
        const recent = root.controller.recentFolders || []
        for (let index = 0; index < recent.length && index < 4; ++index) {
            const path = String(recent[index])
            const key = root.platformName === "windows" ? path.toLowerCase() : path
            if (!seen[key]) {
                entries.push({ label: root.pathLabel(path), path: path, kind: "recent" })
                seen[key] = true
            }
        }
        return entries
    }

    function revealCurrentFolder() {
        if (root.designMode || root.controller.currentDirectory.length === 0 ||
                root.controller.currentFolderTreeIndex === undefined)
            return
        folderTree.expandToIndex(root.controller.currentFolderTreeIndex)
    }

    Connections {
        target: root.controller
        function onCurrentDirectoryChanged() { root.revealCurrentFolder() }
    }

    Connections {
        target: root.designMode ? null : root.controller.folderTree
        function onDirectoryLoaded(path) {
            folderTree.forceLayout()
            if (root.controller.currentDirectory.startsWith(path))
                root.revealCurrentFolder()
        }
    }

    Component.onCompleted: root.revealCurrentFolder()

    component SidebarPlace: Rectangle {
        required property var entry

        Layout.fillWidth: true
        implicitHeight: root.itemHeight
        color: "transparent"
        readonly property bool selected:
            String(entry.path) === root.controller.currentDirectory

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: root.macStyle ? 6 : 0
            anchors.rightMargin: root.macStyle ? 6 : 0
            anchors.topMargin: root.macStyle ? 1 : 0
            anchors.bottomMargin: root.macStyle ? 1 : 0
            radius: root.macStyle ? 5 : 0
            color: parent.selected ? root.selectionBg
                   : sidebarPlaceMouse.containsMouse ? root.hoverBg : "transparent"
        }

        Image {
            x: root.macStyle ? 14 : 18
            anchors.verticalCenter: parent.verticalCenter
            width: root.iconSize_
            height: root.iconSize_
            source: parent.entry.icon ? parent.entry.icon : root.folderIcon(parent.selected)
            sourceSize: Qt.size(32, 32)
            opacity: root.macStyle ? 0.86 : 1
        }

        Text {
            x: root.macStyle ? 40 : 46
            width: Math.max(0, parent.width - x - 12)
            anchors.verticalCenter: parent.verticalCenter
            text: parent.entry.label
            elide: Text.ElideMiddle
            color: parent.selected && root.macStyle ? "white" : root.sidebarText
            font.family: root.nativeFont
            font.pixelSize: root.macStyle ? 13 : 12
        }

        MouseArea {
            id: sidebarPlaceMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            onClicked: root.controller.openDirectory(parent.entry.path)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            implicitHeight: root.macStyle ? 12 : 8
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: root.macStyle ? 25 : 30
            color: "transparent"

            Text {
                id: recentHeading
                objectName: "nativeRecentHeading"
                anchors.left: parent.left
                anchors.leftMargin: root.macStyle ? 12 : 14
                anchors.verticalCenter: parent.verticalCenter
                text: "Recent"
                color: root.macStyle ? "#777777" : "#3B3B3B"
                font.family: root.nativeFont
                font.pixelSize: root.macStyle ? 11 : 12
                font.weight: Font.DemiBold
            }
        }

        Repeater {
            model: root.browsingEnabled ? root.recentEntries() : []
            delegate: SidebarPlace {
                required property var modelData
                entry: modelData
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: root.macStyle ? 9 : 6
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: root.macStyle ? 25 : 30
            color: "transparent"

            Text {
                id: quickAccessHeading
                objectName: "nativeQuickAccessHeading"
                anchors.left: parent.left
                anchors.leftMargin: root.macStyle ? 12 : 14
                anchors.verticalCenter: parent.verticalCenter
                text: "Quick Access"
                color: root.macStyle ? "#777777" : "#3B3B3B"
                font.family: root.nativeFont
                font.pixelSize: root.macStyle ? 11 : 12
                font.weight: Font.DemiBold
            }
        }

        Repeater {
            model: root.browsingEnabled ? root.quickAccessEntries() : []
            delegate: SidebarPlace {
                required property var modelData
                entry: modelData
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: root.macStyle ? 9 : 6
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: root.macStyle ? 25 : 30
            color: "transparent"

            Text {
                id: locationsHeading
                objectName: "nativeLocationsHeading"
                anchors.left: parent.left
                anchors.leftMargin: root.macStyle ? 12 : 14
                anchors.verticalCenter: parent.verticalCenter
                text: "Locations"
                color: root.macStyle ? "#777777" : "#3B3B3B"
                font.family: root.nativeFont
                font.pixelSize: root.macStyle ? 11 : 12
                font.weight: Font.DemiBold
            }
        }

        TreeView {
            id: folderTree
            objectName: "nativeFolderTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.controller.folderTree
            rootIndex: root.designMode ? undefined : root.controller.folderRootIndex
            columnWidthProvider: function (column) { return width }
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            delegate: Item {
                id: treeDelegate
                required property TreeView treeView
                required property bool isTreeNode
                required property bool expanded
                required property bool hasChildren
                required property int depth
                required property int row
                required property int column
                required property string display
                required property string filePath

                implicitWidth: folderTree.width
                implicitHeight: root.itemHeight
                readonly property bool isDirectory: hasChildren || isTreeNode
                readonly property bool isCurrentFolder:
                    filePath === root.controller.currentDirectory
                function toggleDirectoryExpansion() {
                    if (!treeDelegate.isDirectory)
                        return
                    if (treeDelegate.expanded)
                        treeDelegate.treeView.collapse(treeDelegate.row)
                    else {
                        treeDelegate.treeView.expand(treeDelegate.row)
                        root.controller.loadFolderTreeChildren(treeDelegate.filePath)
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: root.macStyle ? 6 : 0
                    anchors.rightMargin: root.macStyle ? 6 : 0
                    anchors.topMargin: root.macStyle ? 1 : 0
                    anchors.bottomMargin: root.macStyle ? 1 : 0
                    radius: root.macStyle ? 5 : 0
                    color: treeDelegate.isCurrentFolder ? root.selectionBg
                           : disclosureMouse.containsMouse || directoryMouse.containsMouse
                             ? root.hoverBg : "transparent"
                }

                Text {
                    x: treeDelegate.depth * root.indentWidth + (root.macStyle ? 7 : 5)
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.indentWidth
                    height: root.itemHeight
                    text: treeDelegate.isDirectory
                          ? (treeDelegate.expanded ? "▾" : "▸") : ""
                    color: treeDelegate.isCurrentFolder && root.macStyle ? "white" : "#666666"
                    font.family: root.nativeFont
                    font.pixelSize: root.macStyle ? 11 : 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                Image {
                    x: treeDelegate.depth * root.indentWidth + root.indentWidth +
                       (root.macStyle ? 9 : 8)
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.iconSize_
                    height: root.iconSize_
                    source: !root.macStyle && treeDelegate.depth === 0
                            ? root.iconPrefix + "windows-drive.svg"
                            : root.folderIcon(treeDelegate.expanded)
                    sourceSize: Qt.size(32, 32)
                    opacity: root.macStyle ? 0.86 : 1
                }

                Text {
                    x: treeDelegate.depth * root.indentWidth + root.indentWidth +
                       root.iconSize_ + (root.macStyle ? 14 : 15)
                    width: Math.max(0, parent.width - x - 10)
                    anchors.verticalCenter: parent.verticalCenter
                    text: treeDelegate.display
                    elide: Text.ElideRight
                    color: treeDelegate.isCurrentFolder && root.macStyle
                           ? "white" : root.sidebarText
                    font.family: root.nativeFont
                    font.pixelSize: root.macStyle ? 13 : 12
                    font.weight: Font.Normal
                }

                MouseArea {
                    id: disclosureMouse
                    objectName: "folderDisclosure-" + treeDelegate.row
                    x: treeDelegate.depth * root.indentWidth
                    width: root.indentWidth + 6
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    enabled: treeDelegate.isDirectory
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: treeDelegate.toggleDirectoryExpansion()
                }

                MouseArea {
                    id: directoryMouse
                    objectName: "folderDirectory-" + treeDelegate.row
                    x: disclosureMouse.x + disclosureMouse.width
                    width: Math.max(0, parent.width - x)
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    onClicked: root.controller.openDirectory(treeDelegate.filePath)
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: root.macStyle ? 8 : 10
            }
        }
    }
}
