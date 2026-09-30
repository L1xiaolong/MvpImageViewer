import QtQuick
import "../src/qml/Pages"

// Design Studio composition root.
// This file contains no duplicated visual implementation: every visible item
// comes from the production BrowsePage and its shared Mvp components.
Item {
    id: root
    width: 1440
    height: 900

    MockBrowseWorkspace {
        id: mockWorkspace
    }

    MockImagePropertiesController {
        id: mockImageProperties
    }

    MockRawParametersController {
        id: mockRawParameters
    }

    BrowsePage {
        id: productionBrowsePage
        anchors.fill: parent
        controller: mockWorkspace.activePane
        workspaceController: mockWorkspace
        propertiesController: mockImageProperties
        rawController: mockRawParameters
        designMode: true
        iconPrefix: Qt.resolvedUrl("../assets/icons/ui/").toString()
    }
}
