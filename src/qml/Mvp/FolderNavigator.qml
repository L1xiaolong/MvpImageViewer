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
    readonly property int indentWidth: 16
    readonly property int itemHeight: 30
    readonly property int iconSize_: 16
    readonly property color selectionBg: Theme.explorerSelectionBg
    readonly property color hoverBg: Theme.softHover
    readonly property color sidebarText: Theme.graphiteInk
    readonly property string nativeFont: Theme.uiFont

    objectName: "folderNavigatorSurface"
    color: Theme.sidebarSurface
    enabled: browsingEnabled
    opacity: browsingEnabled ? 1 : 0.45
    clip: true

    function pathLabel(path) {
        const parts = String(path).split(/[\\/]/)
        return parts.length > 0 && parts[parts.length - 1].length > 0
                ? parts[parts.length - 1] : path
    }

    function folderIcon(expanded) {
        return root.iconPrefix + (expanded ? "folder-open.svg" : "folder.svg")
    }
    function placeIcon(entry) {
        if (entry.remote) return root.iconPrefix + "nav-network.svg"
        const icons = {home:"nav-home", desktop:"nav-desktop", documents:"nav-documents",
                       downloads:"nav-downloads", pictures:"nav-pictures", drive:"nav-drive"}
        return root.iconPrefix + (icons[entry.kind] || "folder") + ".svg"
    }
    function samePath(left, right) {
        let a = String(left).replace(/[\\/]+$/, "")
        let b = String(right).replace(/[\\/]+$/, "")
        if (root.platformName === "windows") { a = a.toLowerCase(); b = b.toLowerCase() }
        return a === b
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
                entries.push({ label: root.pathLabel(path), path: path, kind: "recent",
                               remote: root.controller.isNetworkPath !== undefined &&
                                       root.controller.isNetworkPath(path) })
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
    onControllerChanged: {
        syncDrivePlaces()
        Qt.callLater(revealActivePath)
    }

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

    signal activePathChanged()
    function revealActivePath() {
        const state = ({})
        const places = root.controller.nativeDrivePlaces || []
        for (const place of places)
            state[place.path] = root.belongsTo(root.controller.currentDirectory, place.path)
        expandedDrives = state
        activePathChanged()
    }

    Connections {
        target: root.controller
        function onCurrentDirectoryChanged() { root.revealActivePath() }
    }

    component SidebarPlace: Rectangle {
        required property var entry

        Layout.fillWidth: true
        implicitHeight: root.itemHeight
        color: "transparent"
        readonly property bool selected: root.samePath(entry.path, root.controller.currentDirectory)

        ToolTip.visible: sidebarPlaceMouse.containsMouse
        ToolTip.delay: 500
        ToolTip.text: String(entry.path)

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            anchors.topMargin: 1
            anchors.bottomMargin: 1
            radius: 5
            color: parent.selected ? root.selectionBg
                   : sidebarPlaceMouse.containsMouse ? root.hoverBg : "transparent"
        }

        Image {
            x: parent.entry.kind === "drive" ? 32 : 16
            anchors.verticalCenter: parent.verticalCenter
            width: root.iconSize_
            height: root.iconSize_
            source: root.placeIcon(parent.entry)
            sourceSize: Qt.size(32, 32)
            opacity: 0.72
        }

        Text {
            objectName: "sidebarPlaceLabel-" + parent.entry.path
            x: parent.entry.kind === "drive" ? 56 : 40
            width: Math.max(0, parent.width - x - 12)
            anchors.verticalCenter: parent.verticalCenter
            text: parent.entry.label
            elide: Text.ElideMiddle
            color: root.sidebarText
            font.family: root.nativeFont
            font.pixelSize: 13
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
                implicitHeight: 8
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 26
                color: "transparent"

                Text {
                    id: recentHeading
                    objectName: "nativeRecentHeading"
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Recent")
                    color: Theme.mutedInk
                    font.family: root.nativeFont
                    font.pixelSize: 11
                    font.weight: Theme.bodyFontWeight
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
                implicitHeight: 16
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 26
                color: "transparent"

                Text {
                    id: quickAccessHeading
                    objectName: "nativeQuickAccessHeading"
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Quick Access")
                    color: Theme.mutedInk
                    font.family: root.nativeFont
                    font.pixelSize: 11
                    font.weight: Theme.bodyFontWeight
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
                implicitHeight: 16
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 26
                color: "transparent"

                Text {
                    id: locationsHeading
                    objectName: "nativeLocationsHeading"
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Locations")
                    color: Theme.mutedInk
                    font.family: root.nativeFont
                    font.pixelSize: 11
                    font.weight: Theme.bodyFontWeight
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
                        target: root
                        function onActivePathChanged() { driveBranch.revealDrive() }
                    }

                    SidebarPlace {
                        entry: driveBranch.place
                        Image {
                            x: 14
                            anchors.verticalCenter: parent.verticalCenter
                            width: 12
                            height: 12
                            source: root.iconPrefix + "chevron-down.svg"
                            rotation: driveBranch.expanded ? 0 : -90
                            opacity: 0.65
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
                // Effective visibility also follows the browse page during full-screen viewing.
                // Keep the model attached while hidden so TreeView retains expanded folders.
                model: root.platformName !== "windows" ? root.controller.folderTree : null
            }

        }
    }
    component NavigationTree: TreeView {
        id: folderTree
        objectName: "nativeFolderTree"
        Layout.fillWidth: true
        property string drivePath: ""
        property bool pendingReveal: true
        property bool collapseBeforeReveal: true
        implicitHeight: rows * root.itemHeight
        interactive: false
        model: drivePath.length > 0 && !root.designMode
               ? root.controller.folderTreeBranch(drivePath) : root.controller.folderTree
        rootIndex: root.designMode || drivePath.length > 0
                   ? undefined : root.controller.folderRootIndex

        function requestReveal() {
            pendingReveal = true
            Qt.callLater(revealCurrentFolder)
        }

        function resetToCurrentPath() {
            collapseBeforeReveal = true
            requestReveal()
        }

        function revealCurrentFolder() {
            if (!pendingReveal || root.designMode || !visible
                    || root.controller.currentDirectory.length === 0
                    || (drivePath.length > 0
                        && !root.belongsTo(root.controller.currentDirectory, drivePath)))
                return
            const currentIndex = drivePath.length > 0
                    ? root.controller.folderTreeIndex(root.controller.currentDirectory, drivePath)
                    : root.controller.currentFolderTreeIndex
            if (collapseBeforeReveal) {
                collapseRecursively()
                collapseBeforeReveal = false
            }
            expandToIndex(currentIndex)
            if ((drivePath.length > 0
                 && root.belongsTo(drivePath, root.controller.currentDirectory))
                    || rowAtIndex(currentIndex) >= 0)
                pendingReveal = false
        }

        Connections {
            target: root.controller
            function onCurrentDirectoryChanged() { folderTree.resetToCurrentPath() }
        }
        Connections {
            target: root
            function onActivePathChanged() { folderTree.resetToCurrentPath() }
        }
        // Each pane owns a branch proxy. Swapping it resets TreeView's expanded
        // rows even when both panes share the same underlying filesystem model.
        onModelChanged: requestReveal()
        onRootIndexChanged: requestReveal()
        Connections {
            target: folderTree.model
            function onModelReset() { folderTree.requestReveal() }
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
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                anchors.topMargin: 1
                anchors.bottomMargin: 1
                radius: 5
                color: treeDelegate.isCurrentFolder ? root.selectionBg
                       : disclosureMouse.containsMouse || directoryMouse.containsMouse
                         ? root.hoverBg : "transparent"
            }

            Image {
                x: (treeDelegate.depth + (folderTree.drivePath.length > 0 ? 1 : 0)) * root.indentWidth + 8
                anchors.verticalCenter: parent.verticalCenter
                width: 12
                height: 12
                source: root.iconPrefix + "chevron-down.svg"
                visible: treeDelegate.isDirectory
                rotation: treeDelegate.expanded ? 0 : -90
                opacity: 0.65
            }

            Image {
                x: (treeDelegate.depth + (folderTree.drivePath.length > 0 ? 1 : 0)) * root.indentWidth + root.indentWidth +
                   (root.macStyle ? 9 : 8)
                anchors.verticalCenter: parent.verticalCenter
                width: root.iconSize_
                height: root.iconSize_
                source: root.folderIcon(treeDelegate.expanded)
                sourceSize: Qt.size(32, 32)
                opacity: 0.72
            }

            Text {
                x: (treeDelegate.depth + (folderTree.drivePath.length > 0 ? 1 : 0)) * root.indentWidth + root.indentWidth +
                   root.iconSize_ + (root.macStyle ? 14 : 15)
                width: Math.max(0, parent.width - x - 10)
                anchors.verticalCenter: parent.verticalCenter
                text: treeDelegate.display
                elide: Text.ElideMiddle
                color: root.sidebarText
                font.family: root.nativeFont
                font.pixelSize: 13
                font.weight: Font.Normal
            }

            PathToolTip {
                parent: treeDelegate
                path: treeDelegate.filePath
                fileName: treeDelegate.display
                visible: directoryMouse.containsMouse
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
