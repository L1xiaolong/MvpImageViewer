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
}
