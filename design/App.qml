import QtQuick
import QtQuick.Controls

ApplicationWindow {
    visible: true
    width: 1440
    height: 900
    minimumWidth: 980
    minimumHeight: 640
    title: "MVP Image Viewer · Design Preview"

    DesignPreview {
        anchors.fill: parent
    }
}
