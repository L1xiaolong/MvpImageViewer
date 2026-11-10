import QtQuick
import QtQuick.Controls
import QtTest
import "../../src/qml/Mvp"

Item {
    width: 600
    height: 500
    QtObject {
        id: fakeController
        property size selectedImageSize: Qt.size(800, 600)
        property bool transformBusy: false
        property int requests: 0
        signal transformFinished(string path, string error)
        function resizeSelected(width, height) {
            ++requests
            transformBusy = true
            return ""
        }
    }
    ImageResizeDialog { id: dialog; controller: fakeController }
    TestCase {
        name: "AsyncImageResize"
        when: windowShown
        function test_error_and_retry() {
            dialog.openForSelection()
            tryCompare(dialog, "opened", true)
            dialog.applyResize()
            verify(dialog.awaitingResize)
            verify(dialog.opened)
            dialog.applyResize()
            compare(fakeController.requests, 1)
            fakeController.transformBusy = false
            fakeController.transformFinished("image.png", "Write failed")
            verify(!dialog.awaitingResize)
            verify(dialog.opened)
            compare(dialog.errorText, "Write failed")
            dialog.applyResize()
            compare(fakeController.requests, 2)
            fakeController.transformBusy = false
            fakeController.transformFinished("image.png", "")
            tryCompare(dialog, "opened", false)
        }
    }
}
