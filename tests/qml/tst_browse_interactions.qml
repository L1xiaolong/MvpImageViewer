import QtQuick
import QtQuick.Controls
import QtTest
import "../../design"
import "../../src/qml/Isp"
import "../../src/qml/Pages"

TestCase {
    id: testCase
    name: "BrowseInteractions"
    when: windowShown
    width: 1440
    height: 900
    visible: true

    MockBrowseWorkspace {
        id: mockWorkspace
        paneCount: 1
    }

    readonly property var mockController: mockWorkspace.pane0

    QtObject {
        id: mockSettings
        property bool smoothDisplay: true
        property bool confirmTrash: true
        property int shortcutsRevision: 0
        function shortcutFor(action) { return "" }
    }

    AppMenuItem {
        id: conditionalMenuItem
        visible: false
        text: "Conditional action"
    }

    BrowsePage {
        id: browsePage
        anchors.fill: parent
        controller: testCase.mockController
        workspaceController: mockWorkspace
        settingsController: mockSettings
        designMode: true
        iconPrefix: Qt.resolvedUrl("../../assets/icons/ui/").toString()
        externalModalVisible: settingsCard.visible
    }

    SettingsCard {
        id: settingsCard
        parent: Overlay.overlay
    }

    function test_modalCardsBlockThumbnailInput_data() {
        const cards = ["imagePropertiesDialog", "rawParametersDialog", "imageResizeDialog",
                       "newFolderDialog", "renameDialog", "trashConfirmationDialog",
                       "transferConfirmationDialog", "settingsCard"]
        const rows = []
        for (let mode = 0; mode < 3; ++mode) {
            for (const card of cards)
                rows.push({ tag: card + "-mode-" + mode, card: card, mode: mode })
        }
        return rows
    }

    function test_modalCardsBlockThumbnailInput(data) {
        browsePage.displayMode = data.mode
        mockController.setDisplayMode(data.mode)
        wait(80)
        const tile = data.mode === 2
                ? findChild(browsePage, "galleryDelegate-0")
                : findChild(findChild(browsePage, "paneContactSheet-0").itemAtIndex(0), "thumbnailMouseArea").parent
        verify(tile !== null)
        const handler = tile.dragHandler
        verify(handler.enabled)
        const card = data.card === "settingsCard" ? settingsCard : findChild(browsePage, data.card)
        verify(card !== null)
        const selectionBefore = mockController.selectedPaths.join("|")
        card.open()
        // Input must be blocked already during the opening transition.
        compare(browsePage.contentInteractionEnabled, false)
        compare(browsePage.fileShortcutsEnabled, false)
        compare(handler.enabled, false)
        tryCompare(card, "opened", true)
        const oldX = card.x
        const oldY = card.y
        mouseDrag(card.contentItem, card.contentItem.width / 2, 20, 60, 45, Qt.LeftButton)
        compare(handler.active, false)
        compare(mockController.selectedPaths.join("|"), selectionBefore)
        card.close()
        tryCompare(card, "visible", false)
        card.x = oldX
        card.y = oldY
        tryCompare(handler, "enabled", true)
        verify(browsePage.contentInteractionEnabled)
        // Exercise the real gesture without entering the OS drag-and-drop loop.
        const dragType = tile.Drag.dragType
        tile.Drag.dragType = Drag.None
        try {
            mousePress(tile, tile.width / 2, tile.height / 2, Qt.LeftButton)
            mouseMove(tile, tile.width / 2 + 25, tile.height / 2 + 25, 20)
            mouseMove(tile, tile.width / 2 + 50, tile.height / 2 + 50, 20)
            tryCompare(handler, "active", true)
        } finally {
            mouseRelease(tile, tile.width / 2 + 50, tile.height / 2 + 50, Qt.LeftButton)
            tile.Drag.dragType = dragType
        }
    }

    function cleanup() {
        settingsCard.close()
        for (const name of ["imagePropertiesDialog", "rawParametersDialog", "imageResizeDialog",
                            "newFolderDialog", "renameDialog", "trashConfirmationDialog",
                            "transferConfirmationDialog"]) {
            const card = findChild(browsePage, name)
            if (card)
                card.close()
        }
        wait(150)
    }

    function init() {
        browsePage.displayMode = 2;
        mockSettings.smoothDisplay = true
        const navigator = findChild(browsePage, "folderNavigator")
        if (navigator !== null)
            navigator.platformName = Qt.platform.os
        mockController.clearSelection();
        mockController.galleryFullResolution = false
        mockController.selectPath(mockController.thumbnails.get(0).path, false, false);
        mockController.selectPath(mockController.thumbnails.get(1).path, false, true);
        wait(80);
    }

    function test_topToolbarMatchesComparisonToolbarMetrics() {
        const toolbar = findChild(browsePage, "topToolbar")
        const openFolderButton = findChild(toolbar, "openFolderButton")
        const compareButton = findChild(toolbar, "compareButton")
        const sortButton = findChild(toolbar, "sortButton")
        const rotateCounterClockwiseButton = findChild(toolbar, "rotateCounterClockwiseButton")
        const searchField = findChild(toolbar, "browserSearchField")
        const smoothDisplayButton = findChild(toolbar, "smoothDisplayButton")
        const settingsButton = findChild(toolbar, "settingsButton")
        verify(toolbar !== null)
        verify(openFolderButton !== null)
        verify(compareButton !== null)
        verify(sortButton !== null)
        verify(rotateCounterClockwiseButton !== null)
        verify(searchField !== null)
        verify(smoothDisplayButton !== null)
        verify(settingsButton !== null)
        compare(toolbar.height, 38)
        compare(openFolderButton.width, 28)
        compare(openFolderButton.height, 28)
        compare(openFolderButton.renderedIconSize, 16)
        compare(searchField.height, 28)
        compare(smoothDisplayButton.width, 28)
        compare(smoothDisplayButton.renderedIconSize, 16)
        verify(smoothDisplayButton.x > sortButton.x)
        verify(smoothDisplayButton.x < rotateCounterClockwiseButton.x)
        verify(compareButton.x + compareButton.width < searchField.x)
        compare(settingsButton.width, 28)
        compare(settingsButton.renderedIconSize, 16)
    }

    function test_galleryThumbnailReceivesLeftAndRightClicks() {
        const thumbnailMouse = findChild(browsePage, "galleryMouse-2");
        verify(thumbnailMouse !== null);

        mouseClick(thumbnailMouse, thumbnailMouse.width / 2, thumbnailMouse.height / 2,
                   Qt.LeftButton);
        compare(mockController.selectedPaths.length, 1);
        compare(mockController.selectedPaths[0], mockController.thumbnails.get(2).path);

        const imageMenu = findChild(browsePage, "galleryFileContextMenu");
        verify(imageMenu !== null);
        mouseClick(thumbnailMouse, thumbnailMouse.width / 2, thumbnailMouse.height / 2,
                   Qt.RightButton);
        tryCompare(imageMenu, "opened", true);
        compare(mockController.selectedPaths[0], mockController.thumbnails.get(2).path);
        imageMenu.close();
    }

    function test_topToolbarSmoothDisplaySettingUpdatesGalleryAndThumbnails() {
        const galleryImage = findChild(browsePage, "galleryImage")
        const toolbarButton = findChild(browsePage, "smoothDisplayButton")
        verify(galleryImage !== null)
        verify(toolbarButton !== null)
        verify(mockSettings.smoothDisplay)
        verify(toolbarButton.checked)
        verify(galleryImage.smooth)
        verify(galleryImage.mipmap)

        mouseClick(toolbarButton, toolbarButton.width / 2, toolbarButton.height / 2,
                   Qt.LeftButton)
        compare(mockSettings.smoothDisplay, false)
        compare(toolbarButton.checked, false)
        compare(galleryImage.smooth, false)
        compare(galleryImage.mipmap, false)

        metadataTile.displayMode = 0
        const gridPreview = findChild(metadataTile, "gridImagePreview")
        verify(gridPreview !== null)
        compare(gridPreview.smooth, false)
        compare(gridPreview.mipmap, false)

        metadataTile.displayMode = 1
        wait(0)
        const listPreview = findChild(metadataTile, "listImagePreview")
        verify(listPreview !== null)
        compare(listPreview.smooth, false)
        compare(listPreview.mipmap, false)

        mockSettings.smoothDisplay = true
        tryCompare(toolbarButton, "checked", true)
    }

    function test_nonSmoothOneToOneGalleryUsesFullResolutionTexture() {
        const workspace = findChild(browsePage, "galleryWorkspace")
        const galleryImage = findChild(browsePage, "galleryImage")
        verify(workspace !== null)
        verify(galleryImage !== null)

        mockSettings.smoothDisplay = false
        workspace.currentPreviewUrl = "image://thumbnail/test?v=1"
        workspace.actualPixels = true
        workspace.manualZoom = 2.0
        mockController.galleryFullResolution = true
        mockController.galleryImageChanged()
        tryCompare(workspace, "useFullResolutionTexture", true)
        tryVerify(function() {
            return galleryImage.source.toString().indexOf("purpose=gallery-full") >= 0
        })

        mockController.galleryFullResolution = false
        mockSettings.smoothDisplay = true
        workspace.actualPixels = false
        workspace.manualZoom = 1.0
        workspace.currentPreviewUrl = ""
    }

    function test_hiddenMenuItemDoesNotReserveSpace() {
        conditionalMenuItem.visible = false
        compare(conditionalMenuItem.implicitHeight, 0)
        conditionalMenuItem.visible = true
        compare(conditionalMenuItem.implicitHeight, 32)
        conditionalMenuItem.visible = false
    }

    function test_gallerySplitterChangesThumbnailWidth() {
        const handle = findChild(browsePage, "galleryResizeHandle");
        const panel = findChild(browsePage, "galleryStripPanel");
        verify(handle !== null);
        verify(panel !== null);
        const oldWidth = panel.width;
        mouseDrag(handle, handle.width / 2, handle.height / 2, -64, 0, Qt.LeftButton);
        verify(panel.width > oldWidth);
    }

    function test_previewInformationAndDragMimeMatchProductionContract() {
        const infoText = findChild(browsePage, "galleryInfoText");
        const card = findChild(browsePage, "galleryDelegate-0");
        verify(infoText !== null);
        verify(card !== null);
        compare(infoText.text, mockController.galleryInfoText);
        compare(card.Drag.mimeData["text/uri-list"], mockController.selectedUriList);
    }

    function test_sortActionsRouteToController() {
        const sortType = findChild(browsePage, "sortByTypeAction");
        const sortSize = findChild(browsePage, "sortBySizeAction");
        const sortModified = findChild(browsePage, "sortByModifiedAction");
        verify(sortType !== null);
        verify(sortSize !== null);
        verify(sortModified !== null);
        sortType.triggered();
        compare(mockController.sortMode, 3);
        sortSize.triggered();
        compare(mockController.sortMode, 2);
        sortModified.triggered();
        compare(mockController.sortMode, 1);
    }

    function test_renameAndTrashConfirmationsAreQmlDialogs() {
        mockController.clearSelection();
        mockController.selectPath(mockController.thumbnails.get(0).path, false, false);

        const renameDialog = findChild(browsePage, "renameDialog");
        verify(renameDialog !== null);
        mockController.renameSelected();
        tryCompare(renameDialog, "opened", true);
        compare(renameDialog.inputText, "sample_0001.jpg");
        renameDialog.close();

        const trashDialog = findChild(browsePage, "trashConfirmationDialog");
        verify(trashDialog !== null);
        mockController.moveSelectedToTrash();
        tryCompare(trashDialog, "opened", true);
        verify(trashDialog.message.indexOf("selected item") >= 0);
        trashDialog.close();
    }

    function test_copyAndMoveConfirmationsAreQmlDialogs() {
        const transferDialog = findChild(browsePage, "transferConfirmationDialog");
        verify(transferDialog !== null);

        mockWorkspace.transferConfirmationRequested(false, 2, "/Images/Target");
        tryCompare(transferDialog, "opened", true);
        compare(transferDialog.dialogTitle, "Copy items?");
        compare(transferDialog.confirmText, "Copy");
        verify(transferDialog.message.indexOf("2 items") >= 0);
        verify(transferDialog.message.indexOf("/Images/Target") >= 0);
        transferDialog.confirmAction();
        tryCompare(transferDialog, "opened", false);
        compare(mockController.statusText, "Transfer confirmed");

        mockWorkspace.transferConfirmationRequested(true, 1, "/Images/Archive");
        tryCompare(transferDialog, "opened", true);
        compare(transferDialog.dialogTitle, "Move items?");
        compare(transferDialog.confirmText, "Move");
        verify(transferDialog.message.indexOf("1 item") >= 0);
        transferDialog.close();
        tryCompare(mockController, "statusText", "Transfer cancelled");
    }

    ThumbnailTile {
        id: directoryTile
        visible: false
        x: 1060
        y: 620
        width: 196
        controller: testCase.mockController
        workspaceController: mockWorkspace
        settingsController: mockSettings
        path: "/Images/Folder"
        fileName: "Folder"
        technicalLabel: "Folder"
        thumbnailUrl: Qt.resolvedUrl("../../assets/icons/ui/" +
                                    (Qt.platform.os === "osx" ? "macos-folder.svg"
                                                              : "windows-folder.svg"))
        directory: true
        displayMode: 0
    }

    ThumbnailTile {
        id: metadataTile
        visible: false
        x: 1060
        y: 620
        width: 220
        controller: testCase.mockController
        workspaceController: mockWorkspace
        settingsController: mockSettings
        path: "/Images/sample.png"
        fileName: "sample.png"
        technicalLabel: "PNG | 1920×1080 | 10 bit | 12.4 MB"
        fileType: "PNG"
        dimensions: Qt.size(1920, 1080)
        bitDepth: 10
        fileSizeText: "12.4 MB"
        directory: false
        displayMode: 0
    }

    function test_folderNavigatorUsesNativePlatformOrganization() {
        const navigator = findChild(browsePage, "folderNavigator")
        const recentHeading = findChild(navigator, "nativeRecentHeading")
        const quickAccessHeading = findChild(navigator, "nativeQuickAccessHeading")
        const locationsHeading = findChild(navigator, "nativeLocationsHeading")
        const nativeTree = findChild(navigator, "nativeFolderTree")
        verify(navigator !== null)
        verify(recentHeading !== null)
        verify(quickAccessHeading !== null)
        verify(locationsHeading !== null)
        verify(nativeTree !== null)

        navigator.platformName = "windows"
        compare(recentHeading.text, "Recent")
        compare(quickAccessHeading.text, "Quick Access")
        compare(locationsHeading.text, "Locations")
        compare(navigator.macStyle, false)

        navigator.platformName = "osx"
        compare(recentHeading.text, "Recent")
        compare(quickAccessHeading.text, "Quick Access")
        compare(locationsHeading.text, "Locations")
        compare(navigator.macStyle, true)

        compare(navigator.recentEntries().length,
                mockController.recentFolders.length)
        compare(navigator.quickAccessEntries().length,
                mockController.nativeSidebarPlaces.length)
        for (let index = 0; index < navigator.recentEntries().length; ++index)
            compare(navigator.recentEntries()[index].kind, "recent")
    }

    function test_paneHeaderProvidesEditableLocationNavigation() {
        browsePage.displayMode = 0
        mockController.setDisplayMode(0)
        wait(80)
        const backButton = findChild(browsePage, "paneBackButton-0")
        const forwardButton = findChild(browsePage, "paneForwardButton-0")
        const upButton = findChild(browsePage, "paneUpButton-0")
        const locationField = findChild(browsePage, "paneLocationField-0")
        const dropButton = findChild(browsePage, "paneLocationDropButton-0")
        const popup = findChild(browsePage, "paneLocationPopup-0")
        verify(backButton !== null)
        verify(forwardButton !== null)
        verify(upButton !== null)
        verify(locationField !== null)
        verify(dropButton !== null)
        verify(popup !== null)
        compare(locationField.text, mockController.currentDirectory)
        compare(locationField.verticalAlignment, TextInput.AlignVCenter)
        compare(locationField.topPadding, 0)
        compare(locationField.bottomPadding, 0)
        compare(locationField.font.weight, Font.DemiBold)

        mouseClick(backButton, backButton.width / 2, backButton.height / 2, Qt.LeftButton)
        compare(mockController.statusText, "Back")
        mockController.canGoForward = true
        mouseClick(forwardButton, forwardButton.width / 2, forwardButton.height / 2, Qt.LeftButton)
        compare(mockController.statusText, "Forward")
        mouseClick(upButton, upButton.width / 2, upButton.height / 2, Qt.LeftButton)
        compare(mockController.statusText, "Parent folder")

        mouseClick(locationField, 8, locationField.height / 2, Qt.LeftButton)
        locationField.text = "/Images/Typed path"
        keyClick(Qt.Key_Return)
        compare(mockController.currentDirectory, "/Images/Typed path")
        compare(mockController.recentLocations[0], "/Images/Typed path")

        mouseClick(dropButton, dropButton.width / 2, dropButton.height / 2, Qt.LeftButton)
        tryCompare(popup, "opened", true)
        popup.close()
        tryCompare(popup, "opened", false)
        mockController.canGoForward = false
    }

    function test_directoryThumbnailKeepsSquareHighResolutionTexture() {
        directoryTile.displayMode = 0
        wait(0)
        const gridIcon = findChild(directoryTile, "gridFolderIcon")
        const gridInfo = findChild(directoryTile, "gridTechnicalLabel")
        verify(gridIcon !== null)
        verify(gridInfo !== null)
        compare(gridIcon.width, gridIcon.height)
        verify(gridIcon.sourceSize.width >= 160)
        compare(gridIcon.sourceSize.width, gridIcon.sourceSize.height)
        compare(gridIcon.fillMode, Image.PreserveAspectFit)
        compare(gridInfo.font.pixelSize, 10)
        verify(gridInfo.anchors.topMargin <= 2)

        directoryTile.displayMode = 1
        wait(0)
        const listIcon = findChild(directoryTile, "listFolderIcon")
        const listInfo = findChild(directoryTile, "listTechnicalLabel")
        verify(listIcon !== null)
        verify(listInfo !== null)
        compare(listIcon.width, listIcon.height)
        verify(listIcon.sourceSize.width >= 96)
        compare(listIcon.sourceSize.width, listIcon.sourceSize.height)
        compare(listIcon.fillMode, Image.PreserveAspectFit)
        compare(listInfo.font.pixelSize, 10)
        verify(listInfo.anchors.topMargin <= 2)
    }

    function test_thumbnailHoverShowsTheCompletePath() {
        mouseMove(browsePage, browsePage.width / 2, browsePage.height / 2)
        directoryTile.displayMode = 0
        directoryTile.visible = true
        wait(0)
        compare(directoryTile.pathToolTipText, directoryTile.path)
        compare(directoryTile.pathToolTipDelay, 500)
        mouseMove(directoryTile, directoryTile.width / 2, directoryTile.height / 2)
        tryCompare(directoryTile, "pathHoverActive", true, 1000)
        mouseMove(browsePage, browsePage.width / 2, browsePage.height / 2)
        directoryTile.visible = false
    }

    function test_thumbnailMetadataUsesColoredTypeBadges() {
        metadataTile.displayMode = 0
        metadataTile.fileType = "PNG"
        wait(0)
        const badge = findChild(metadataTile, "gridTypeBadge")
        const details = findChild(metadataTile, "gridTechnicalLabel")
        verify(badge !== null)
        verify(details !== null)
        compare(badge.radius, 4)
        compare(badge.border.width, 1)
        compare(badge.color, Theme.encodedBadgeSurface)
        compare(details.text, "| 1920×1080 | 10 bit | 12.4 MB")

        metadataTile.fileType = "YUV"
        wait(0)
        compare(badge.color, Theme.yuvBadgeSurface)

        metadataTile.fileType = "RAW"
        wait(0)
        compare(badge.color, Theme.rawBadgeSurface)
        verify(Theme.rawBadgeSurface !== Theme.yuvBadgeSurface)
        verify(Theme.yuvBadgeSurface !== Theme.encodedBadgeSurface)
    }

    function test_propertiesAndRawCardsCanBeDraggedByTheirHeaders() {
        const propertiesDialog = findChild(browsePage, "imagePropertiesDialog")
        const propertiesHeader = findChild(propertiesDialog, "imagePropertiesDragHeader")
        const contactSheet = findChild(browsePage, "paneContactSheet-0")
        const galleryStrip = findChild(browsePage, "galleryStrip")
        verify(propertiesDialog !== null)
        verify(propertiesHeader !== null)
        verify(contactSheet !== null)
        verify(galleryStrip !== null)
        compare(contactSheet.interactive, true)
        compare(galleryStrip.interactive, true)
        propertiesDialog.openForPath(mockController.thumbnails.get(0).path)
        tryCompare(propertiesDialog, "opened", true)
        compare(contactSheet.interactive, false)
        compare(galleryStrip.interactive, false)
        const contactSheetY = contactSheet.contentY
        const galleryStripY = galleryStrip.contentY
        const propertiesX = propertiesDialog.x
        const propertiesY = propertiesDialog.y
        mouseDrag(propertiesHeader, propertiesHeader.width / 2, propertiesHeader.height / 2,
                  80, 45, Qt.LeftButton)
        verify(propertiesDialog.x > propertiesX)
        verify(propertiesDialog.y > propertiesY)
        compare(contactSheet.contentY, contactSheetY)
        compare(galleryStrip.contentY, galleryStripY)
        propertiesDialog.close()
        tryCompare(contactSheet, "interactive", true)
        tryCompare(galleryStrip, "interactive", true)

        const rawDialog = findChild(browsePage, "rawParametersDialog")
        const rawHeader = findChild(rawDialog, "rawParametersDragHeader")
        verify(rawDialog !== null)
        verify(rawHeader !== null)
        rawDialog.openForPath("/Images/Demo/sample_0010.raw")
        tryCompare(rawDialog, "opened", true)
        compare(contactSheet.interactive, false)
        compare(galleryStrip.interactive, false)
        const rawX = rawDialog.x
        const rawY = rawDialog.y
        mouseDrag(rawHeader, rawHeader.width / 2, rawHeader.height / 2,
                  -80, 45, Qt.LeftButton)
        verify(rawDialog.x < rawX)
        verify(rawDialog.y > rawY)
        rawDialog.close()
        tryCompare(contactSheet, "interactive", true)
        tryCompare(galleryStrip, "interactive", true)
    }
}
