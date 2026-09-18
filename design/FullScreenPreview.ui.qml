import QtQuick
import "../src/qml/Pages"

Item {
    id: root
    width: 1440
    height: 900

    MockFullScreenController { id: mockFullScreen }
    MockImagePropertiesController { id: mockProperties }
    QtObject {
        id: mockSettings
        property bool smoothDisplay: true
        property bool confirmTrash: true
    }

    FullScreenPage {
        id: productionFullScreenPage
        anchors.fill: parent
        controller: mockFullScreen
        propertiesController: mockProperties
        settingsController: mockSettings
        designMode: true
        iconPrefix: Qt.resolvedUrl("../assets/icons/ui/").toString()
        Component.onCompleted: {
            open(mockFullScreen.paths, 0)
            showPropertiesDialog()
            pixelText = "(1842,1064) RGB(80,120,160)"
        }
    }
}
