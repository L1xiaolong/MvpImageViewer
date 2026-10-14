pragma ComponentBehavior: Bound
import QtQuick
import "."

RawParametersContent {
    id: root
    objectName: "rawParametersPanel"
    property var browseController: null
    property bool hasActivePane: true
    selectionCount: hasActivePane && browseController ? browseController.selectionCount : 0
    readonly property string selectedPath: selectionCount === 1 && browseController && browseController.canEditRaw
        ? browseController.selectedPaths[0] : ""
    function synchronize() {
        if (!controller) return
        if (!inspectionActive || !selectedPath) {
            finishEditing()
            if (controller.path.length > 0) controller.clear()
        } else if (controller.path !== selectedPath) {
            finishEditing()
            controller.loadPath(selectedPath)
        }
    }
    onSelectedPathChanged: synchronize()
    onBrowseControllerChanged: synchronize()
    onInspectionActiveChanged: synchronize()
    onHasActivePaneChanged: synchronize()
    Component.onCompleted: synchronize()
    Connections {
        target: root.browseController
        ignoreUnknownSignals: true
        function onCurrentDirectoryChanged() {
            root.finishEditing()
            if (root.controller) root.controller.clear()
            Qt.callLater(root.synchronize)
        }
    }
}
