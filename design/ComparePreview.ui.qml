import QtQuick
import "../src/qml/Pages"

Item {
    id: root
    width: 1440
    height: 900

    MockCompareController {
        id: mockCompareController
    }

    ComparePage {
        id: productionComparePage
        anchors.fill: parent
        controller: mockCompareController
        designMode: true
    }

    Component.onCompleted: productionComparePage.open([
        "/Images/Demo/sample_0001.jpg",
        "/Images/Outdoor samples/sample_0002.jpg"
    ])
}
