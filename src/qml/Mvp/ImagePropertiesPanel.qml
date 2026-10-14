pragma ComponentBehavior: Bound
import QtQuick
import "."

ImagePropertiesContent {
    id: root
    objectName: "imagePropertiesPanel"
    property var browseController: null
    property bool hasActivePane: true
    selectionCount: hasActivePane && browseController ? browseController.selectionCount : 0
    readonly property string selectedPath: selectionCount === 1 && browseController
        ? browseController.selectedPaths[0] : ""
    function synchronize() {
        if (!controller) return
        if (!inspectionActive || !hasActivePane || !selectedPath) controller.clear()
        else if (controller.path !== selectedPath) controller.loadPath(selectedPath, true)
    }
    onSelectedPathChanged: synchronize()
    onBrowseControllerChanged: synchronize()
    onInspectionActiveChanged: synchronize()
    onHasActivePaneChanged: synchronize()
    Component.onCompleted: synchronize()
    Keys.onEscapePressed: closeRequested()
    Connections {
        target: root.browseController
        ignoreUnknownSignals: true
        function onCurrentDirectoryChanged() {
            if (root.controller) root.controller.clear()
            Qt.callLater(root.synchronize)
        }
        function onImageTransformed(path) {
            if (root.inspectionActive && root.controller && root.controller.path === path)
                root.controller.reload()
        }
    }
}
