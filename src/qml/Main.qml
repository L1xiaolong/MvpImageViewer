import QtQuick
import QtQuick.Controls
import "Isp"
import "Pages"

ApplicationWindow {
    id: window
    objectName: "qmlMainWindow"
    // C++ shows the window after restoring its last normal/maximized state.
    visible: false
    width: 1440
    height: 900
    minimumWidth: 980
    minimumHeight: 640
    title: qsTr("MVP Image Viewer")
    color: Theme.sensorWhite
    palette.window: Theme.sensorWhite
    palette.windowText: Theme.graphiteInk
    palette.base: Theme.paperWhite
    palette.alternateBase: Theme.raisedSurface
    palette.text: Theme.graphiteInk
    palette.button: Theme.raisedSurface
    palette.buttonText: Theme.graphiteInk
    palette.highlight: Theme.explorerSelectionBg
    palette.highlightedText: Theme.graphiteInk
    palette.placeholderText: Theme.faintInk
    palette.mid: Theme.opticalGray
    palette.dark: Theme.opticalGray
    palette.toolTipBase: Theme.raisedSurface
    palette.toolTipText: Theme.graphiteInk
    palette.link: Theme.probeBlue
    property bool showingCompare: false
    property bool showingFullScreen: false
    property bool fullScreenTransitioning: false
    property bool enteringFullScreen: false
    property var fullScreenEnterAction: null
    property int coverFramesRemaining: 0
    property bool instantFullScreenActive: false
    property var pendingFullScreenPaths: []
    property int pendingFullScreenIndex: 0
    property bool forceApplicationClose: false
    property bool applicationExitPending: false
    property int visibilityBeforeFullScreen: Window.Windowed
    signal quitApplicationRequested()

    Binding {
        target: Theme
        property: "darkMode"
        value: appSettings.darkTheme
    }
    Binding {
        target: Theme
        property: "canvasBackgroundMode"
        value: appSettings.canvasBackground
    }

    function screenAtWindowCenter() {
        const centerX = x + width / 2
        const centerY = y + height / 2
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; ++i) {
            const candidate = screens[i]
            if (centerX >= candidate.virtualX && centerX < candidate.virtualX + candidate.width
                    && centerY >= candidate.virtualY
                    && centerY < candidate.virtualY + candidate.height)
                return candidate
        }
        return screen
    }

    function completeFullScreenOpen() {
        if (!fullScreenTransitioning || !enteringFullScreen
                || showingFullScreen
                || (instantFullScreenActive
                    ? !fullScreenPresentationController.active
                    : visibility !== Window.FullScreen))
            return

        // Only create the image canvas after the native window has reached its final size.
        // Otherwise the first image is visibly fitted twice (windowed, then full-screen).
        fullScreenPage.open(pendingFullScreenPaths, pendingFullScreenIndex)
        pendingFullScreenPaths = []
        showingFullScreen = true
        fullScreenPage.forceActiveFocus()
        // Keep the cover until the canvas has uploaded and drawn the first image.
        // Finishing the asynchronous decode alone does not make a frame ready to display.
    }

    function openFullScreen(paths, initialIndex) {
        if (!paths || paths.length === 0 || showingFullScreen || fullScreenTransitioning)
            return
        showingCompare = false
        const targetScreen = screenAtWindowCenter()
        visibilityBeforeFullScreen = visibility
        // Native full screen can add a long system transition. Turn the existing Cocoa window or
        // Windows HWND into a borderless full-display window instead, so there is no second white
        // window and no incomplete transient-window coverage. A main window already in its native
        // full-screen state keeps the cheaper in-place page switch.
        instantFullScreenActive = (Qt.platform.os === "osx" || Qt.platform.os === "windows")
                && visibilityBeforeFullScreen !== Window.FullScreen
        pendingFullScreenPaths = paths
        pendingFullScreenIndex = initialIndex
        enteringFullScreen = true
        fullScreenTransitioning = true
        // The first swap may belong to a frame already in flight. Wait for the next one as
        // well so the cover has actually been submitted before resizing the native window.
        coverFramesRemaining = 2
        fullScreenEnterAction = function() {
            if (!window.enteringFullScreen)
                return
            if (window.instantFullScreenActive
                    && !fullScreenPresentationController.begin(window)) {
                window.instantFullScreenActive = false
            }
            if (!window.instantFullScreenActive) {
                // The native path uses the main window itself. Select its current physical
                // display before changing state so Qt never migrates the image canvas later.
                if (targetScreen)
                    window.screen = targetScreen
                window.showFullScreen()
            }
            Qt.callLater(function() { window.completeFullScreenOpen() })
        }
        window.update()
    }

    onFrameSwapped: {
        if (!fullScreenEnterAction)
            return
        coverFramesRemaining -= 1
        if (coverFramesRemaining > 0) {
            window.update()
        } else {
            const enter = fullScreenEnterAction
            fullScreenEnterAction = null
            Qt.callLater(enter)
        }
    }

    function closeCompare() {
        if (!showingCompare)
            return
        compareController.setHoldCandidate(false)
        compareController.closeSession()
        showingCompare = false
        Qt.callLater(function() { browsePage.forceActiveFocus() })
    }

    function closeFullScreen() {
        if (!showingFullScreen)
            return

        fullScreenController.closeSession()
        fullScreenTransitioning = true
        enteringFullScreen = false
        showingFullScreen = false
        if (instantFullScreenActive) {
            fullScreenPresentationController.end()
        } else if (visibilityBeforeFullScreen === Window.Maximized) {
            showMaximized()
        } else if (visibilityBeforeFullScreen === Window.FullScreen) {
            showFullScreen()
        } else {
            showNormal()
        }
        Qt.callLater(function() {
            window.instantFullScreenActive = false
            window.fullScreenTransitioning = false
            browseController.refreshAll()
            browsePage.forceActiveFocus()
        })
    }

    function beginApplicationExit() {
        if (applicationExitPending)
            return
        applicationExitPending = true
        compareController.closeSession()
        fullScreenController.closeSession()
        if (instantFullScreenActive) {
            fullScreenPresentationController.end()
        }
        pendingFullScreenPaths = []
        fullScreenEnterAction = null
        fullScreenTransitioning = false
        enteringFullScreen = false
        instantFullScreenActive = false
        showingCompare = false
        showingFullScreen = false
        quitApplicationRequested()
    }

    onClosing: function(close) {
        if (!forceApplicationClose && !applicationExitPending && showingFullScreen) {
            close.accepted = false
            Qt.callLater(function() { window.closeFullScreen() })
            return
        }
        if (!forceApplicationClose && !applicationExitPending && showingCompare) {
            close.accepted = false
            // Leave the native close callback before replacing the compare page. On Windows,
            // changing the window contents synchronously from this callback can hide the native
            // window even though the close event was rejected.
            Qt.callLater(function() { window.closeCompare() })
            return
        }

        close.accepted = true
        beginApplicationExit()
    }
    onVisibilityChanged: {
        if (fullScreenTransitioning && enteringFullScreen
                && window.visibility === Window.FullScreen)
            Qt.callLater(function() { window.completeFullScreenOpen() })
    }

    Component.onCompleted: {
        appSettings.startAutomaticUpdateCheck()
        if (initialComparePaths.length >= 2) {
            showingCompare = true
            comparePage.open(initialComparePaths)
        }
        if (showSettingsOnStartup) {
            settingsCard.currentSection = settingsStartupSection
            settingsCard.open()
        }
    }
    Connections {
        target: browseController
        function onCompareRequested(paths) {
            window.showingCompare = true
            comparePage.open(paths)
        }
    }

    BrowsePage {
        id: browsePage
        anchors.fill: parent
        controller: browseController.activePane
        workspaceController: browseController
        propertiesController: imagePropertiesController
        rawController: rawParametersController
        settingsController: appSettings
        visible: !window.showingCompare && !window.showingFullScreen
        externalModalVisible: settingsCard.visible
        onFullScreenRequested: function(paths, initialIndex) {
            window.openFullScreen(paths, initialIndex)
        }
        onSettingsRequested: settingsCard.open()
    }
    ComparePage {
        id: comparePage
        objectName: "comparePage"
        controller: compareController
        settingsController: appSettings
        anchors.fill: parent
        visible: window.showingCompare && !window.showingFullScreen
        onCloseRequested: window.closeCompare()
    }

    FullScreenPage {
        id: fullScreenPage
        controller: fullScreenController
        propertiesController: imagePropertiesController
        settingsController: appSettings
        messageParent: window.contentItem
        parent: window.contentItem
        anchors.fill: parent
        visible: window.showingFullScreen
        onCloseRequested: window.closeFullScreen()
        onImageFrameReady: {
            if (window.enteringFullScreen && window.showingFullScreen) {
                window.enteringFullScreen = false
                window.fullScreenTransitioning = false
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: window.fullScreenTransitioning
        color: Theme.canvasBackground
        z: 10000
    }
    Connections {
        target: fullScreenController
        function onFilesystemChanged() { browseController.refreshAll() }
    }

    Rectangle {
        id: diagnosticNotice
        property bool dismissed: false
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 16
        z: 100
        width: Math.min(470, parent.width - 32)
        height: noticeColumn.implicitHeight + 24
        radius: 8
        color: Theme.paperWhite
        border.color: Theme.exposureAmber
        visible: !dismissed && typeof diagnosticsController !== "undefined"
                 && diagnosticsController.previousExit.length > 0
        Column {
            id: noticeColumn
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                color: Theme.graphiteInk
                text: typeof diagnosticsController !== "undefined" && diagnosticsController.previousExit === "crash"
                      ? qsTr("A crash report from the previous session is available.")
                      : qsTr("The previous session ended unexpectedly. Diagnostic information may be available.")
            }
            Row {
                spacing: 8
                Button {
                    text: qsTr("Diagnostics / Export")
                    onClicked: { settingsCard.currentSection = 6; settingsCard.open(); diagnosticNotice.dismissed = true }
                }
                Button { text: qsTr("Dismiss"); onClicked: diagnosticNotice.dismissed = true }
            }
        }
    }

    SettingsCard {
        id: settingsCard
        parent: Overlay.overlay
        settingsController: appSettings
        diagnostics: typeof diagnosticsController !== "undefined" ? diagnosticsController : null
    }

    Shortcut {
        sequence: {
            const revision = appSettings.shortcutsRevision
            return appSettings.shortcutFor("settings")
        }
        onActivated: settingsCard.open()
    }
}
