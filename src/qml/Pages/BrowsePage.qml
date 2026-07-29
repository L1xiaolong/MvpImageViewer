pragma ComponentBehavior: Bound
// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as PlatformDialogs
import "../Isp"

Rectangle {
    id: root
    objectName: "browsePage"
    color: Theme.sensorWhite
    // Kept injectable for production and design-time mocks. Qt Design Studio's
    // 2D puppet does not instantiate custom components with required var props.
    property var controller: null
    property var workspaceController: controller
    property var propertiesController: null
    property var rawController: null
    property var settingsController: null
    property bool navigatorVisible: true
    property real navigatorWidth: Theme.sidebarWidth
    property real galleryStripWidth: 280
    property int displayMode: controller ? controller.displayMode : 0
    property bool designMode: false
    property string iconPrefix: Theme.iconPrefix
    readonly property var sortLabels: [qsTr("Name"), qsTr("Modified"),
                                       qsTr("Size"), qsTr("Type")]
    signal fullScreenRequested(var paths, int initialIndex)
    signal settingsRequested()
    readonly property bool contentInteractionEnabled: !propertiesDialog.opened &&
                                                       !rawParametersDialog.opened &&
                                                       !imageResizeDialog.opened
    readonly property bool fileShortcutsEnabled: (workspaceController.hasActivePane === undefined ||
                                                  workspaceController.hasActivePane) &&
                                                  !toolbar.searchControl.activeFocus &&
                                                  !newFolderDialog.opened &&
                                                  !renameDialog.opened &&
                                                  !trashDialog.opened &&
                                                  !transferDialog.opened &&
                                                  !propertiesDialog.opened &&
                                                  !rawParametersDialog.opened &&
                                                  !imageResizeDialog.opened &&
                                                  !folderPicker.visible
    function pathIsRaw(path) {
        const lowered = path.toLowerCase();
        return lowered.endsWith(".raw") || lowered.endsWith(".yuv");
    }
    function synchronizeActivePaneControls() {
        if (!root.controller)
            return
        if (toolbar.searchControl.text !== root.controller.filterText)
            toolbar.searchControl.text = root.controller.filterText || ""
    }
    function paneController(index) {
        const availablePanes = root.workspaceController.panes
        return availablePanes && index >= 0 && index < availablePanes.length
                ? availablePanes[index] : root.controller
    }
    function configuredShortcut(action, fallback) {
        const revision = root.settingsController
                ? root.settingsController.shortcutsRevision : 0
        return root.settingsController
                ? root.settingsController.shortcutFor(action) : fallback
    }


    TopToolbar {
        id: toolbar
        objectName: "topToolbar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.toolbarHeight
        iconPrefix: root.iconPrefix
        displayMode: root.displayMode
        sortMode: root.controller.sortMode
        compareEnabled: root.workspaceController.canCompare
        activePaneAvailable: root.workspaceController.hasActivePane === undefined
                             ? true : root.workspaceController.hasActivePane
        activeDirectoryAvailable: root.controller.currentDirectory.length > 0
        canAddPane: root.workspaceController.canAddPane === undefined
                    ? true : root.workspaceController.canAddPane
        gridEnabled: root.workspaceController.paneCount === undefined ||
                     root.workspaceController.paneCount <= 2
        galleryEnabled: root.workspaceController.paneCount === undefined ||
                        root.workspaceController.paneCount === 1
        transformEnabled: root.controller.canTransform
        navigationWidth: root.navigatorWidth
    }

    Connections {
        target: toolbar.gridControl
        function onClicked() { root.workspaceController.setActiveDisplayMode(0); }
    }
    Connections {
        target: toolbar.listControl
        function onClicked() { root.workspaceController.setActiveDisplayMode(1); }
    }
    Connections {
        target: toolbar.galleryControl
        function onClicked() {
            root.workspaceController.setActiveDisplayMode(2)
        }
    }
    Connections {
        target: toolbar.sortControl
        function onClicked() { sortMenu.popup(); }
    }
    Connections {
        target: toolbar.openFolderControl
        function onClicked() { root.controller.chooseDirectory(); }
    }
    Connections {
        target: toolbar.newFolderControl
        function onClicked() { newFolderDialog.open(); }
    }
    Connections {
        target: toolbar.folderCompareControl
        function onClicked() { root.workspaceController.addFileManagerPane(); }
    }
    Connections {
        target: toolbar.searchControl
        function onTextChanged() { searchDebounce.restart(); }
    }
    Timer {
        id: searchDebounce
        interval: 120
        repeat: false
        onTriggered: root.controller.setFilterText(toolbar.searchControl.text)
    }
    Connections {
        target: toolbar.clearSearchControl
        function onClicked() { toolbar.searchControl.clear(); }
    }
    Connections {
        target: toolbar.compareControl
        function onClicked() { root.workspaceController.compareSelected(); }
    }
    Connections {
        target: toolbar.rotateClockwiseControl
        function onClicked() { root.controller.rotateSelectedClockwise(); }
    }
    Connections {
        target: toolbar.rotateCounterClockwiseControl
        function onClicked() { root.controller.rotateSelectedCounterClockwise(); }
    }
    Connections {
        target: toolbar.resizeImageControl
        function onClicked() { imageResizeDialog.openForSelection(); }
    }
    Connections {
        target: toolbar.settingsControl
        function onClicked() { root.settingsRequested(); }
    }
    Connections {
        target: root.controller
        function onGalleryImageChanged() {
            if (root.controller.galleryImageReady &&
                    galleryWorkspace.pixelProbeText === "Loading pixel data…")
                galleryWorkspace.pixelProbeText = "";
        }
    }
    Connections {
        target: root.workspaceController
        function onActivePaneChanged() { Qt.callLater(root.synchronizeActivePaneControls) }
    }
    Connections {
        target: root.controller
        function onFilterTextChanged() { root.synchronizeActivePaneControls() }
    }
    Component.onCompleted: synchronizeActivePaneControls()

    AppMenu {
        id: sortMenu
        AppMenuItem {
            text: (root.controller.sortMode === 0 ? "✓  " : "    ") + "Name"
            onTriggered: root.controller.setSortMode(0)
        }
        AppMenuItem {
            text: (root.controller.sortMode === 1 ? "✓  " : "    ") + "Modified time"
            onTriggered: root.controller.setSortMode(1)
        }
        AppMenuItem {
            text: (root.controller.sortMode === 2 ? "✓  " : "    ") + "File size"
            onTriggered: root.controller.setSortMode(2)
        }
        AppMenuItem {
            text: (root.controller.sortMode === 3 ? "✓  " : "    ") + "File type"
            onTriggered: root.controller.setSortMode(3)
        }
    }

    ImageResizeDialog {
        id: imageResizeDialog
        parent: Overlay.overlay
        controller: root.controller
    }

    Menu {
        id: folderMenu
        MenuItem {
            text: qsTr("Open folder…")
            onTriggered: root.controller.chooseDirectory()
        }
        MenuItem {
            text: qsTr("Back")
            enabled: root.controller.canGoBack
            onTriggered: root.controller.navigateBack()
        }
        MenuItem {
            text: qsTr("Forward")
            enabled: root.controller.canGoForward
            onTriggered: root.controller.navigateForward()
        }
        MenuItem {
            text: qsTr("Parent folder")
            enabled: root.controller.canGoUp
            onTriggered: root.controller.navigateUp()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Add file manager")
            enabled: root.workspaceController.canAddPane === undefined || root.workspaceController.canAddPane
            onTriggered: root.workspaceController.addFileManagerPane()
        }
    }

    // Design reference: Quick Folder Navigator, collapsible with Cmd/Ctrl+B.
    FolderNavigator {
        id: navigator
        objectName: "folderNavigator"
        controller: root.controller
        designMode: root.designMode
        browsingEnabled: root.workspaceController.hasActivePane === undefined ||
                         root.workspaceController.hasActivePane
        iconPrefix: root.iconPrefix
        anchors.left: parent.left
        anchors.top: toolbar.bottom
        anchors.bottom: statusRail.top
        width: root.navigatorVisible ? root.navigatorWidth : 0
        opacity: root.navigatorVisible ? 1 : 0
        Behavior on width {
            enabled: !resizeMouse.pressed
            NumberAnimation {
                duration: Theme.normal
                easing.type: Easing.OutCubic
            }
        }
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.fast
            }
        }
    }

    Rectangle {
        id: gutter
        anchors.left: navigator.right
        anchors.top: toolbar.bottom
        anchors.bottom: statusRail.top
        width: root.navigatorVisible ? 1 : 0
        color: resizeMouse.containsMouse || resizeMouse.pressed ? Theme.primaryButton : Theme.opticalGray
        z: 20

        MouseArea {
            id: resizeMouse
            anchors.centerIn: parent
            width: 11
            height: parent.height
            enabled: root.navigatorVisible
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            property real lastRootX: 0

            onPressed: function (mouse) {
                lastRootX = mapToItem(root, mouse.x, mouse.y).x;
            }
            onPositionChanged: function (mouse) {
                if (!pressed)
                    return;
                const currentRootX = mapToItem(root, mouse.x, mouse.y).x;
                const maximumWidth = Math.max(220, Math.min(420, root.width - 560));
                root.navigatorWidth = Math.max(200, Math.min(maximumWidth,
                                                             root.navigatorWidth + currentRootX - lastRootX));
                lastRootX = currentRootX;
            }
        }
    }

    // The production workspace keeps every folder session in the main window.
    // SplitView is recreated when the pane count changes, which intentionally
    // restores equal proportions after add/close operations.
    Item {
        id: paneWorkspace
        objectName: "paneWorkspace"
        visible: root.displayMode !== 2 || root.workspaceController.paneCount !== 1
        anchors.left: gutter.right
        anchors.right: parent.right
        anchors.top: toolbar.bottom
        anchors.bottom: statusRail.top
        anchors.margins: 8

        Loader {
            id: paneLayoutLoader
            anchors.fill: parent
            sourceComponent: {
                const count = root.workspaceController.paneCount
                if (count <= 1) return singlePaneComponent
                if (count === 4) return fourPaneComponent
                return horizontalPanesComponent
            }
        }
    }

    Component {
        id: singlePaneComponent
        BrowserPane {
            controller: root.paneController(0)
            workspaceController: root.workspaceController
            paneIndex: 0
            iconPrefix: root.iconPrefix
            contentInteractionEnabled: root.contentInteractionEnabled
            onNewFolderRequested: newFolderDialog.open()
        }
    }

    Component {
        id: horizontalPanesComponent
        SplitView {
            id: horizontalSplit
            orientation: Qt.Horizontal
            function resetEqual() {
                const equalWidth = (width - Math.max(0, paneRepeater.count - 1) * 7) /
                                   Math.max(1, paneRepeater.count)
                for (let index = 0; index < paneRepeater.count; ++index) {
                    const pane = paneRepeater.itemAt(index)
                    if (pane)
                        pane.SplitView.preferredWidth = equalWidth
                }
            }
            handle: Rectangle {
                objectName: "horizontalPaneHandle"
                implicitWidth: 7
                color: SplitHandle.pressed || SplitHandle.hovered ? Theme.accentBorder : "transparent"
                property bool handlePressed: SplitHandle.pressed
                property real pressCoordinate: 0
                property double lastClickAt: 0
                onHandlePressedChanged: {
                    if (handlePressed) {
                        pressCoordinate = x
                        return
                    }
                    if (Math.abs(x - pressCoordinate) > 1) {
                        lastClickAt = 0
                        return
                    }
                    const now = Date.now()
                    if (now - lastClickAt <= 500) {
                        horizontalSplit.resetEqual()
                        lastClickAt = 0
                    } else {
                        lastClickAt = now
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: parent.height
                    color: SplitHandle.pressed || SplitHandle.hovered ? Theme.primaryButton : Theme.opticalGray
                }
            }
            Repeater {
                id: paneRepeater
                model: root.workspaceController.panes
                delegate: BrowserPane {
                    required property var modelData
                    required property int index
                    controller: modelData || root.controller
                    workspaceController: root.workspaceController
                    paneIndex: index
                    iconPrefix: root.iconPrefix
                    contentInteractionEnabled: root.contentInteractionEnabled
                    SplitView.fillWidth: index === paneRepeater.count - 1
                    SplitView.minimumWidth: 190
                    SplitView.preferredWidth: (horizontalSplit.width -
                                               Math.max(0, paneRepeater.count - 1) * 7) /
                                              Math.max(1, paneRepeater.count)
                    onNewFolderRequested: newFolderDialog.open()
                }
            }
        }
    }

    Component {
        id: fourPaneComponent
        SplitView {
            id: verticalSplit
            orientation: Qt.Vertical
            function resetEqual() {
                const equalHeight = (verticalSplit.height - 7) / 2
                topRow.SplitView.preferredHeight = equalHeight
                bottomRow.SplitView.preferredHeight = equalHeight
            }
            handle: Rectangle {
                objectName: "verticalPaneHandle"
                implicitHeight: 7
                color: SplitHandle.pressed || SplitHandle.hovered ? Theme.accentBorder : "transparent"
                property bool handlePressed: SplitHandle.pressed
                property real pressCoordinate: 0
                property double lastClickAt: 0
                onHandlePressedChanged: {
                    if (handlePressed) {
                        pressCoordinate = y
                        return
                    }
                    if (Math.abs(y - pressCoordinate) > 1) {
                        lastClickAt = 0
                        return
                    }
                    const now = Date.now()
                    if (now - lastClickAt <= 500) {
                        verticalSplit.resetEqual()
                        lastClickAt = 0
                    } else {
                        lastClickAt = now
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: 1
                    color: SplitHandle.pressed || SplitHandle.hovered ? Theme.primaryButton : Theme.opticalGray
                }
            }
            SplitView {
                id: topRow
                orientation: Qt.Horizontal
                SplitView.fillHeight: false
                SplitView.minimumHeight: 190
                SplitView.preferredHeight: (verticalSplit.height - 7) / 2
                function resetEqual() {
                    const equalWidth = (topRow.width - 7) / 2
                    topLeft.SplitView.preferredWidth = equalWidth
                    topRight.SplitView.preferredWidth = equalWidth
                }
                handle: Rectangle {
                    objectName: "topRowPaneHandle"
                    implicitWidth: 7
                    color: SplitHandle.pressed || SplitHandle.hovered ? Theme.accentBorder : "transparent"
                    property bool handlePressed: SplitHandle.pressed
                    property real pressCoordinate: 0
                    property double lastClickAt: 0
                    onHandlePressedChanged: {
                        if (handlePressed) {
                            pressCoordinate = x
                            return
                        }
                        if (Math.abs(x - pressCoordinate) > 1) {
                            lastClickAt = 0
                            return
                        }
                        const now = Date.now()
                        if (now - lastClickAt <= 500) {
                            topRow.resetEqual()
                            lastClickAt = 0
                        } else {
                            lastClickAt = now
                        }
                    }
                    Rectangle { anchors.centerIn: parent; width: 1; height: parent.height; color: SplitHandle.pressed || SplitHandle.hovered ? Theme.primaryButton : Theme.opticalGray }
                }
                BrowserPane {
                    id: topLeft
                    controller: root.paneController(0)
                    workspaceController: root.workspaceController
                    paneIndex: 0
                    iconPrefix: root.iconPrefix
                    contentInteractionEnabled: root.contentInteractionEnabled
                    SplitView.fillWidth: false
                    SplitView.minimumWidth: 190
                    SplitView.preferredWidth: (topRow.width - 7) / 2
                    onNewFolderRequested: newFolderDialog.open()
                }
                BrowserPane {
                    id: topRight
                    controller: root.paneController(1)
                    workspaceController: root.workspaceController
                    paneIndex: 1
                    iconPrefix: root.iconPrefix
                    contentInteractionEnabled: root.contentInteractionEnabled
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 190
                    SplitView.preferredWidth: (topRow.width - 7) / 2
                    onNewFolderRequested: newFolderDialog.open()
                }
            }
            SplitView {
                id: bottomRow
                orientation: Qt.Horizontal
                SplitView.fillHeight: true
                SplitView.minimumHeight: 190
                SplitView.preferredHeight: (verticalSplit.height - 7) / 2
                function resetEqual() {
                    const equalWidth = (bottomRow.width - 7) / 2
                    bottomLeft.SplitView.preferredWidth = equalWidth
                    bottomRight.SplitView.preferredWidth = equalWidth
                }
                handle: Rectangle {
                    objectName: "bottomRowPaneHandle"
                    implicitWidth: 7
                    color: SplitHandle.pressed || SplitHandle.hovered ? Theme.accentBorder : "transparent"
                    property bool handlePressed: SplitHandle.pressed
                    property real pressCoordinate: 0
                    property double lastClickAt: 0
                    onHandlePressedChanged: {
                        if (handlePressed) {
                            pressCoordinate = x
                            return
                        }
                        if (Math.abs(x - pressCoordinate) > 1) {
                            lastClickAt = 0
                            return
                        }
                        const now = Date.now()
                        if (now - lastClickAt <= 500) {
                            bottomRow.resetEqual()
                            lastClickAt = 0
                        } else {
                            lastClickAt = now
                        }
                    }
                    Rectangle { anchors.centerIn: parent; width: 1; height: parent.height; color: SplitHandle.pressed || SplitHandle.hovered ? Theme.primaryButton : Theme.opticalGray }
                }
                BrowserPane {
                    id: bottomLeft
                    controller: root.paneController(2)
                    workspaceController: root.workspaceController
                    paneIndex: 2
                    iconPrefix: root.iconPrefix
                    contentInteractionEnabled: root.contentInteractionEnabled
                    SplitView.fillWidth: false
                    SplitView.minimumWidth: 190
                    SplitView.preferredWidth: (bottomRow.width - 7) / 2
                    onNewFolderRequested: newFolderDialog.open()
                }
                BrowserPane {
                    id: bottomRight
                    controller: root.paneController(3)
                    workspaceController: root.workspaceController
                    paneIndex: 3
                    iconPrefix: root.iconPrefix
                    contentInteractionEnabled: root.contentInteractionEnabled
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 190
                    SplitView.preferredWidth: (bottomRow.width - 7) / 2
                    onNewFolderRequested: newFolderDialog.open()
                }
            }
        }
    }

    // Gallery view: a quiet inspection surface with a large fitted preview and
    // a compact contact strip. It uses the same production thumbnail model.
    Item {
        id: galleryWorkspace
        objectName: "galleryWorkspace"
        property url currentPreviewUrl: ""
        property string currentPreviewPath: ""
        property string currentPreviewName: ""
        property string currentPreviewTechnicalLabel: ""
        property string contextPath: ""
        property bool contextIsDirectory: false
        property string pixelProbeText: ""
        property bool actualPixels: false
        property real manualZoom: 1.0
        function showPreview(path, previewUrl, fileName, technicalLabel, directory) {
            if (directory)
                return;
            currentPreviewPath = path;
            currentPreviewUrl = previewUrl;
            currentPreviewName = fileName;
            currentPreviewTechnicalLabel = technicalLabel;
            pixelProbeText = "";
            actualPixels = false;
            manualZoom = 1.0;
            root.controller.setGalleryPath(path);
        }
        function showImageMenu(x, y) {
            if (root.controller.selectionCount === 0)
                return;
            if (contextIsDirectory)
                galleryFolderContextMenu.popup(x, y);
            else if (root.pathIsRaw(contextPath))
                galleryRawFileContextMenu.popup(x, y);
            else
                galleryFileContextMenu.popup(x, y);
        }
        visible: root.displayMode === 2 && root.workspaceController.paneCount === 1
        anchors.left: gutter.right
        anchors.right: parent.right
        anchors.top: toolbar.bottom
        anchors.bottom: statusRail.top
        anchors.margins: 16

        Rectangle {
            id: galleryPreview
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: galleryResizeHandle.left
            anchors.rightMargin: 4
            color: Theme.softHover
            radius: 8
            clip: true

            Rectangle {
                id: galleryInfoRail
                objectName: "galleryInfoRail"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 38
                color: Theme.paperWhite
                border.color: Theme.opticalGray
                border.width: 0

                Text {
                    objectName: "galleryInfoText"
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: galleryZoomLabel.left
                    anchors.rightMargin: 10
                    text: root.controller.galleryInfoText.length > 0
                          ? root.controller.galleryInfoText
                          : galleryWorkspace.currentPreviewPath.length > 0 ? "Loading image information…"
                                                                           : "No image selected"
                    elide: Text.ElideRight
                    color: Theme.graphiteInk
                    font.family: Theme.monoFont
                    font.pixelSize: 10
                }
                Text {
                    id: galleryZoomLabel
                    anchors.right: galleryControls.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 54
                    horizontalAlignment: Text.AlignRight
                    text: {
                        if (!root.controller.galleryImageReady || root.controller.galleryImageSize.width <= 0)
                            return "—";
                        if (galleryWorkspace.actualPixels)
                            return (galleryWorkspace.manualZoom * 100).toFixed(1) + "%";
                        const scale = Math.min(galleryImage.paintedWidth / root.controller.galleryImageSize.width,
                                               galleryImage.paintedHeight / root.controller.galleryImageSize.height);
                        return (scale * 100).toFixed(1) + "%";
                    }
                    color: Theme.mutedInk
                    font.family: Theme.monoFont
                    font.pixelSize: 10
                }

                Row {
                    id: galleryControls
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Button {
                        id: actualPixelsButton
                        width: 42
                        height: 26
                        text: qsTr("1:1")
                        hoverEnabled: true
                        onClicked: {
                            galleryWorkspace.actualPixels = true;
                            galleryWorkspace.manualZoom = 1.0;
                            Qt.callLater(function () {
                                galleryFlick.contentX = Math.max(0, (galleryFlick.contentWidth - galleryFlick.width) / 2);
                                galleryFlick.contentY = Math.max(0, (galleryFlick.contentHeight - galleryFlick.height) / 2);
                            });
                        }
                        background: Rectangle {
                            radius: 5
                            color: actualPixelsButton.down ? Theme.pressedSurface
                                                           : actualPixelsButton.hovered ? Theme.softHover : "transparent"
                            border.color: galleryWorkspace.actualPixels &&
                                          Math.abs(galleryWorkspace.manualZoom - 1.0) < 0.001
                                          ? Theme.primaryButton : Theme.opticalGray
                        }
                        contentItem: Text {
                            text: actualPixelsButton.text
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: Theme.graphiteInk
                            font.family: Theme.monoFont
                            font.pixelSize: 10
                        }
                    }
                    Button {
                        id: fitButton
                        width: 42
                        height: 26
                        text: qsTr("Fit")
                        hoverEnabled: true
                        onClicked: {
                            galleryWorkspace.actualPixels = false;
                            galleryWorkspace.manualZoom = 1.0;
                            galleryFlick.contentX = 0;
                            galleryFlick.contentY = 0;
                        }
                        background: Rectangle {
                            radius: 5
                            color: fitButton.down ? Theme.pressedSurface
                                                  : fitButton.hovered ? Theme.softHover : "transparent"
                            border.color: !galleryWorkspace.actualPixels ? Theme.primaryButton : Theme.opticalGray
                        }
                        contentItem: Text {
                            text: fitButton.text
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: Theme.graphiteInk
                            font.family: Theme.uiFont
                            font.pixelSize: 10
                        }
                    }
                    Button {
                        id: fullScreenButton
                        width: 28
                        height: 26
                        text: qsTr("↗")
                        hoverEnabled: true
                        onClicked: {
                            if (galleryWorkspace.currentPreviewPath.length > 0)
                                root.controller.activatePath(galleryWorkspace.currentPreviewPath);
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Open full screen")
                        background: Rectangle {
                            radius: 5
                            color: fullScreenButton.down ? Theme.pressedSurface
                                                         : fullScreenButton.hovered ? Theme.softHover : "transparent"
                            border.color: Theme.opticalGray
                        }
                        contentItem: Text {
                            text: fullScreenButton.text
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: Theme.graphiteInk
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                        }
                    }
                }
            }

            Rectangle {
                id: galleryImageWell
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: galleryInfoRail.top
                color: Theme.softHover
                clip: true

                Flickable {
                    id: galleryFlick
                    anchors.fill: parent
                    clip: true
                    interactive: root.contentInteractionEnabled
                    boundsBehavior: Flickable.StopAtBounds
                    onInteractiveChanged: {
                        if (!interactive)
                            cancelFlick()
                    }
                    contentWidth: galleryWorkspace.actualPixels
                                  ? Math.max(width, galleryImage.width + 36) : width
                    contentHeight: galleryWorkspace.actualPixels
                                   ? Math.max(height, galleryImage.height + 36) : height

                    Image {
                        id: galleryImage
                        x: galleryWorkspace.actualPixels
                           ? Math.max(18, (galleryFlick.width - width) / 2) : 18
                        y: galleryWorkspace.actualPixels
                           ? Math.max(18, (galleryFlick.height - height) / 2) : 18
                        width: galleryWorkspace.actualPixels && root.controller.galleryImageSize.width > 0
                               ? root.controller.galleryImageSize.width * galleryWorkspace.manualZoom
                               : Math.max(1, galleryFlick.width - 36)
                        height: galleryWorkspace.actualPixels && root.controller.galleryImageSize.height > 0
                                ? root.controller.galleryImageSize.height * galleryWorkspace.manualZoom
                                : Math.max(1, galleryFlick.height - 36)
                        source: galleryWorkspace.currentPreviewUrl.toString().length > 0
                                ? galleryWorkspace.currentPreviewUrl.toString() + "&purpose=gallery"
                                : ""
                        // Do not change the requested source size for each wheel step.
                        // A source reload was the brief white flash seen during zooming.
                        // Keep a stable, high-quality preview texture through the zoom gesture.
                        // Asking the provider for the complete sensor frame on every large image
                        // can allocate an extra 100+ MiB texture and cause a visible blank frame.
                        sourceSize: Qt.size(2048, 2048)
                        asynchronous: true
                        cache: true
                        retainWhileLoading: true
                        smooth: true
                        mipmap: true
                        fillMode: galleryWorkspace.actualPixels ? Image.Stretch : Image.PreserveAspectFit
                    }

                    MouseArea {
                        id: galleryProbeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.BackButton | Qt.ForwardButton
                        preventStealing: false
                        onPositionChanged: function (mouse) {
                            if (!root.controller.galleryImageReady || galleryImage.paintedWidth <= 0 ||
                                    galleryImage.paintedHeight <= 0) {
                                galleryWorkspace.pixelProbeText = "Loading pixel data…";
                                return;
                            }
                            const point = mapToItem(galleryImage, mouse.x, mouse.y);
                            const left = (galleryImage.width - galleryImage.paintedWidth) / 2;
                            const top = (galleryImage.height - galleryImage.paintedHeight) / 2;
                            const sourceSize = root.controller.galleryImageSize;
                            const px = Math.floor((point.x - left) * sourceSize.width / galleryImage.paintedWidth);
                            const py = Math.floor((point.y - top) * sourceSize.height / galleryImage.paintedHeight);
                            const value = root.controller.probeGalleryPixel(px, py);
                            galleryWorkspace.pixelProbeText = value.length > 0
                                                             ? value : "Outside image";
                        }
                        onExited: galleryWorkspace.pixelProbeText = ""
                        onWheel: function (wheel) {
                            if (!root.controller.galleryImageReady)
                                return;
                            const sourceSize = root.controller.galleryImageSize;
                            if (sourceSize.width <= 0 || sourceSize.height <= 0 ||
                                    galleryImage.paintedWidth <= 0 || galleryImage.paintedHeight <= 0)
                                return;
                            // Map through the painted image rather than assuming a fitted image
                            // starts at (0, 0). This keeps the source pixel below the cursor fixed.
                            const imagePoint = mapToItem(galleryImage, wheel.x, wheel.y);
                            const imageLeft = (galleryImage.width - galleryImage.paintedWidth) / 2;
                            const imageTop = (galleryImage.height - galleryImage.paintedHeight) / 2;
                            const sourceX = Math.max(0, Math.min(sourceSize.width,
                                (imagePoint.x - imageLeft) * sourceSize.width / galleryImage.paintedWidth));
                            const sourceY = Math.max(0, Math.min(sourceSize.height,
                                (imagePoint.y - imageTop) * sourceSize.height / galleryImage.paintedHeight));
                            const oldScale = galleryImage.paintedWidth / sourceSize.width;
                            const factor = Math.pow(1.2, wheel.angleDelta.y / 120.0);
                            const newScale = Math.max(0.02, Math.min(32.0, oldScale * factor));
                            const viewportPoint = mapToItem(galleryFlick, wheel.x, wheel.y);
                            galleryWorkspace.actualPixels = true;
                            galleryWorkspace.manualZoom = newScale;
                            Qt.callLater(function () {
                                galleryFlick.contentX = Math.max(0, Math.min(galleryFlick.contentWidth - galleryFlick.width,
                                    galleryImage.x + sourceX * newScale - viewportPoint.x));
                                galleryFlick.contentY = Math.max(0, Math.min(galleryFlick.contentHeight - galleryFlick.height,
                                    galleryImage.y + sourceY * newScale - viewportPoint.y));
                            });
                            wheel.accepted = true;
                        }
                        onClicked: function (mouse) {
                            if (mouse.button === Qt.BackButton) {
                                root.controller.navigateBack();
                                return;
                            }
                            if (mouse.button === Qt.ForwardButton) {
                                root.controller.navigateForward();
                                return;
                            }
                            if (mouse.button === Qt.RightButton) {
                                if (root.controller.selectedPaths.indexOf(galleryWorkspace.currentPreviewPath) < 0)
                                    root.controller.selectPath(galleryWorkspace.currentPreviewPath);
                                galleryWorkspace.contextPath = galleryWorkspace.currentPreviewPath;
                                galleryWorkspace.contextIsDirectory = false;
                                const point = mapToItem(root, mouse.x, mouse.y);
                                galleryWorkspace.showImageMenu(point.x, point.y);
                            } else if (galleryWorkspace.currentPreviewPath.length > 0) {
                                root.controller.selectPath(galleryWorkspace.currentPreviewPath);
                            }
                        }
                        onDoubleClicked: {
                            if (galleryWorkspace.currentPreviewPath.length > 0)
                                root.controller.activatePath(galleryWorkspace.currentPreviewPath);
                        }
                    }
                }
            }
        }

        Item {
            id: galleryResizeHandle
            objectName: "galleryResizeHandle"
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            x: galleryStripPanel.x - width / 2
            width: 12
            z: 30

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: galleryResizeMouse.containsMouse || galleryResizeMouse.pressed
                       ? Theme.primaryButton : Theme.opticalGray
            }
            MouseArea {
                id: galleryResizeMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SizeHorCursor
                property real lastRootX: 0
                onPressed: function (mouse) {
                    lastRootX = mapToItem(root, mouse.x, mouse.y).x;
                }
                onPositionChanged: function (mouse) {
                    if (!pressed)
                        return;
                    const currentRootX = mapToItem(root, mouse.x, mouse.y).x;
                    const maximumWidth = Math.max(240, galleryWorkspace.width - 420);
                    root.galleryStripWidth = Math.max(220, Math.min(maximumWidth,
                                                                     root.galleryStripWidth - (currentRootX - lastRootX)));
                    lastRootX = currentRootX;
                }
            }
        }

        Rectangle {
            id: galleryStripPanel
            objectName: "galleryStripPanel"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.max(220, Math.min(root.galleryStripWidth, galleryWorkspace.width - 420))
            color: Theme.paperWhite
            radius: 8
            border.color: Theme.opticalGray
            border.width: 1

            GridView {
                id: galleryStrip
                objectName: "galleryStrip"
                anchors.fill: parent
                anchors.margins: 8
                // Reserve a real workspace footer so users always have a dependable
                // blank target for the workspace context menu.
                anchors.bottomMargin: 60
                clip: true
                model: root.controller.thumbnails
                currentIndex: 0
                // Keep the contact cards at one optical size. GridView wraps them into
                // more/fewer columns when the divider moves instead of stretching them.
                cellWidth: 132
                cellHeight: 116
                boundsBehavior: Flickable.StopAtBounds
                interactive: root.contentInteractionEnabled
                reuseItems: true
                onInteractiveChanged: {
                    if (!interactive)
                        cancelFlick()
                }

                delegate: Item {
                    id: galleryDelegate
                    objectName: "galleryDelegate-" + galleryDelegate.index
                    required property int index
                    required property string path
                    required property string fileName
                    required property string technicalLabel
                    required property url thumbnailUrl
                    required property bool isDirectory
                    required property bool isSelected

                    property url previewUrl: thumbnailUrl
                    property string previewName: fileName
                    property string previewTechnicalLabel: technicalLabel

                    Drag.active: galleryDragHandler.active && galleryDelegate.isSelected
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.mimeData: ({ "text/uri-list": root.controller.selectedUriList })
                    Drag.imageSource: galleryDelegate.thumbnailUrl
                    Drag.hotSpot.x: galleryDelegate.width / 2
                    Drag.hotSpot.y: Math.min(galleryDelegate.height / 2, 52)

                    onTechnicalLabelChanged: {
                        if (galleryWorkspace.currentPreviewPath === galleryDelegate.path)
                            galleryWorkspace.currentPreviewTechnicalLabel = technicalLabel;
                    }

                    Component.onCompleted: {
                        if (galleryDelegate.index === 0 && galleryWorkspace.currentPreviewUrl.toString().length === 0) {
                            galleryWorkspace.showPreview(galleryDelegate.path, galleryDelegate.thumbnailUrl,
                                                         galleryDelegate.fileName,
                                                         galleryDelegate.technicalLabel,
                                                         galleryDelegate.isDirectory);
                        }
                    }

                    width: galleryStrip.cellWidth
                    height: galleryStrip.cellHeight

                    Rectangle {
                        id: galleryCard
                        objectName: "galleryCard-" + galleryDelegate.index
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: 6
                        color: galleryMouse.containsMouse ? Theme.softHover : "transparent"
                        border.color: galleryDelegate.GridView.isCurrentItem
                                      ? Theme.primaryButton
                                      : galleryDelegate.isSelected ? Theme.accentBorder : "transparent"
                        border.width: galleryDelegate.GridView.isCurrentItem ? 2 : 1
                        ToolTip.delay: 500
                        ToolTip.timeout: 8000
                        ToolTip.visible: galleryMouse.containsMouse &&
                                         !galleryDragHandler.active &&
                                         galleryDelegate.path.length > 0
                        ToolTip.text: galleryDelegate.path

                        Image {
                            id: galleryPreview
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 5
                            height: 72
                            source: galleryDelegate.thumbnailUrl
                            visible: !galleryDelegate.isDirectory
                            sourceSize: Qt.size(320, 240)
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectCrop
                        }
                        Image {
                            objectName: "galleryFolderIcon-" + galleryDelegate.index
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: 12
                            width: 52
                            height: 52
                            source: galleryDelegate.thumbnailUrl
                            visible: galleryDelegate.isDirectory
                            sourceSize: Qt.size(160, 160)
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            anchors.bottomMargin: 7
                            text: galleryDelegate.fileName
                            elide: Text.ElideMiddle
                            color: Theme.graphiteInk
                            font.family: Theme.uiFont
                            font.pixelSize: 10
                        }
                        MouseArea {
                            id: galleryMouse
                            objectName: "galleryMouse-" + galleryDelegate.index
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.BackButton | Qt.ForwardButton
                            onClicked: function (mouse) {
                                if (mouse.button === Qt.BackButton) {
                                    root.controller.navigateBack();
                                    return;
                                }
                                if (mouse.button === Qt.ForwardButton) {
                                    root.controller.navigateForward();
                                    return;
                                }
                                galleryStrip.currentIndex = galleryDelegate.index;
                                galleryWorkspace.showPreview(galleryDelegate.path,
                                                             galleryDelegate.thumbnailUrl,
                                                             galleryDelegate.fileName,
                                                             galleryDelegate.technicalLabel,
                                                             galleryDelegate.isDirectory);
                                if (mouse.button === Qt.RightButton) {
                                    if (!galleryDelegate.isSelected)
                                        root.controller.selectPath(galleryDelegate.path);
                                    galleryWorkspace.contextPath = galleryDelegate.path;
                                    galleryWorkspace.contextIsDirectory = galleryDelegate.isDirectory;
                                    const point = mapToItem(root, mouse.x, mouse.y);
                                    galleryWorkspace.showImageMenu(point.x, point.y);
                                    return;
                                }
                                const toggle = (mouse.modifiers & Qt.ControlModifier) || (mouse.modifiers & Qt.MetaModifier);
                                const extend = (mouse.modifiers & Qt.ShiftModifier);
                                root.controller.selectPath(galleryDelegate.path, extend, toggle);
                            }
                            onDoubleClicked: root.controller.activatePath(galleryDelegate.path)
                        }
                        DragHandler {
                            id: galleryDragHandler
                            acceptedButtons: Qt.LeftButton
                            target: null
                            onActiveChanged: {
                                if (active && !galleryDelegate.isSelected)
                                    root.controller.selectPath(galleryDelegate.path);
                            }
                        }
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }

            MouseArea {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 8
                height: 44
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.BackButton | Qt.ForwardButton
                onClicked: function (mouse) {
                    if (mouse.button === Qt.BackButton) {
                        root.controller.navigateBack();
                        return;
                    }
                    if (mouse.button === Qt.ForwardButton) {
                        root.controller.navigateForward();
                        return;
                    }
                    if (mouse.button === Qt.RightButton) {
                        const point = mapToItem(root, mouse.x, mouse.y);
                        workspaceContextMenu.popup(point.x, point.y);
                    } else {
                        root.controller.clearSelection();
                    }
                }
            }
        }
    }

    // One window-wide external drop target preserves the original behavior in every view mode,
    // including the large gallery preview. Internal thumbnail drags are intentionally ignored.
    DropArea {
        id: workspaceDropArea
        enabled: root.displayMode === 2 && root.workspaceController.paneCount === 1
        property bool externalDragActive: false
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: toolbar.bottom
        anchors.bottom: statusRail.top
        z: 80
        onEntered: function (drag) {
            externalDragActive = drag.source === null && drag.hasUrls;
            if (externalDragActive)
                drag.acceptProposedAction();
        }
        onExited: externalDragActive = false
        onDropped: function (drop) {
            if (drop.source === null && drop.hasUrls) {
                root.controller.copyDroppedUrls(drop.urls);
                drop.acceptProposedAction();
            }
            externalDragActive = false;
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 8
            visible: workspaceDropArea.externalDragActive
            color: "#0A7893A6"
            border.color: Theme.primaryButton
            border.width: 1
            radius: 8
        }
    }

    Rectangle {
        id: statusRail
        objectName: "statusRail"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 32
        color: Theme.paperWhite
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.opticalGray
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: root.displayMode === 2 && galleryWorkspace.pixelProbeText.length > 0
                  ? galleryWorkspace.pixelProbeText : root.workspaceController.statusText
            color: Theme.mutedInk
            font.family: Theme.monoFont
            font.pixelSize: 12
        }
    }

    AppMenu {
        id: workspaceContextMenu
        objectName: "workspaceContextMenu"
        AppMenuItem {
            text: Qt.platform.os === "osx" ? qsTr("Open in Finder")
                                          : qsTr("Open in File Explorer")
            onTriggered: root.controller.openCurrentDirectoryInFileManager()
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Refresh")
            shortcutText: Qt.platform.os === "osx" ? "⌘R" : "Ctrl+R"
            onTriggered: root.controller.refresh()
        }
        AppMenuItem {
            text: qsTr("Select all")
            shortcutText: Qt.platform.os === "osx" ? "⌘A" : "Ctrl+A"
            onTriggered: root.controller.selectAll()
        }
        AppMenuItem {
            text: qsTr("Paste")
            shortcutText: Qt.platform.os === "osx" ? "⌘V" : "Ctrl+V"
            enabled: root.controller.canPaste
            onTriggered: root.controller.pasteItems()
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("New folder…")
            shortcutText: Qt.platform.os === "osx" ? "⇧⌘N" : "Ctrl+Shift+N"
            onTriggered: newFolderDialog.open()
        }
        AppMenuSeparator {}
        AppMenu {
            title: qsTr("Sort by")
            AppMenuItem {
                objectName: "sortByNameAction"
                text: (root.controller.sortMode === 0 ? "✓  " : "    ") + "Name"
                onTriggered: root.controller.setSortMode(0)
            }
            AppMenuItem {
                objectName: "sortByTypeAction"
                text: (root.controller.sortMode === 3 ? "✓  " : "    ") + "Type"
                onTriggered: root.controller.setSortMode(3)
            }
            AppMenuItem {
                objectName: "sortBySizeAction"
                text: (root.controller.sortMode === 2 ? "✓  " : "    ") + "Size"
                onTriggered: root.controller.setSortMode(2)
            }
            AppMenuItem {
                objectName: "sortByModifiedAction"
                text: (root.controller.sortMode === 1 ? "✓  " : "    ") + "Date modified"
                onTriggered: root.controller.setSortMode(1)
            }
        }
    }

    AppMenu {
        id: galleryFileContextMenu
        objectName: "galleryFileContextMenu"
        AppMenuItem {
            text: qsTr("Open full screen")
            onTriggered: root.controller.activatePath(galleryWorkspace.contextPath)
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
        id: galleryRawFileContextMenu
        objectName: "galleryRawFileContextMenu"
        AppMenuItem { text: qsTr("Open full screen"); onTriggered: root.controller.activatePath(galleryWorkspace.contextPath) }
        AppMenuItem { text: qsTr("Compare selected"); enabled: root.workspaceController.canCompare; onTriggered: root.workspaceController.compareSelected() }
        AppMenuItem { text: qsTr("RAW/YUV parameters…"); enabled: root.controller.canEditRaw; onTriggered: root.controller.editSelectedRawParameters() }
        AppMenuItem { visible: root.controller.canRestoreSelected; text: qsTr("Restore original"); onTriggered: root.controller.restoreSelected() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Cut"); shortcutText: Qt.platform.os === "osx" ? "⌘X" : "Ctrl+X"; onTriggered: root.controller.copySelected(true) }
        AppMenuItem { text: qsTr("Copy"); shortcutText: Qt.platform.os === "osx" ? "⌘C" : "Ctrl+C"; onTriggered: root.controller.copySelected(false) }
        AppMenuItem { text: qsTr("Rename…"); enabled: root.controller.selectionCount === 1; onTriggered: root.controller.renameSelected() }
        AppMenuItem { text: qsTr("Move to Trash"); destructive: true; onTriggered: root.controller.moveSelectedToTrash() }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("Reveal in Finder / Explorer"); enabled: root.controller.selectionCount === 1; onTriggered: root.controller.revealSelected() }
        AppMenuItem { text: qsTr("Properties"); enabled: root.controller.selectionCount === 1; onTriggered: root.controller.showSelectedProperties() }
    }

    // Separate folder/file menus avoid invisible menu rows reserving vertical space.
    AppMenu {
        id: galleryFolderContextMenu
        objectName: "galleryFolderContextMenu"
        AppMenuItem {
            text: qsTr("Open folder")
            onTriggered: root.controller.activatePath(galleryWorkspace.contextPath)
        }
        AppMenuItem {
            text: qsTr("Paste into folder")
            enabled: root.controller.canPaste
            onTriggered: root.controller.pasteItemsInto(galleryWorkspace.contextPath)
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

    PlatformDialogs.FolderDialog {
        id: folderPicker
        title: qsTr("Open folder")
        onAccepted: root.controller.openDirectoryUrl(selectedFolder)
    }

    AppTextInputDialog {
        id: newFolderDialog
        objectName: "newFolderDialog"
        parent: root
        dialogTitle: qsTr("New folder")
        description: qsTr("Create a folder in ") + root.controller.currentFolderName
        initialText: "New folder"
        acceptText: qsTr("Create")
        onSubmitted: function(text) {
            complete(root.controller.createFolder(text))
        }
    }

    AppTextInputDialog {
        id: renameDialog
        objectName: "renameDialog"
        parent: root
        dialogTitle: qsTr("Rename item")
        description: qsTr("Enter a new name for the selected item")
        acceptText: qsTr("Rename")
        onSubmitted: function(text) {
            complete(root.controller.renameSelectedTo(text))
        }
    }

    AppConfirmDialog {
        id: trashDialog
        objectName: "trashConfirmationDialog"
        parent: root
        dialogTitle: qsTr("Move to Trash?")
        confirmText: qsTr("Move")
        destructive: true
        onConfirmed: {
            complete(root.controller.moveSelectedToTrashConfirmed())
        }
    }

    AppConfirmDialog {
        id: transferDialog
        objectName: "transferConfirmationDialog"
        parent: root
        property bool transferPending: false
        destructive: false
        onConfirmed: {
            transferPending = false
            root.workspaceController.confirmPendingTransfer()
            complete("")
        }
        onClosed: {
            if (transferPending) {
                transferPending = false
                root.workspaceController.cancelPendingTransfer()
            }
        }
    }

    ImagePropertiesDialog {
        id: propertiesDialog
        parent: root
        controller: root.propertiesController
        iconPrefix: root.iconPrefix
    }

    RawParametersDialog {
        id: rawParametersDialog
        parent: root
        controller: root.rawController
        iconPrefix: root.iconPrefix
    }

    Connections {
        target: root.controller
        ignoreUnknownSignals: true

        function onDirectorySelectionRequested(initialFolder) {
            folderPicker.currentFolder = initialFolder
            folderPicker.open()
        }

        function onRenameRequested(currentName) {
            renameDialog.openWith(currentName)
        }

        function onTrashConfirmationRequested(itemCount) {
            if (root.settingsController && !root.settingsController.confirmTrash) {
                const error = root.controller.moveSelectedToTrashConfirmed()
                if (error.length > 0) {
                    trashDialog.showConfirmation()
                    trashDialog.complete(error)
                }
                return
            }
            trashDialog.message = itemCount === 1
                    ? qsTr("The selected item will be moved to the system Trash.")
                    : qsTr("%1 selected items will be moved to the system Trash.").arg(itemCount)
            trashDialog.showConfirmation()
        }

        function onPropertiesRequested(path) {
            propertiesDialog.openForPath(path)
        }

        function onFullScreenRequested(paths, initialIndex) {
            root.fullScreenRequested(paths, initialIndex)
        }

        function onRawParametersRequested(path) {
            rawParametersDialog.openForPath(path)
        }
    }

    Connections {
        target: root.workspaceController
        ignoreUnknownSignals: true

        function onTransferConfirmationRequested(move, itemCount, targetDirectory) {
            transferDialog.dialogTitle = move ? qsTr("Move items?") : qsTr("Copy items?")
            transferDialog.confirmText = move ? qsTr("Move") : qsTr("Copy")
            const target = String(targetDirectory)
            if (itemCount === 1) {
                transferDialog.message = move
                        ? qsTr("Move 1 item to “%1”?").arg(target)
                        : qsTr("Copy 1 item to “%1”?").arg(target)
            } else {
                transferDialog.message = move
                        ? qsTr("Move %1 items to “%2”?").arg(itemCount).arg(target)
                        : qsTr("Copy %1 items to “%2”?").arg(itemCount).arg(target)
            }
            transferDialog.transferPending = true
            transferDialog.showConfirmation()
        }
    }

    Shortcut {
        sequence: root.configuredShortcut("openFolder", "Ctrl+O")
        onActivated: root.controller.chooseDirectory()
    }
    Shortcut {
        sequence: root.configuredShortcut("find", "Ctrl+F")
        onActivated: {
            toolbar.searchControl.forceActiveFocus();
            toolbar.searchControl.selectAll();
        }
    }
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: root.fileShortcutsEnabled
        onActivated: root.controller.copySelected(false)
    }
    Shortcut {
        sequences: [StandardKey.Cut]
        enabled: root.fileShortcutsEnabled
        onActivated: root.controller.copySelected(true)
    }
    Shortcut {
        sequences: [StandardKey.Paste]
        enabled: root.fileShortcutsEnabled
        onActivated: root.controller.pasteItems()
    }
    Shortcut {
        sequence: root.configuredShortcut("toggleNavigator", "Ctrl+B")
        onActivated: root.navigatorVisible = !root.navigatorVisible
    }
    Shortcut {
        sequence: root.configuredShortcut("rename", "F2")
        enabled: root.fileShortcutsEnabled
        onActivated: root.controller.renameSelected()
    }
    Shortcut {
        sequence: root.configuredShortcut("compare", "C")
        enabled: root.fileShortcutsEnabled && root.workspaceController.canCompare
        onActivated: root.workspaceController.compareSelected()
    }
    Shortcut {
        sequence: "Delete"
        enabled: root.fileShortcutsEnabled && root.controller.selectionCount > 0
        onActivated: root.controller.moveSelectedToTrash()
    }
    Shortcut {
        sequences: [StandardKey.SelectAll]
        enabled: root.fileShortcutsEnabled
        onActivated: root.controller.selectAll()
    }
    Shortcut {
        sequence: root.configuredShortcut("newFolder", "Ctrl+Shift+N")
        enabled: root.fileShortcutsEnabled
        onActivated: newFolderDialog.open()
    }
    Shortcut {
        sequences: [StandardKey.Back]
        enabled: root.fileShortcutsEnabled && root.controller.canGoBack
        onActivated: root.controller.navigateBack()
    }
    Shortcut {
        sequences: [StandardKey.Forward]
        enabled: root.fileShortcutsEnabled && root.controller.canGoForward
        onActivated: root.controller.navigateForward()
    }
    Shortcut {
        sequence: "Alt+Up"
        enabled: root.fileShortcutsEnabled && root.controller.canGoUp
        onActivated: root.controller.navigateUp()
    }
    Shortcut {
        sequence: "Alt+Return"
        enabled: root.fileShortcutsEnabled && root.controller.selectionCount === 1
        onActivated: root.controller.showSelectedProperties()
    }
    Shortcut {
        sequences: [StandardKey.Refresh]
        enabled: root.fileShortcutsEnabled
        onActivated: root.controller.refresh()
    }
    Shortcut {
        sequence: "Return"
        enabled: root.fileShortcutsEnabled && root.controller.selectionCount === 1
        onActivated: root.controller.activatePath(root.controller.selectedPaths[0])
    }
    Shortcut {
        sequence: "Backspace"
        enabled: Qt.platform.os === "osx" && root.fileShortcutsEnabled &&
                 root.controller.selectionCount > 0
        onActivated: root.controller.moveSelectedToTrash()
    }
}
