import QtQuick
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
        designMode: true
        iconPrefix: Qt.resolvedUrl("../../assets/icons/ui/").toString()
    }

    function init() {
        browsePage.displayMode = 2;
        const navigator = findChild(browsePage, "folderNavigator")
        if (navigator !== null)
            navigator.platformName = Qt.platform.os
        mockController.clearSelection();
        mockController.selectPath(mockController.thumbnails.get(0).path, false, false);
        mockController.selectPath(mockController.thumbnails.get(1).path, false, true);
        wait(80);
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
        path: "/Images/Folder"
        fileName: "Folder"
        technicalLabel: "Folder"
        thumbnailUrl: Qt.resolvedUrl("../../assets/icons/ui/" +
                                    (Qt.platform.os === "osx" ? "macos-folder.svg"
                                                              : "windows-folder.svg"))
        directory: true
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

    function test_folderEntitiesUseTheCurrentPlatformIcon() {
        const paneFolderIcon = findChild(browsePage, "paneFolderIcon-0")
        verify(paneFolderIcon !== null)
        const expectedName = Qt.platform.os === "osx"
                ? "macos-folder.svg"
                : Qt.platform.os === "windows"
                  ? "windows-folder.svg" : "folder.svg"
        verify(paneFolderIcon.source.toString().endsWith(expectedName))
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
