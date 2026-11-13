import QtQuick
import QtTest
import "../../src/qml/Mvp"

Rectangle {
    id: scene
    width: 276
    height: 44
    color: Theme.raisedSurface
    QtObject {
        id: idle
        property int hoveredCaptionButton: -1
        property int pressedCaptionButton: -1
        function activateCaptionButton(index) {}
    }
    QtObject {
        id: hovered
        property int hoveredCaptionButton: 2
        property int pressedCaptionButton: -1
        function activateCaptionButton(index) {}
    }
    WindowCaptionButtons { id: normal; controller: idle }
    WindowCaptionButtons { x: 138; controller: hovered }
    TestCase {
        name: "WindowCaption"
        when: windowShown
        function cleanup() { Theme.darkMode = false }
        function test_glyph_alignment_and_capture_data() {
            return [{tag:"light", dark:false}, {tag:"dark", dark:true}]
        }
        function test_glyph_alignment_and_capture(data) {
            Theme.darkMode = data.dark
            for (const name of ["windowMinimizeButton", "windowMaximizeButton", "windowCloseButton"]) {
                const button = findChild(normal, name)
                const glyph = findChild(button, "windowCaptionGlyph")
                compare(button.height, 44)
                compare(glyph.width, 12)
                const center = glyph.mapToItem(button, 6, 6)
                compare(center.x, button.width / 2)
                compare(center.y, button.height / 2)
                tryCompare(glyph, "status", Image.Ready)
            }
            waitForRendering(scene)
            verify(testCapture.save(scene, testArtifactDir + "/caption-" + data.tag + ".png"))
        }
    }
}
