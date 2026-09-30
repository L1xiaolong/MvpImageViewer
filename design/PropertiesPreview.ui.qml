import QtQuick
import "../src/qml/Mvp"

Item {
    id: root
    width: 1000
    height: 820

    Rectangle {
        anchors.fill: parent
        color: Theme.sensorWhite
    }

    MockImagePropertiesController {
        id: mockProperties
    }

    ImagePropertiesDialog {
        id: productionPropertiesDialog
        parent: root
        controller: mockProperties
        iconPrefix: Qt.resolvedUrl("../assets/icons/ui/").toString()
        Component.onCompleted: openForPath(mockProperties.path)
    }
}
