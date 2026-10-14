import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtTest
import "../../design" as Design
import "../../src/qml/Mvp"
import "../../src/qml/Pages"

Item {
    id: scene
    width: 1440; height: 900
    Design.MockBrowseWorkspace { id: workspace; paneCount: 1 }
    Design.MockImagePropertiesController { id: controller; path: "" }
    Design.MockRawParametersController { id: raw }
    BrowsePage {
        id: browser
        anchors.fill: parent
        controller: workspace.activePane
        workspaceController: workspace
        propertiesController: controller
        rawController: raw
        designMode: true
        iconPrefix: Qt.resolvedUrl("../../assets/icons/" + (Theme.darkMode ? "ui-dark/" : "ui/")).toString()
    }
    Design.MockImagePropertiesController { id: fullController }
    ImagePropertiesDialog { id: dialog; parent: scene; controller: fullController; iconPrefix: browser.iconPrefix }
    TestCase {
        name: "PropertiesUI"
        property string rawDefaults: ""
        function initTestCase() { rawDefaults = JSON.stringify(raw.values) }
        when: windowShown
        function hasText(item, text) {
            if (item.text === text) return true
            for (let child of item.children || []) if (hasText(child, text)) return true
            return false
        }
        function keyClicks(text) { for (let character of text) keyClick(character.charCodeAt(0)) }
        function rawPanel() { return findChild(browser, "rawParametersPanel") }
        function rawControl(name) {
            const item = findChild(rawPanel(), name)
            verify(item !== null)
            const scroll = findChild(rawPanel(), "rawParameterScroll")
            const y = item.mapToItem(scroll, 0, 0).y + scroll.contentY
            scroll.contentY = Math.max(0, Math.min(y - 16, scroll.contentHeight - scroll.height))
            wait(40)
            return item
        }
        function panel() { return findChild(browser, "imagePropertiesPanel") }
        function size(w, h) { scene.width = w; scene.height = h; scene.Window.window.width = w; scene.Window.window.height = h; wait(80) }
        function init() {
            failOnWarning(/(TypeError|ReferenceError|Binding loop)/)
            size(1440, 900)
            browser.visible = true
            browser.inspectorMode = ""
            browser.inspectorWidth = 360
            browser.navigatorWidth = 272
            workspace.activePaneIndex = 0
            workspace.paneCount = 1
            workspace.activePane.currentDirectory = "/Images/Demo"
            workspace.activePane.currentFolderName = "Demo"
            workspace.activePane.statusText = "16 items · 1 selected"
            workspace.activePane.selectedPaths = ["/Images/Demo/sample_0001.jpg"]
            controller.clear()
            controller.scenario = "jpeg"
            controller.directory = false
            controller.warnings = []
            controller.loading = false
            controller.metadataLoading = false
            controller.hasHistogramSource = true
            controller.histogramRequests = 0
            panel().currentTab = 0
            raw.values = JSON.parse(rawDefaults)
            raw.edits = []
            raw.presetNames = ["6236_4178_MIPI_RAW10", "Reference daylight"]
            raw.selectedPreset = ""
            raw.flushCount = 0
            raw.appliedFolderCount = 0
            Theme.darkMode = false
            testSettings.language = "zh_CN"
        }
        function cleanup() { dialog.close(); browser.inspectorMode = ""; wait(30) }
        function test_selection_and_lifetime() {
            browser.forceActiveFocus()
            keyClick(Qt.Key_Return, Qt.AltModifier)
            verify(browser.propertiesVisible)
            compare(controller.path, workspace.activePane.selectedPaths[0])
            verify(browser.contentInteractionEnabled)
            compare(controller.histogramRequests, 0)
            workspace.activePane.selectedPaths = ["/Images/Demo/second.png"]
            tryCompare(controller, "fileName", "second.png")
            workspace.activePane.selectedPaths = []
            tryCompare(controller, "path", "")
            workspace.activePane.selectedPaths = ["a", "b"]
            compare(panel().selectionCount, 2)
            compare(controller.path, "")
            browser.closeProperties()
            compare(controller.path, "")
            verify(!browser.propertiesVisible)
        }
        function test_active_pane_and_surfaces() {
            workspace.paneCount = 2
            workspace.panes[0].selectedPaths = ["/Images/a.png"]
            workspace.panes[1].selectedPaths = ["/Images/b.png"]
            browser.showProperties()
            compare(controller.path, "/Images/a.png")
            workspace.activePaneIndex = 1
            tryCompare(controller, "path", "/Images/b.png")
            workspace.closePane(0)
            tryCompare(controller, "path", "/Images/b.png")
            dialog.openForPath("/Images/fullscreen.jpg")
            tryCompare(dialog, "opened", true)
            compare(controller.path, "/Images/b.png")
            compare(fullController.path, "/Images/fullscreen.jpg")
            dialog.close()
            tryCompare(fullController, "path", "")
            compare(controller.path, "/Images/b.png")
            browser.visible = false
            tryCompare(controller, "path", "")
            browser.visible = true
            tryCompare(controller, "path", "/Images/b.png")
        }
        function test_tabs_scroll_and_folder() {
            size(980, 640)
            browser.showProperties()
            wait(50)
            verify(findChild(browser, "copyProperty-path") === null)
            const scroll = findChild(browser, "imagePropertiesScroll")
            const savedY = Math.min(60, Math.max(0, scroll.contentHeight - scroll.height))
            verify(savedY > 0, "overview must scroll: " + scroll.contentHeight + "/" + scroll.height)
            scroll.contentY = savedY
            panel().selectTab(1)
            wait(60)
            verify(controller.histogramRequests > 0)
            const requests = controller.histogramRequests
            panel().selectTab(0)
            wait(60)
            verify(Math.abs(scroll.contentY - savedY) < 2, "scroll " + savedY + " -> " + scroll.contentY + ", content " + scroll.contentHeight + "/" + scroll.height)
            workspace.activePane.selectedPaths = ["/Images/Demo/new.jpg"]
            wait(50)
            compare(scroll.contentY, 0)
            panel().selectTab(2)
            controller.scenario = "raw"
            wait(50)
            verify(findChild(browser, "propertyField-color-matrix") !== null)
            controller.directory = true
            wait(50)
            verify(!findChild(browser, "propertiesTabs").visible)
            compare(panel().effectiveTab, 0)
            verify(controller.histogramRequests >= requests)
        }
        function test_size_and_keyboard() {
            browser.showProperties()
            for (let w of [980, 1440]) {
                size(w, w === 980 ? 640 : 900)
                for (let panelWidth of [320, 360, 480]) {
                    browser.inspectorWidth = panelWidth
                    wait(80)
                    verify(panel().width >= 320 && panel().width <= 480)
                    verify(findChild(browser, "paneWorkspace").width >= 400)
                    verify(findChild(browser, "settingsButton").x + 28 < findChild(browser, "browserSearchField").x)
                }
            }
            const tab = findChild(browser, "imagePropertiesTab-0")
            tab.forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(panel().currentTab, 1)
            mouseClick(findChild(browser, "closePropertiesButton"))
            compare(controller.path, "")
        }
        function test_language_states_and_text_copy() {
            browser.showProperties()
            wait(50)
            compare(findChild(browser, "imagePropertiesTab-0").text, "概览")
            const value = findChild(browser, "propertyValue-path")
            value.forceActiveFocus()
            value.selectAll()
            verify(!browser.fileShortcutsEnabled)
            keyClick(Qt.Key_C, Qt.ControlModifier)
            compare(testCapture.clipboardText(), value.text)
            testSettings.language = "en"
            tryCompare(findChild(browser, "imagePropertiesTab-0"), "text", "Overview")
            compare(controller.overviewGroups[1].fields[1].label, "Full path")
            controller.loading = true
            wait(30)
            controller.loading = false
            controller.metadataLoading = true
            wait(30)
            controller.metadataLoading = false
            controller.errorText = "Decode fixture failed"
            wait(30)
            verify(findChild(browser, "retryPropertiesButton").visible)
            const count = controller.loadRequests
            mouseClick(findChild(browser, "retryPropertiesButton"))
            verify(controller.loadRequests > count)
            controller.errorText = ""
            panel().selectTab(1)
            wait(30)
            verify(findChild(browser, "histogramSource-1") === null)
            compare(controller.lastHistogramSource, 1)
        }
        function test_native_channel_dropdown_and_statistics() {
            browser.showProperties()
            panel().selectTab(1)
            wait(50)
            const histogram = findChild(browser, "propertiesHistogram")
            const combo = findChild(browser, "histogramChannelSelector")
            compare(controller.lastHistogramSource, 1)
            compare(combo.count, 4)
            compare(combo.model.join(","), "Luma,Red,Green,Blue")
            compare(histogram.selectedChannelData.id, "luma")
            compare(findChild(browser, "propertyValue-histogram-mean").text, "112.42")
            combo.forceActiveFocus()
            keyClick(Qt.Key_Down)
            tryCompare(histogram, "selectedChannel", 1)
            compare(histogram.selectedChannelData.id, "red")
            compare(findChild(browser, "propertyValue-histogram-mean").text, "118.37")
            combo.forceActiveFocus()
            keyClick(Qt.Key_Down)
            tryCompare(histogram, "selectedChannel", 2)
            compare(histogram.selectedChannelData.id, "green")
            compare(findChild(browser, "propertyValue-histogram-mean").text, "110.14")
            controller.scenario = "raw"
            wait(30)
            compare(combo.count, 4)
            compare(combo.model.join(","), "R,Gr,Gb,B")
            controller.scenario = "yuv"
            wait(30)
            compare(combo.count, 3)
            compare(combo.model.join(","), "Y,U,V")
            controller.scenario = "gray"
            wait(30)
            compare(combo.count, 4)
            compare(combo.model.join(","), "Luma,Red,Green,Blue")
            testSettings.language = "en"
            wait(30)
            compare(combo.model.join(","), "Luma,Red,Green,Blue")
            testSettings.language = "zh_CN"
            wait(30)
            compare(combo.model.join(","), "Luma,Red,Green,Blue")
            verify(findChild(browser, "histogramAnalysisSummary") === null)
            verify(!hasText(histogram, "横轴：采样值 · 纵轴：相对频数（峰值为 100%）"))
            const item = findChild(browser, "propertyField-histogram-mean")
            for (let child of item.children) {
                if (child.visible && child.height > 1)
                    verify(Math.abs(child.y + child.height / 2 - item.height / 2) < 1)
            }
            panel().selectTab(2)
            controller.warnings = ["Metadata fixture warning"]
            wait(30)
            verify(!hasText(panel(), "重新读取"))
            verify(!hasText(panel(), "Read again"))
        }
        function test_content_font_consistency() {
            browser.showProperties()
            wait(30)
            const format = findChild(browser, "propertyValue-format")
            const dimensions = findChild(browser, "propertyValue-dimensions")
            compare(format.font.family, dimensions.font.family)
            compare(format.font.pixelSize, dimensions.font.pixelSize)
            compare(format.font.family, Theme.uiFont)
            const family = format.font.family, pixelSize = format.font.pixelSize
            panel().selectTab(1)
            wait(30)
            const mean = findChild(browser, "propertyValue-histogram-mean")
            compare(mean.font.family, family)
            compare(mean.font.pixelSize, pixelSize)
            controller.scenario = "raw"
            panel().selectTab(2)
            wait(30)
            const matrix = findChild(browser, "propertyField-color-matrix")
            function checkFonts(item) {
                if (item.font && item.text !== undefined) {
                    compare(item.font.family, Theme.uiFont)
                    compare(item.font.pixelSize, 12)
                }
                for (let child of item.children || []) checkFonts(child)
            }
            checkFonts(matrix)
        }
        function test_fullscreen_drag_and_close() {
            dialog.openForPath("/Images/Demo/fullscreen.jpg")
            tryCompare(dialog, "opened", true)
            const header = findChild(dialog, "imagePropertiesDragHeader")
            const originalX = dialog.x, originalY = dialog.y
            const start = header.mapToItem(scene, 36, 22)
            mousePress(header, 36, 22)
            mouseMove(scene, start.x + 80, start.y + 45)
            mouseRelease(scene, start.x + 80, start.y + 45)
            verify(Math.abs(dialog.x - originalX - 80) < 2, "x " + originalX + " -> " + dialog.x)
            verify(Math.abs(dialog.y - originalY - 45) < 2, "y " + originalY + " -> " + dialog.y)
            const secondStart = header.mapToItem(scene, 36, 22)
            mousePress(header, 36, 22)
            mouseMove(scene, secondStart.x - 2000, secondStart.y - 2000)
            mouseRelease(scene, secondStart.x - 2000, secondStart.y - 2000)
            compare(dialog.x, 0)
            compare(dialog.y, 0)
            size(980, 640)
            verify(dialog.x >= 0 && dialog.x + dialog.width <= scene.width)
            verify(dialog.y >= 0 && dialog.y + dialog.height <= scene.height)
            mouseClick(findChild(dialog, "closePropertiesButton"))
            tryCompare(dialog, "visible", false)
            dialog.openForPath("/Images/Demo/fullscreen.jpg")
            tryCompare(dialog, "opened", true)
            compare(dialog.x, Math.round((scene.width - dialog.width) / 2))
            compare(dialog.y, Math.round((scene.height - dialog.height) / 2))
        }
        function test_raw_mutual_exclusion_and_selection() {
            workspace.activePane.selectedPaths = ["/Images/Demo/first.raw"]
            browser.showProperties()
            verify(panel().visible)
            workspace.activePane.editSelectedRawParameters()
            compare(browser.inspectorMode, "raw")
            verify(rawPanel().visible && !panel().visible)
            compare(controller.path, "")
            compare(raw.path, "/Images/Demo/first.raw")
            verify(browser.contentInteractionEnabled)
            const editor = findChild(rawPanel(), "rawField-width")
            editor.forceActiveFocus()
            editor.selectAll()
            keyClicks("3200")
            verify(!browser.fileShortcutsEnabled)
            workspace.activePane.selectedPaths = ["/Images/Demo/second.raw"]
            tryCompare(raw, "path", "/Images/Demo/second.raw")
            verify(raw.edits.some(function(edit) { return edit.path === "/Images/Demo/first.raw" && edit.key === "width" && edit.value === 3200 }))
            verify(!raw.edits.some(function(edit) { return edit.path === "/Images/Demo/second.raw" && edit.key === "width" }))
            const oldWidth = rawPanel().width
            browser.showProperties()
            verify(panel().visible && !rawPanel().visible)
            compare(raw.path, "")
            compare(panel().width, oldWidth)
            workspace.activePane.editSelectedRawParameters()
            verify(rawPanel().visible && !panel().visible)
            workspace.activePane.selectedPaths = ["/Images/Demo/plain.jpg"]
            tryCompare(raw, "path", "")
            verify(rawPanel().visible)
            verify(findChild(rawPanel(), "rawParametersTabs") === null)
            workspace.activePane.selectedPaths = ["a.raw", "b.yuv"]
            compare(rawPanel().selectionCount, 2)
            compare(raw.path, "")
            mouseClick(findChild(rawPanel(), "closeRawParametersButton"))
            verify(!browser.inspectorVisible)
        }
        function test_raw_presets_and_editors() {
            workspace.activePane.selectedPaths = ["/Images/Demo/fixture.raw"]
            browser.showRawParameters()
            const gamma = rawControl("rawField-displayGamma")
            gamma.forceActiveFocus(); gamma.selectAll(); keyClicks("2.4"); keyClick(Qt.Key_Return)
            compare(raw.values.displayGamma, 2.4)
            const matrix = rawControl("rawColorMatrix-1")
            matrix.forceActiveFocus(); matrix.selectAll(); keyClicks("-0.25"); keyClick(Qt.Key_Return)
            compare(raw.values.colorMatrix[1], -0.25)
            const preset = rawControl("rawPresetSelector")
            preset.forceActiveFocus(); keyClick(Qt.Key_Down)
            compare(raw.selectedPreset, "6236_4178_MIPI_RAW10")
            mouseClick(rawControl("rawDeletePresetButton"))
            compare(raw.selectedPreset, "")
            mouseClick(rawControl("rawSavePresetButton"))
            const nameDialog = findChild(rawPanel(), "rawPresetNameDialog")
            tryCompare(nameDialog, "opened", true)
            verify(!browser.contentInteractionEnabled)
            nameDialog.submitted("")
            verify(nameDialog.visible)
            nameDialog.submitted("Fixture preset")
            tryCompare(nameDialog, "visible", false)
            compare(raw.selectedPreset, "Fixture preset")
            mouseClick(rawControl("rawSaveFolderPresetButton"))
            tryCompare(nameDialog, "opened", true)
            nameDialog.submitted("Folder fixture")
            tryCompare(nameDialog, "visible", false)
            compare(raw.appliedFolderCount, 1)
            mouseClick(rawControl("rawApplyFolderButton"))
            compare(raw.appliedFolderCount, 2)
            verify(findChild(rawPanel(), "rawParameterNotice").visible)
        }
        function test_raw_size_and_keyboard() {
            size(980, 640)
            workspace.activePane.selectedPaths = ["/Images/Demo/fixture.raw"]
            browser.showRawParameters()
            for (let width of [320, 360, 480]) {
                browser.inspectorWidth = width
                wait(30)
                verify(rawPanel().width >= 320 && rawPanel().width <= 480)
                verify(findChild(browser, "paneWorkspace").width >= 400)
                const editor = findChild(rawPanel(), "rawField-width")
                compare(editor.font.family, Theme.uiFont)
                editor.forceActiveFocus()
                verify(!browser.fileShortcutsEnabled)
            }
            verify(findChild(rawPanel(), "rawParametersTabs") === null)
            rawPanel().forceActiveFocus()
            keyClick(Qt.Key_Escape)
            verify(!browser.inspectorVisible)
        }
        function test_raw_visual_states_and_layout() {
            workspace.activePane.selectedPaths = ["/Images/Demo/fixture.raw"]
            browser.showRawParameters()
            for (let dark of [false, true]) {
                Theme.darkMode = dark
                for (let w of [980, 1440]) {
                    size(w, w === 980 ? 640 : 900)
                    browser.inspectorWidth = w === 980 ? 320 : 480
                    for (let format of [6, 3]) {
                        raw.setValue("format", format)
                        wait(60)
                        const scroll = findChild(rawPanel(), "rawParameterScroll")
                        verify(scroll.contentHeight > scroll.height)
                        for (let position = 0; position < 3; ++position) {
                            scroll.contentY = Math.max(0, scroll.contentHeight - scroll.height) * position / 2
                            wait(60)
                            verify(rawPanel().width >= 320 && rawPanel().width <= 480)
                            verify(findChild(browser, "paneWorkspace").width >= 400)
                            function checkRows(item) {
                                for (let child of item.children || []) {
                                    if (child.visible) {
                                        if (child.height === 32 && (child.objectName.indexOf("rawField-") === 0 || child.objectName.indexOf("rawWhiteBalance-") === 0 || child.objectName.indexOf("rawColorMatrix-") === 0 || child.objectName === "rawPresetSelector")) {
                                            verify(child.width > 30)
                                            compare(child.font.family, Theme.uiFont)
                                        }
                                        checkRows(child)
                                    }
                                }
                            }
                            checkRows(rawPanel())
                            verify(testCapture.save(rawPanel(), testArtifactDir + "/raw-" + (dark ? "dark" : "light") + "-" + w + "-" + format + "-" + position + ".png"))
                        }
                    }
                }
            }
        }
        function test_visual_states() {
            browser.showProperties()
            controller.scenario = "raw"
            workspace.activePane.selectedPaths = ["/Images/长目录名/工程图像与色彩测试/" + "very-long-directory/".repeat(6) + "sample.raw"]
            for (let dark of [false, true]) {
                Theme.darkMode = dark
                for (let w of [980, 1440]) {
                    size(w, w === 980 ? 640 : 900)
                    browser.inspectorWidth = w === 980 ? 320 : 480
                    for (let tab = 0; tab < 3; ++tab) {
                        panel().selectTab(tab)
                        wait(120)
                        const fields = panel().groups
                        for (let group of fields) {
                            for (let field of group.fields) {
                                const item = findChild(browser, "propertyField-" + field.id)
                                verify(item !== null)
                                for (let child of item.children) {
                                    if (child.visible && child.width > 0)
                                        verify(child.x >= 0 && child.x + child.width <= item.width + 1)
                                    if (child.visible && child.height > 1)
                                        verify(Math.abs(child.y + child.height / 2 - item.height / 2) < 1)
                                }
                            }
                        }
                        verify(testCapture.save(browser, testArtifactDir + "/" + (dark ? "dark" : "light") + "-" + w + "-" + tab + ".png"))
                    }
                }
            }
            Theme.darkMode = false
            testSettings.language = "en"
            dialog.openForPath("/Images/Demo/full.jpg")
            tryCompare(dialog, "opened", true)
            dialog.currentTab = 1
            wait(120)
            verify(testCapture.save(dialog.contentItem.parent, testArtifactDir + "/fullscreen-en.png"))
            dialog.close()
            controller.warnings = ["Display color conversion warning. 原始配置文件不可用，当前采用默认色彩空间。"]
            panel().selectTab(2)
            wait(60)
            verify(testCapture.save(panel(), testArtifactDir + "/warnings.png"))
            workspace.activePane.selectedPaths = []
            wait(60)
            verify(testCapture.save(panel(), testArtifactDir + "/empty.png"))
        }
    }
}
