import QtQuick

Text {
    width: parent ? parent.width : implicitWidth
    topPadding: 20
    bottomPadding: 8
    color: Theme.graphiteInk
    font.family: Theme.uiFont
    font.pixelSize: 13
    font.weight: Font.DemiBold
    wrapMode: Text.Wrap
}
