pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "."

Dialog {
    id: root
    objectName: "imagePropertiesDialog"
    property var controller: null
    property string iconPrefix: Theme.iconPrefix
    property alias currentTab: content.currentTab
    property point dragPointerOrigin: Qt.point(0, 0)
    property point dragDialogOrigin: Qt.point(0, 0)
    modal: true
    focus: true
    width: parent ? Math.min(600, parent.width - 48) : 600
    height: parent ? Math.min(680, parent.height - 40) : 680
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 1
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: Theme.overlay }
    background: Rectangle {
        color: Theme.paperWhite
        radius: 8
        border.color: Theme.opticalGray
    }
    function openForPath(path) {
        if (controller) controller.loadPath(path)
        open()
    }
    function clampPosition() {
        if (!parent) return
        x = Math.max(0, Math.min(Math.max(0, parent.width - width), x))
        y = Math.max(0, Math.min(Math.max(0, parent.height - height), y))
    }
    onAboutToShow: {
        x = parent ? Math.round((parent.width - width) / 2) : 0
        y = parent ? Math.round((parent.height - height) / 2) : 0
    }
    onWidthChanged: if (visible) clampPosition()
    onHeightChanged: if (visible) clampPosition()
    Connections {
        target: root.parent
        function onWidthChanged() { if (root.visible) root.clampPosition() }
        function onHeightChanged() { if (root.visible) root.clampPosition() }
    }
    onClosed: if (controller) controller.clear()
    contentItem: ImagePropertiesContent {
        id: content
        controller: root.controller
        iconPrefix: root.iconPrefix
        inspectionActive: root.visible
        draggable: true
        onHeaderDragStarted: function(position) {
            root.dragPointerOrigin = position
            root.dragDialogOrigin = Qt.point(root.x, root.y)
        }
        onHeaderDragMoved: function(position) {
            root.x = root.dragDialogOrigin.x + position.x - root.dragPointerOrigin.x
            root.y = root.dragDialogOrigin.y + position.y - root.dragPointerOrigin.y
            root.clampPosition()
        }
        onCloseRequested: root.close()
    }
}
