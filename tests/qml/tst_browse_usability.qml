import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import "../../design" as Design
import "../../src/qml/Mvp"

Item {
    id: scene
    width: 980
    height: 640
    Loader { id: loader; anchors.fill: parent }
    Component { id: previewComponent; Design.DesignPreview {} }
    Component { id: tileComponent; ThumbnailTile {} }
    Component { id: comparisonComponent; Design.ComparePreview {} }
    TestCase {
        name: "BrowseUsability"
        when: windowShown
        function init() {
            testSettings.restoreDefaults()
            testSettings.language = "en"
            Theme.darkMode = false
            loader.sourceComponent = previewComponent
            tryVerify(function() { return loader.status === Loader.Ready })
            page().settingsController = testSettings
            scene.Window.window.requestActivate()
            waitForRendering(loader.item)
        }
        function cleanup() { loader.sourceComponent = undefined; Theme.darkMode = false }
        function page() { return findChild(loader.item, "browsePage") }
        function test_shortcut_and_accessibility() {
            const toolbar = findChild(page(), "topToolbar")
            const expected = Qt.platform.os === "osx" ? "⌘F" : "Ctrl+F"
            compare(toolbar.searchShortcutText, expected)
            compare(testSettings.setShortcut("find", "Ctrl+Shift+F"), "")
            const entry = testSettings.shortcutEntries.filter(function(item) { return item.id === "find" })[0]
            tryCompare(toolbar, "searchShortcutText", entry.nativeSequence)
            verify(toolbar.searchShortcutText !== expected)
            const button = findChild(toolbar, "openFolderButton")
            verify(button.Accessible.name.length > 0)
            scene.Window.window.requestActivate()
            button.forceActiveFocus()
            tryVerify(function() { return button.activeFocus && button.ToolTip.visible })
            compare(findChild(toolbar, "compareButton").text, "")
            verify(findChild(toolbar, "clearSearchButton").Accessible.name.length > 0)
        }
        function test_empty_search_recovery() {
            const controller = page().controller
            controller.setFilterText("missing-image")
            controller.thumbnails.clear()
            const pane = findChild(page(), "browserPane-0")
            const message = findChild(pane, "paneEmptyMessage")
            tryVerify(function() { return message.visible && message.text.indexOf("missing-image") >= 0 })
            const clear = findChild(pane, "clearEmptySearchButton")
            waitForRendering(clear)
            verify(clear.visible && clear.enabled && clear.width > 0)
            mouseClick(clear, clear.width / 2, clear.height / 2)
            tryCompare(controller, "filterText", "")
            tryCompare(findChild(page(), "browserSearchField"), "text", "")
            verify(message.text.indexOf("No supported images") >= 0)
        }
        function test_single_selection_and_operations() {
            const name = "camera_module_exposure_0001_sensor_gain_16_timestamp_20261113.yuv"
            page().controller.selectedPaths = ["/Images/" + name]
            compare(findChild(page(), "selectedFileDetails"), null)
            const operations = findChild(page(), "imageOperationsButton")
            verify(operations.enabled)
            mouseClick(operations)
            const menu = findChild(page(), "imageOperationsMenu")
            tryCompare(menu, "opened", true)
            compare(menu.count, 3)
            keyClick(Qt.Key_Escape)
            tryCompare(menu, "opened", false)
        }
        function test_thumbnail_presentation_data() {
            return [{tag:"grid-wide", mode:0, portrait:false}, {tag:"grid-tall", mode:0, portrait:true},
                    {tag:"list-wide", mode:1, portrait:false}, {tag:"list-tall", mode:1, portrait:true}]
        }
        function test_thumbnail_presentation(data) {
            const tile = createTemporaryObject(tileComponent, scene, {
                width:300, displayMode:data.mode, controller:page().controller,
                settingsController:testSettings, fileName:"Inspection sample.png", fileType:"PNG",
                thumbnailUrl:testCapture.thumbnailFixture(data.portrait), selected:true, keyboardCurrent:true
            })
            verify(tile)
            tryVerify(function() { return tile.thumbnailReady })
            const image = findChild(tile, data.mode === 0 ? "gridImagePreview" : "listImagePreview")
            const geometry = [tile.width, tile.height, image.width, image.height]
            compare(image.fillMode, Image.PreserveAspectFit)
            verify(testCapture.save(tile, testArtifactDir + "/thumbnail-" + data.tag + "-fit.png"))
            testSettings.thumbnailPresentation = "fill"
            tryCompare(image, "fillMode", Image.PreserveAspectCrop)
            waitForRendering(tile)
            tryCompare(image, "status", Image.Ready)
            compare([tile.width, tile.height, image.width, image.height].join(), geometry.join())
            verify(testCapture.save(tile, testArtifactDir + "/thumbnail-" + data.tag + "-fill.png"))
        }
        function test_keyboard_focus_and_selection() {
            page().workspaceController.clearSelection()
            const view = findChild(page(), "paneContactSheet-0")
            view.currentIndex = 0
            view.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Down)
            tryVerify(function() { return view.currentIndex > 0 && view.keyboardFocusVisible })
            const current = view.currentItem.fileTile
            verify(current.keyboardCurrent)
            keyClick(Qt.Key_Space)
            tryVerify(function() { return current.selected })
            verify(findChild(current, "thumbnailKeyboardFocus").visible)
            view.forceActiveFocus(Qt.MouseFocusReason)
            view.keyboardNavigating = false
            verify(!view.keyboardFocusVisible)
        }
        function test_comparison_escape_return() {
            const preview = createTemporaryObject(comparisonComponent, scene, {width:980, height:640})
            verify(preview)
            const comparison = findChild(preview, "comparePage")
            comparison.iconPrefix = Qt.resolvedUrl("../../assets/icons/ui/")
            comparison.controller.thumbnailVisible = false
            let closed = 0
            comparison.closeRequested.connect(function() { ++closed })
            compare(findChild(preview, "compareBackButton"), null)
            comparison.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            compare(closed, 1)
        }
        function test_layout_capture_data() {
            return [{tag:"light-small", w:980, dark:false}, {tag:"dark-small", w:980, dark:true},
                    {tag:"light-wide", w:1440, dark:false}, {tag:"dark-wide", w:1440, dark:true}]
        }
        function test_layout_capture(data) {
            scene.width = data.w
            scene.Window.window.width = data.w
            scene.Window.window.height = 640
            Theme.darkMode = data.dark
            page().iconPrefix = Qt.resolvedUrl("../../assets/icons/" + (data.dark ? "ui-dark/" : "ui/"))
            wait(150)
            const toolbar = findChild(page(), "topToolbar")
            const compareButton = findChild(toolbar, "compareButton")
            const settings = findChild(toolbar, "settingsButton")
            verify(compareButton.x + compareButton.width + 8 <= settings.x, "Toolbar actions overlap")
            verify(testCapture.save(loader.item, testArtifactDir + "/browse-" + data.tag + ".png"))
        }
    }
}
