import QtQuick
import QtQuick.Controls

// Fixed navigation overlays own a visible consumer independently of browser
// viewports. Clearing source also cancels Qt's pending image response.
Image {
    id: root
    required property string path
    readonly property var sceneWindow: root.Window.window
    source: root.visible && root.width > 0 && root.height > 0 && root.path.length > 0
            && sceneWindow && sceneWindow.visible && sceneWindow.visibility !== Window.Minimized
            ? "image://thumbnail/" + encodeURIComponent(root.path) + "?purpose=navigation" : ""
}
