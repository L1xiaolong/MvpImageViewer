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
    title: "ISP Image Viewer"
    color: Theme.sensorWhite
    property bool showingCompare: false
    property bool showingFullScreen: false
    property bool forceApplicationClose: false
    property bool applicationExitPending: false
    property int visibilityBeforeFullScreen: Window.Windowed
    signal quitApplicationRequested()

    function openFullScreen(paths, initialIndex) {
        showingCompare = false
        showingFullScreen = true
        if (Qt.platform.os === "osx") {
            // On macOS, hiding a separate window while Cocoa is still leaving its native
            // full-screen Space can strand an all-black Space and keep the application alive.
            // Use the main window there so Cocoa owns one uninterrupted full-screen transition.
            visibilityBeforeFullScreen = visibility
            showFullScreen()
        } else {
            // Keep the main window's native state untouched on Windows. A separate full-screen
            // window avoids the Maximize <-> FullScreen transition that makes the QML scene and
            // image canvas resize twice.
            fullScreenWindow.showFullScreen()
        }
        fullScreenPage.open(paths, initialIndex)
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
        showingFullScreen = false
        if (Qt.platform.os === "osx") {
            if (visibilityBeforeFullScreen === Window.Maximized)
                showMaximized()
            else if (visibilityBeforeFullScreen === Window.FullScreen)
                showFullScreen()
            else
                showNormal()
        } else {
            // The main window never changed state, so closing the viewer is just a hide
            // operation. This keeps the maximized main page at one stable size.
            fullScreenWindow.hide()
        }
        browseController.refreshAll()
        Qt.callLater(function() { browsePage.forceActiveFocus() })
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
        applicationExitPending = true
        compareController.closeSession()
        fullScreenController.closeSession()
        quitApplicationRequested()
    }

    Component.onCompleted: {
        if (initialComparePaths.length >= 2) {
            showingCompare = true
            comparePage.open(initialComparePaths)
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
        visible: !window.showingCompare && !window.showingFullScreen
        onFullScreenRequested: function(paths, initialIndex) {
            window.openFullScreen(paths, initialIndex)
        }
    }
    ComparePage {
        id: comparePage
        objectName: "comparePage"
        controller: compareController
        anchors.fill: parent
        visible: window.showingCompare && !window.showingFullScreen
        onCloseRequested: window.closeCompare()
    }

    Window {
        id: fullScreenWindow
        objectName: "qmlFullScreenWindow"
        visible: false
        color: "#A0A0A0"
        flags: Qt.Window | Qt.FramelessWindowHint
        transientParent: window

        onClosing: function(close) {
            close.accepted = false
            window.closeFullScreen()
        }
    }
    FullScreenPage {
        id: fullScreenPage
        controller: fullScreenController
        propertiesController: imagePropertiesController
        parent: Qt.platform.os === "osx" ? window.contentItem : fullScreenWindow.contentItem
        anchors.fill: parent
        visible: window.showingFullScreen
        onCloseRequested: window.closeFullScreen()
    }
    Connections {
        target: fullScreenController
        function onFilesystemChanged() { browseController.refreshAll() }
    }
}
