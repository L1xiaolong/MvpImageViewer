import QtQuick
import "../src/qml/Mvp"

Item {
    id: root
    width: 1000
    height: 860

    Rectangle { anchors.fill: parent; color: Theme.sensorWhite }
    MockRawParametersController { id: mockRaw }

    RawParametersDialog {
        id: productionRawParametersDialog
        parent: root
        controller: mockRaw
        iconPrefix: Qt.resolvedUrl("../assets/icons/ui/").toString()
        Component.onCompleted: openForPath(mockRaw.path)
    }
}
