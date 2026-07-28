import QtQuick
import QtQuick.Controls

Menu {
    id: control
    implicitWidth: 220
    padding: 6
    topPadding: 6
    bottomPadding: 6
    margins: 8

    delegate: AppMenuItem {}

    background: Rectangle {
        color: Theme.raisedSurface
        radius: 8
        border.color: Theme.opticalGray
        border.width: 1
    }
}
