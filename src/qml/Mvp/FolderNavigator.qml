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
    property string iconPrefix: Theme.iconPrefix
    // Injectable so both native variants can be covered by QML tests on one host.
    property string platformName: Qt.platform.os

    readonly property bool macStyle: platformName === "osx"
    readonly property int indentWidth: macStyle ? 16 : 20
    readonly property int itemHeight: macStyle ? 28 : 26
    readonly property int iconSize_: macStyle ? 16 : 18
    readonly property color selectionBg: macStyle ? Theme.probeBlue : Theme.explorerSelectionBg
    readonly property color hoverBg: macStyle ? "#12000000" : Theme.explorerSelectionBg
    readonly property color sidebarText: macStyle ? Theme.graphiteInk : Theme.graphiteInk
    readonly property string nativeFont: Theme.uiFont

    objectName: "folderNavigatorSurface"
    color: macStyle ? Theme.softHover : Theme.raisedSurface
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

    property var expandedDrives: ({})

    ListModel {
        id: drivePlaces
        dynamicRoles: true
    }

    function syncDrivePlaces() {
        if (!controller || !drivePlaces)
            return
        const places = platformName === "windows" && browsingEnabled
                ? controller.nativeDrivePlaces || [] : []
        // Update by path so adding/removing a drive does not recreate other expanded trees.
        for (let row = drivePlaces.count - 1; row >= 0; --row) {
            const path = drivePlaces.get(row).place.path
            if (!places.some(function (place) { return place.path === path }))
                drivePlaces.remove(row)
        }
        for (let row = 0; row < places.length; ++row) {
            let existing = -1
            for (let index = row; index < drivePlaces.count; ++index) {
                if (drivePlaces.get(index).place.path === places[row].path) {
                    existing = index
                    break
                }
            }
            if (existing < 0)
                drivePlaces.insert(row, { place: places[row] })
            else {
                if (existing !== row)
                    drivePlaces.move(existing, row, 1)
                drivePlaces.setProperty(row, "place", places[row])
            }
        }
    }

    Component.onCompleted: syncDrivePlaces()
    onPlatformNameChanged: syncDrivePlaces()
    onBrowsingEnabledChanged: syncDrivePlaces()
    onControllerChanged: syncDrivePlaces()

    Connections {
        target: root.controller
        function onNativeDrivePlacesChanged() { root.syncDrivePlaces() }
    }

    function belongsTo(path, parentPath) {
        const candidate = String(path).replace(/\\/g, "/")
        const parent = String(parentPath).replace(/\\/g, "/")
        const key = platformName === "windows" ? candidate.toLowerCase() : candidate
        const parentKey = platformName === "windows" ? parent.toLowerCase() : parent
        return key === parentKey || key.startsWith(parentKey.endsWith("/")
                                                   ? parentKey : parentKey + "/")
    }

    function setDriveExpanded(path, expanded) {
        const state = Object.assign({}, expandedDrives)
        state[path] = expanded
        expandedDrives = state
    }

    component SidebarPlace: Rectangle {
        required property var entry

        Layout.fillWidth: true
        implicitHeight: root.itemHeight
        color: "transparent"
        readonly property bool selected:
            entry.kind === "drive"
            ? root.controller.currentDirectory.toLowerCase().startsWith(
                  String(entry.path).toLowerCase())
            : String(entry.path) === root.controller.currentDirectory

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
            x: root.macStyle ? 14 : parent.entry.kind === "drive" ? 28 : 18
            anchors.verticalCenter: parent.verticalCenter
            width: root.iconSize_
            height: root.iconSize_
            source: parent.entry.icon ? parent.entry.icon
                  : parent.entry.kind === "drive"
                    ? root.iconPrefix + "windows-drive.svg"
                    : root.folderIcon(parent.selected)
            sourceSize: Qt.size(32, 32)
            opacity: root.macStyle ? 0.86 : 1
        }

        Text {
            x: root.macStyle ? 40 : parent.entry.kind === "drive" ? 51 : 46
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

    ScrollView {
        id: navigationScroll
        objectName: "navigationScroll"
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: navigationScroll.availableWidth
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
                    text: qsTr("Recent")
                    color: root.macStyle ? Theme.mutedInk : Theme.graphiteInk
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
                    text: qsTr("Quick Access")
                    color: root.macStyle ? Theme.mutedInk : Theme.graphiteInk
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
                    text: qsTr("Locations")
                    color: root.macStyle ? Theme.mutedInk : Theme.graphiteInk
                    font.family: root.nativeFont
                    font.pixelSize: root.macStyle ? 11 : 12
                    font.weight: Font.DemiBold
                }
            }

            Repeater {
                model: drivePlaces
                delegate: ColumnLayout {
                    id: driveBranch
                    required property var place
                    readonly property string drivePath: String(place.path)
                    readonly property bool expanded: root.expandedDrives[drivePath] === true
                    Layout.fillWidth: true
                    spacing: 0

                    function revealDrive() {
                        if (root.belongsTo(root.controller.currentDirectory, drivePath))
                            root.setDriveExpanded(drivePath, true)
                    }

                    Component.onCompleted: {
                        if (root.expandedDrives[drivePath] === undefined)
                            revealDrive()
                    }
                    Connections {
                        target: root.controller
                        function onCurrentDirectoryChanged() { driveBranch.revealDrive() }
                    }

                    SidebarPlace {
                        entry: driveBranch.place
                        Text {
                            x: 5
                            width: root.indentWidth
                            height: root.itemHeight
                            text: driveBranch.expanded ? "▾" : "▸"
                            color: Theme.mutedInk
                            font.family: root.nativeFont
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        MouseArea {
                            objectName: "driveDisclosure-" + driveBranch.drivePath
                            width: root.indentWidth + 6
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.setDriveExpanded(driveBranch.drivePath,
                                                           !driveBranch.expanded)
                        }
                    }

                    Loader {
                        objectName: "driveTreeLoader-" + driveBranch.drivePath
                        Layout.fillWidth: true
                        visible: driveBranch.expanded
                        // Keep loaded branches alive so collapsing a drive preserves its children.
                        active: driveBranch.expanded || item !== null
                        sourceComponent: NavigationTree {
                            drivePath: driveBranch.drivePath
                        }
                        readonly property NavigationTree branchTree: item as NavigationTree
                        Layout.preferredHeight: visible && branchTree ? branchTree.implicitHeight : 0
                    }
                }
            }

            NavigationTree {
                visible: root.platformName !== "windows"
                model: visible ? root.controller.folderTree : null
            }

        }
    }
    component NavigationTree: TreeView {
        id: folderTree
        objectName: "nativeFolderTree"
        Layout.fillWidth: true
        property string drivePath: ""
        property bool pendingReveal: true
        implicitHeight: rows * root.itemHeight
        interactive: false
        model: drivePath.length > 0 && !root.designMode
               ? root.controller.folderTreeBranch(drivePath) : root.controller.folderTree
        rootIndex: root.designMode || drivePath.length > 0
                   ? undefined : root.controller.folderRootIndex

        function revealCurrentFolder() {
            if (!pendingReveal || root.designMode || !visible
                    || root.controller.currentDirectory.length === 0
                    || (drivePath.length > 0
                        && !root.belongsTo(root.controller.currentDirectory, drivePath)))
                return
            const currentIndex = drivePath.length > 0
                    ? root.controller.folderTreeIndex(root.controller.currentDirectory, drivePath)
                    : root.controller.currentFolderTreeIndex
            expandToIndex(currentIndex)
            if ((drivePath.length > 0
                 && root.belongsTo(drivePath, root.controller.currentDirectory))
                    || rowAtIndex(currentIndex) >= 0)
                pendingReveal = false
        }

        Connections {
            target: root.controller
            function onCurrentDirectoryChanged() {
                folderTree.pendingReveal = true
                Qt.callLater(folderTree.revealCurrentFolder)
            }
        }
        Connections {
            target: root.designMode ? null : root.controller.folderTree
            function onDirectoryLoaded(path) {
                if (root.belongsTo(root.controller.currentDirectory, path))
                    Qt.callLater(folderTree.revealCurrentFolder)
            }
        }
        onVisibleChanged: if (visible) Qt.callLater(revealCurrentFolder)
        Component.onCompleted: {
            if (drivePath.length > 0 && !root.designMode)
                root.controller.loadFolderTreeChildren(drivePath)
            Qt.callLater(revealCurrentFolder)
        }

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
                folderTree.pendingReveal = false
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
                x: (treeDelegate.depth + (folderTree.drivePath.length > 0 ? 1 : 0)) * root.indentWidth + (root.macStyle ? 7 : 5)
                anchors.verticalCenter: parent.verticalCenter
                width: root.indentWidth
                height: root.itemHeight
                text: treeDelegate.isDirectory
                      ? (treeDelegate.expanded ? "▾" : "▸") : ""
                color: treeDelegate.isCurrentFolder && root.macStyle ? "white" : Theme.mutedInk
                font.family: root.nativeFont
                font.pixelSize: root.macStyle ? 11 : 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            Image {
                x: (treeDelegate.depth + (folderTree.drivePath.length > 0 ? 1 : 0)) * root.indentWidth + root.indentWidth +
                   (root.macStyle ? 9 : 8)
                anchors.verticalCenter: parent.verticalCenter
                width: root.iconSize_
                height: root.iconSize_
                source: root.folderIcon(treeDelegate.expanded)
                sourceSize: Qt.size(32, 32)
                opacity: root.macStyle ? 0.86 : 1
            }

            Text {
                x: (treeDelegate.depth + (folderTree.drivePath.length > 0 ? 1 : 0)) * root.indentWidth + root.indentWidth +
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
                x: (treeDelegate.depth + (folderTree.drivePath.length > 0 ? 1 : 0)) * root.indentWidth
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

    }
}
