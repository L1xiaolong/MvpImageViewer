import QtQuick

ImageCanvas {
    id: root

    property var controller: null

    presentationMode: controller ? controller.presentationMode : 0
    compareAmount: controller ? controller.splitAmount : 0.5
    viewSynchronized: true
    backgroundColor: Theme.canvasBackground

    onControllerChanged: {
        if (controller)
            controller.attachCanvas(root)
    }
    Component.onCompleted: {
        if (controller)
            controller.attachCanvas(root)
    }
    onCompareAmountChanged: {
        if (controller)
            controller.setSplitAmount(root.compareAmount)
    }
}
