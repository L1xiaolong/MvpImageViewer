import QtQuick

ImageCanvas {
    id: root
    property var controller: null
    property var settingsController: null
    presentationMode: 0
    viewSynchronized: false
    backgroundColor: Theme.canvasBackground
    smoothDisplay: !settingsController || settingsController.smoothDisplay === undefined
                   ? true : settingsController.smoothDisplay

    onControllerChanged: {
        if (controller)
            controller.attachCanvas(root)
    }
    Component.onCompleted: {
        if (controller)
            controller.attachCanvas(root)
    }

    // A native borderless-fullscreen transition can leave QQuickRhiItem hover delivery stale
    // until the next click on some platforms. Feed the handler position into the canvas probe as
    // well; the canvas drops it while it disagrees with the live cursor and keeps its own cursor
    // poll authoritative.
    HoverHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onPointChanged: root.probePixelAt(point.position)
        onHoveredChanged: {
            if (hovered)
                root.probePixelAt(point.position)
            else
                root.clearPixelProbe()
        }
    }
}
