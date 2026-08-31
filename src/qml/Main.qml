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
                || visibility !== Window.FullScreen)
            return

        // Only create the image canvas after the native window has reached its final size.
        // Otherwise the first image is visibly fitted twice (windowed, then full-screen).
        fullScreenPage.open(pendingFullScreenPaths, pendingFullScreenIndex)
        pendingFullScreenPaths = []
        showingFullScreen = true
        enteringFullScreen = false
        fullScreenTransitioning = false
    }

    function openFullScreen(paths, initialIndex) {
        if (showingFullScreen || fullScreenTransitioning)
            return
        showingCompare = false
        // Full-screen the main window itself. This guarantees the viewer uses the exact same
        // physical display and avoids migrating a second GPU-backed QQuickWindow across screens.
        const targetScreen = screenAtWindowCenter()
        if (targetScreen)
            screen = targetScreen
        visibilityBeforeFullScreen = visibility
        pendingFullScreenPaths = paths
        pendingFullScreenIndex = initialIndex
        enteringFullScreen = true
        fullScreenTransitioning = true
        // Give the platform one event-loop turn to apply the selected screen before changing
        // window state. The transition cover prevents intermediate geometry from flashing.
        Qt.callLater(function() {
            if (!window.enteringFullScreen)
                return
            window.showFullScreen()
            Qt.callLater(function() { window.completeFullScreenOpen() })
        })
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
        if (visibilityBeforeFullScreen === Window.Maximized)
            showMaximized()
        else if (visibilityBeforeFullScreen === Window.FullScreen)
            showFullScreen()
        else
            showNormal()
        Qt.callLater(function() {
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
        pendingFullScreenPaths = []
        fullScreenTransitioning = false
        enteringFullScreen = false
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
                && visibility === Window.FullScreen)
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
        onFullScreenRequested: function(paths, initialIndex) {
            window.openFullScreen(paths, initialIndex)
        }
        onSettingsRequested: settingsCard.open()
    }
    ComparePage {
        id: comparePage
        objectName: "comparePage"
        controller: compareController
        anchors.fill: parent
        visible: window.showingCompare && !window.showingFullScreen
        onCloseRequested: window.closeCompare()
    }

    FullScreenPage {
        id: fullScreenPage
        controller: fullScreenController
        propertiesController: imagePropertiesController
        settingsController: appSettings
        parent: window.contentItem
        anchors.fill: parent
        visible: window.showingFullScreen
        onCloseRequested: window.closeFullScreen()
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

    SettingsCard {
        id: settingsCard
        parent: Overlay.overlay
        settingsController: appSettings
    }

    Shortcut {
        sequence: {
            const revision = appSettings.shortcutsRevision
            return appSettings.shortcutFor("settings")
        }
        onActivated: settingsCard.open()
    }
}
