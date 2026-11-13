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
        function paneHeader(index) { return findChild(findChild(page(), "browserPane-" + index), "paneHeader-" + index) }
        function typeSearch(text) { for (let i = 0; i < text.length; ++i) keyClick(text.toUpperCase().charCodeAt(i)) }
        function test_unified_header_controls_fit() {
            page().workspaceController.paneCount = 1
            wait(30)
            page().titleBarHeight = 44
            page().captionButtonsWidth = 138
            const toolbar = findChild(page(), "topToolbar")
            waitForRendering(toolbar)
            compare(toolbar.height, 44)
            compare(findChild(toolbar, "browserSearchField"), null)
            verify(!findChild(toolbar, "toolbarSettingsButton").visible)
            const settings = findChild(page(), "settingsButton")
            verify(settings.visible && settings.y >= 0)
            const header = paneHeader(0)
            const search = findChild(header, "paneSearchField-0")
            const path = findChild(header, "paneLocationFrame-0")
            compare(header.height, 38)
            verify(!header.searchVisible)
            verify(path.width > header.width - 170)
        }
        function test_search_is_scoped_to_each_pane() {
            const workspace = page().workspaceController
            workspace.panes[1].currentDirectory = "/Images/Second"
            const first = paneHeader(0)
            const second = paneHeader(1)
            const firstSearch = findChild(first, "paneSearchField-0")
            const secondSearch = findChild(second, "paneSearchField-1")
            first.focusSearch()
            typeSearch("camera")
            second.focusSearch()
            typeSearch("sensor")
            tryCompare(workspace.panes[0], "filterText", "camera")
            tryCompare(workspace.panes[1], "filterText", "sensor")
            compare(firstSearch.text, "camera")
            compare(secondSearch.text, "sensor")
            compare(workspace.activePaneIndex, 1)
            verify(page().searchInputFocused)
            verify(!page().fileShortcutsEnabled)
            first.focusSearch()
            const clear = findChild(first, "paneCloseSearchButton-0")
            mouseClick(clear)
            tryCompare(workspace.panes[0], "filterText", "")
            compare(workspace.panes[1].filterText, "sensor")
            compare(workspace.activePaneIndex, 0)
            second.closeSearch()
            wait(30) // Drain the navigator's queued reveal before destroying the preview.
        }
        function test_compact_search_and_gallery() {
            page().workspaceController.paneCount = 3
            wait(80)
            const header = paneHeader(0)
            const search = findChild(header, "paneSearchField-0")
            const path = findChild(header, "paneLocationFrame-0")
            compare(header.height, 38)
            verify(!header.searchVisible)
            header.focusSearch()
            tryCompare(header, "searchVisible", true)
            const popup = findChild(header, "paneSearchPopup-0")
            verify(popup.width <= header.width - 16)
            verify(popup.x >= 8 && popup.x + popup.width <= header.width - 8)
            typeSearch("narrow")
            tryCompare(page().controller, "filterText", "narrow")
            keyClick(Qt.Key_Escape)
            tryCompare(header, "searchVisible", false)
            tryCompare(page().controller, "filterText", "")
            verify(path.width > 80)
            page().workspaceController.paneCount = 1
            page().workspaceController.setActiveDisplayMode(2)
            wait(50)
            const activeHeader = page().activePaneHeader()
            verify(activeHeader && activeHeader.visible)
            activeHeader.focusSearch()
            typeSearch("gallery")
            tryCompare(page().controller, "filterText", "gallery")
        }
        function test_settings_language_switch() {
            const settings = findChild(page(), "settingsButton")
            compare(settings.text, "Settings")
            testSettings.language = "zh_CN"
            tryCompare(settings, "text", "设置")
            testSettings.language = "en"
            tryCompare(settings, "text", "Settings")
        }
        function test_search_shortcut_opens_active_pane() {
            const workspace = page().workspaceController
            workspace.activatePane(2)
            const header = paneHeader(2)
            verify(!header.searchVisible)
            page().forceActiveFocus()
            keyClick(Qt.Key_F, Qt.ControlModifier)
            tryCompare(header, "searchVisible", true)
            const search = findChild(header, "paneSearchField-2")
            tryVerify(function() { return search.activeFocus })
            typeSearch("shortcut")
            tryCompare(workspace.panes[2], "filterText", "shortcut")
            compare(workspace.panes[0].filterText, "")
            keyClick(Qt.Key_Escape)
            tryCompare(header, "searchVisible", false)
            compare(workspace.panes[2].filterText, "")
        }
        function test_search_popup_capture_data() {
            return [{tag:"light", dark:false}, {tag:"dark", dark:true}]
        }
        function test_search_popup_capture(data) {
            page().workspaceController.paneCount = 1
            Theme.darkMode = data.dark
            page().iconPrefix = Qt.resolvedUrl("../../assets/icons/" + (data.dark ? "ui-dark/" : "ui/"))
            wait(50)
            const header = paneHeader(0)
            const headerHeight = header.height
            header.focusSearch()
            tryCompare(header, "searchVisible", true)
            compare(header.height, headerHeight)
            waitForRendering(scene.Window.window.contentItem)
            verify(testCapture.save(scene.Window.window.contentItem,
                testArtifactDir + "/floating-search-" + data.tag + ".png"))
            header.closeSearch()
        }
        function test_shortcut_and_accessibility() {
            const toolbar = findChild(page(), "topToolbar")
            const expected = Qt.platform.os === "osx" ? "⌘F" : "Ctrl+F"
            const header = paneHeader(0)
            const search = findChild(header, "paneSearchField-0")
            verify(search.Accessible.name.length > 0)
            compare(testSettings.setShortcut("find", "Ctrl+Shift+F"), "")
            const entry = testSettings.shortcutEntries.filter(function(item) { return item.id === "find" })[0]
            header.focusSearch()
            tryVerify(function() { return search.activeFocus })
            const button = findChild(toolbar, "openFolderButton")
            verify(button.Accessible.name.length > 0)
            scene.Window.window.requestActivate()
            button.forceActiveFocus(Qt.TabFocusReason)
            tryVerify(function() { return button.activeFocus && button.ToolTip.visible })
            compare(findChild(toolbar, "compareButton").text, "")
            verify(findChild(header, "paneCloseSearchButton-0").Accessible.name.length > 0)
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
            tryCompare(findChild(page(), "paneSearchField-0"), "text", "")
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
            if (data.mode === 1) {
                const metadata = findChild(tile, "listTechnicalLabel")
                verify(metadata.mapToItem(tile, 0, metadata.height).y <= tile.height,
                       "List metadata must stay within its row")
            }
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
        function test_bounded_path_tooltip_data() {
            return [{tag:"light", dark:false}, {tag:"dark", dark:true}]
        }
        function test_bounded_path_tooltip(data) {
            Theme.darkMode = data.dark
            page().iconPrefix = Qt.resolvedUrl("../../assets/icons/" + (data.dark ? "ui-dark/" : "ui/"))
            const path = "E:/项目/" + "相机模组与镜头性能检查/".repeat(6) + "样张_20261113_<original>.yuv"
            const tile = createTemporaryObject(tileComponent, scene, {
                x:300, y:180, width:220, controller:page().controller,
                fileName:"样张_20261113_<original>.yuv", path:path
            })
            verify(tile)
            waitForRendering(tile)
            const tip = findChild(tile, "thumbnailPathToolTip")
            mouseMove(tile, 100, 30)
            tryVerify(function() { return tip.opened }, 1800)
            verify(tip.width <= 480)
            compare(tip.text, path)
            const name = findChild(tip.contentItem, "pathTooltipName")
            const directory = findChild(tip.contentItem, "pathTooltipDirectory")
            compare(name.text, tile.fileName)
            compare(name.textFormat, Text.PlainText)
            verify(directory.text.indexOf("相机模组与镜头性能检查") >= 0)
            verify(directory.height > 14, "Long directories must wrap")
            verify(testCapture.save(scene.Window.window.contentItem, testArtifactDir + "/path-tooltip-" + data.tag + ".png"))
            mouseMove(scene, scene.width - 1, scene.height - 1)
        }
        function test_status_summary_language() {
            const workspace = page().workspaceController
            workspace.paneCount = 1
            testSettings.language = "zh_CN"
            const status = findChild(page(), "browserStatusLabel")
            tryVerify(function() { return status.text.indexOf("已选") >= 0 })
            verify(status.text.indexOf("窗格") < 0)
            testSettings.language = "en"
            tryVerify(function() { return status.text.indexOf("selected") >= 0 })
            workspace.paneCount = 3
            tryVerify(function() { return status.text.indexOf("from") >= 0 })
            testSettings.language = "zh_CN"
            tryVerify(function() { return status.text.indexOf("来自") >= 0 && status.text.indexOf("窗格") >= 0 })
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
        function test_visual_grid_capture_data() {
            return [{tag:"light-small", w:980, dark:false}, {tag:"dark-small", w:980, dark:true},
                    {tag:"light-wide", w:1936, dark:false}, {tag:"dark-wide", w:1936, dark:true}]
        }
        function test_visual_grid_capture(data) {
            scene.width = data.w
            scene.Window.window.width = data.w
            scene.Window.window.height = 720
            scene.height = 720
            page().workspaceController.paneCount = 1
            page().workspaceController.setActiveDisplayMode(0)
            Theme.darkMode = data.dark
            page().iconPrefix = Qt.resolvedUrl("../../assets/icons/" + (data.dark ? "ui-dark/" : "ui/"))
            const model = page().controller.thumbnails
            for (let i = 0; i < model.count; ++i)
                model.setProperty(i, "thumbnailUrl", testCapture.thumbnailFixture(i % 2 === 0).toString())
            wait(150)
            const view = findChild(page(), "paneContactSheet-0")
            verify(view.gridColumns >= 1)
            verify(view.width - view.gridColumns * view.cellWidth < view.gridColumns,
                   "Grid must distribute trailing space across its columns")
            compare(findChild(page(), "brandIcon"), null)
            compare(findChild(page(), "browserPane-0").border.width, 0)
            verify(testCapture.save(loader.item, testArtifactDir + "/modern-grid-" + data.tag + ".png"))
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
            verify(compareButton.width > 0, "Compare action remains available")
            verify(findChild(page(), "settingsButton").visible, "Settings stays in the sidebar")
            verify(testCapture.save(loader.item, testArtifactDir + "/browse-" + data.tag + ".png"))
        }
    }
}
