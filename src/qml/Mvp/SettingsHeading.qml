import QtQuick

Text {
    width: parent ? parent.width : implicitWidth
    topPadding: 16
    bottomPadding: 8
    color: Theme.graphiteInk
    font.family: Theme.uiFont
    font.pixelSize: Theme.bodyFontSize
    font.weight: Theme.emphasisFontWeight
    wrapMode: Text.Wrap
}
