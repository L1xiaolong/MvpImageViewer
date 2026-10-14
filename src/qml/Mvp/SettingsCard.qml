pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "."

Popup {
    id: root
    objectName: "settingsCard"
    width: Math.min(860, parent ? parent.width - 48 : 860)
    height: Math.min(620, parent ? parent.height - 48 : 620)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: Theme.overlay }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.normal } }
    background: Rectangle {
        color: Theme.sensorWhite
        radius: 8
        border.color: Theme.opticalGray
    }

    property int currentSection: 0
    property var settingsController: null
    property var diagnostics: null
    property var scrollPositions: [0, 0, 0, 0, 0]
    property int loadedSection: -1
    readonly property bool recordingShortcut: shortcutRecorder.visible
    property var shortcutReturnFocus: null
    property string recordingAction: ""
    property string candidateSequence: ""
    property string candidateLabel: ""
    property string shortcutError: ""
    property string shortcutMessage: ""
    readonly property var sections: [
        { label: qsTr("General"), description: qsTr("Language, startup, and file operations.") },
        { label: qsTr("Display"), description: qsTr("Theme, canvas background, and image presentation.") },
        { label: qsTr("Shortcuts"), description: qsTr("Choose a shortcut, then press the keys you want to use.") },
        { label: qsTr("Updates"), description: qsTr("Check, download, and install updates when you choose.") },
        { label: qsTr("Logs & diagnostics"), description: qsTr("Diagnostics stay on this device. Nothing is uploaded automatically.") }
    ]

    function shortcutLabel(action) {
        const labels = {
            "openFolder": qsTr("Open folder"), "find": qsTr("Find files"),
            "settings": qsTr("Open settings"), "toggleNavigator": qsTr("Show or hide sidebar"),
            "compare": qsTr("Compare selected images"), "rename": qsTr("Rename selected item"),
            "newFolder": qsTr("Create new folder")
        }
        return labels[action] || action
    }
    function beginShortcut(action, returnFocus) {
        root.shortcutReturnFocus = returnFocus || null
        root.recordingAction = action
        root.candidateSequence = ""
        root.candidateLabel = ""
        root.shortcutError = ""
        shortcutRecorder.open()
        captureArea.forceActiveFocus()
    }
    function saveShortcut() {
        if (!root.settingsController || root.candidateSequence.length === 0)
            return
        const result = root.settingsController.setShortcut(root.recordingAction, root.candidateSequence)
        if (result === "invalid" || result === "unknown")
            root.shortcutError = qsTr("Press a valid keyboard shortcut.")
        else if (result.length > 0)
            root.shortcutError = qsTr("This shortcut is already assigned to “%1”.").arg(root.shortcutLabel(result))
        else {
            root.shortcutMessage = qsTr("Shortcut saved.")
            shortcutRecorder.close()
        }
    }
    function selectSection(section) {
        if (root.loadedSection >= 0)
            root.scrollPositions[root.loadedSection] = pageFlickable.contentY
        root.currentSection = section
    }
    function updateStatusText() {
        if (!root.settingsController) return qsTr("Update service is unavailable.")
        switch (root.settingsController.updateState) {
        case "checking": return qsTr("Checking GitHub Releases…")
        case "available": return qsTr("Version %1 is available.").arg(root.settingsController.latestVersion)
        case "downloading": return qsTr("Downloading version %1… %2%").arg(root.settingsController.latestVersion).arg(Math.max(0, root.settingsController.updateDownloadProgress))
        case "verifying": return qsTr("Verifying the downloaded installer…")
        case "ready": return qsTr("Version %1 is downloaded and verified.").arg(root.settingsController.latestVersion)
        case "installing": return qsTr("Opening the installer…")
        case "latest": return qsTr("You’re up to date. Version %1 is the latest release.").arg(root.settingsController.latestVersion)
        case "error": return root.settingsController.updateError.length > 0
                    ? qsTr("Update failed: %1").arg(root.settingsController.updateError)
                    : qsTr("Couldn’t check for updates. Check your connection and try again.")
        default: return qsTr("Check for a newer published version of MVP Image Viewer.")
        }
    }
    onAboutToShow: {
        scrollPositions = [0, 0, 0, 0, 0]
        loadedSection = -1
        shortcutMessage = ""
        pageFlickable.contentY = 0
    }
    onClosed: {
        shortcutRecorder.close()
    }

    component BodyText: Text {
        width: parent ? parent.width : 400
        color: Theme.mutedInk
        font.family: Theme.uiFont
        font.pixelSize: 12
        wrapMode: Text.Wrap
        lineHeight: 1.25
    }

    Rectangle {
        id: sidebar
        x: 1; y: 1
        width: 183
        height: parent.height - 2
        radius: 7
        color: Theme.paperWhite
        Rectangle { anchors.right: parent.right; width: 8; height: parent.height; color: parent.color }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.opticalGray }
        Text {
            x: 20; y: 22
            text: qsTr("Settings")
            color: Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }
        Column {
            x: 12; y: 66
            width: parent.width - 24
            spacing: 4
            Repeater {
                model: root.sections
                delegate: Button {
                    id: nav
                    required property int index
                    required property var modelData
                    width: parent.width
                    height: 36
                    topInset: 0; bottomInset: 0
                    text: modelData.label
                    Accessible.name: text
                    hoverEnabled: true
                    onClicked: root.selectSection(index)
                    contentItem: Text {
                        leftPadding: 10
                        text: nav.text
                        color: Theme.graphiteInk
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        font.weight: root.currentSection === nav.index ? Font.DemiBold : Font.Normal
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Theme.radius
                        color: root.currentSection === nav.index ? Theme.explorerSelectionBg : nav.hovered ? Theme.softHover : "transparent"
                        border.width: nav.activeFocus ? 2 : 0
                        border.color: Theme.probeBlue
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                        Rectangle { width: 2; height: 16; y: 10; color: Theme.probeBlue; visible: root.currentSection === nav.index }
                    }
                }
            }
        }
        Column {
            x: 20
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            width: parent.width - 36
            spacing: 5
            Button {
                id: projectLink
                objectName: "settingsProjectLink"
                width: parent.width
                height: 20
                padding: 0
                topInset: 0; bottomInset: 0
                text: "MVP Image Viewer"
                Accessible.name: qsTr("Open GitHub project")
                hoverEnabled: true
                onClicked: if (root.settingsController) root.settingsController.openRepository()
                contentItem: Text {
                    text: projectLink.text
                    color: Theme.probeBlue
                    font.family: Theme.uiFont
                    font.pixelSize: 11
                    font.underline: true
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: projectLink.hovered ? Theme.softHover : "transparent"
                    radius: Theme.radius
                    border.width: projectLink.activeFocus ? 2 : 0
                    border.color: Theme.probeBlue
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Open GitHub project")
            }
            Text {
                text: qsTr("Version %1").arg(root.settingsController ? root.settingsController.applicationVersion : "")
                color: Theme.faintInk
                font.family: Theme.monoFont
                font.pixelSize: 11
            }
        }
    }
    Item {
        id: header
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.max(86, headerLabels.implicitHeight + 40)
        Column {
            id: headerLabels
            x: 24; y: 22
            width: parent.width - 48
            spacing: 6
            Text {
                text: root.sections[root.currentSection].label
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }
            BodyText { text: root.sections[root.currentSection].description }
        }
        Rectangle { x: 24; width: parent.width - 48; anchors.bottom: parent.bottom; height: 1; color: Theme.opticalGray }
    }
    Flickable {
        id: pageFlickable
        objectName: "settingsScroll"
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: footer.top
        contentWidth: width
        contentHeight: Math.max(height, pageLoader.y + (pageLoader.pageItem ? pageLoader.pageItem.implicitHeight : 0) + 24)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Loader {
            id: pageLoader
            objectName: "settingsPageLoader"
            readonly property Item pageItem: item as Item
            active: root.visible
            x: 24; y: 0
            width: parent.width - 48
            sourceComponent: [generalPage, displayPage, shortcutsPage, updatesPage, diagnosticsPage][root.currentSection]
            onLoaded: {
                root.loadedSection = root.currentSection
                pageFlickable.contentY = Math.min(root.scrollPositions[root.currentSection], Math.max(0, pageItem.implicitHeight + 24 - pageFlickable.height))
            }
        }
    }
    Rectangle {
        id: footer
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.rightMargin: 1
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
        height: 56
        color: Theme.paperWhite
        Rectangle { width: parent.width; height: 1; color: Theme.opticalGray }
        SettingsButton {
            objectName: "settingsRestoreButton"
            x: 24
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Restore default settings…")
            onClicked: restoreDialog.showConfirmation()
        }
        SettingsButton {
            objectName: "settingsCloseButton"
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Close")
            primary: true
            onClicked: root.close()
        }
    }

    Component {
        id: generalPage
        Column {
            width: pageLoader.width
            SettingsHeading { text: qsTr("Language") }
            SettingsRow {
                title: qsTr("Interface language")
                description: qsTr("Applies immediately to the interface.")
                SettingsComboBox {
                    objectName: "settingsLanguage"
                    width: parent.width
                    model: [qsTr("Use system language"), "简体中文", "English"]
                    currentIndex: !root.settingsController || root.settingsController.language === "system" ? 0 : root.settingsController.language === "zh_CN" ? 1 : 2
                    onActivated: if (root.settingsController) root.settingsController.language = ["system", "zh_CN", "en"][currentIndex]
                }
            }
            SettingsHeading { text: qsTr("Startup") }
            SettingsRow {
                title: qsTr("Restore the last opened directory")
                description: qsTr("Continue browsing where you left off.")
                controlWidth: 40
                SettingsToggle {
                    objectName: "settingsRestoreDirectory"
                    text: qsTr("Restore the last opened directory")
                    checked: root.settingsController ? root.settingsController.restoreLastDirectory : true
                    onToggled: if (root.settingsController) root.settingsController.restoreLastDirectory = checked
                }
            }
            SettingsHeading { text: qsTr("File operations") }
            SettingsRow {
                title: qsTr("Ask before moving files to the Trash")
                description: qsTr("Show a confirmation before moving selected files.")
                controlWidth: 40
                SettingsToggle {
                    text: qsTr("Ask before moving files to the Trash")
                    checked: root.settingsController ? root.settingsController.confirmTrash : true
                    onToggled: if (root.settingsController) root.settingsController.confirmTrash = checked
                }
            }
        }
    }
    Component {
        id: displayPage
        Column {
            width: pageLoader.width
            SettingsHeading { text: qsTr("Appearance") }
            SettingsRow {
                title: qsTr("Interface theme")
                SettingsComboBox {
                    objectName: "settingsTheme"
                    width: parent.width
                    model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
                    currentIndex: root.settingsController ? Math.max(0, ["system", "light", "dark"].indexOf(root.settingsController.theme)) : 0
                    onActivated: if (root.settingsController) root.settingsController.theme = ["system", "light", "dark"][currentIndex]
                }
            }
            SettingsRow {
                title: qsTr("Canvas background")
                SettingsComboBox {
                    objectName: "settingsCanvasBackground"
                    width: parent.width
                    model: [qsTr("Neutral gray"), qsTr("Dark gray"), qsTr("Black"), qsTr("White")]
                    currentIndex: root.settingsController ? Math.max(0, ["neutral", "dark", "black", "white"].indexOf(root.settingsController.canvasBackground)) : 0
                    onActivated: if (root.settingsController) root.settingsController.canvasBackground = ["neutral", "dark", "black", "white"][currentIndex]
                }
            }
            SettingsHeading { text: qsTr("Display color space") }
            SettingsRow {
                title: qsTr("Display color space")
                description: qsTr("Currently active: %1").arg(root.settingsController ? root.settingsController.resolvedDisplayColorSpace : "sRGB")
                SettingsComboBox {
                    width: parent.width
                    model: [qsTr("Auto (follow display)"), "sRGB", "Display P3", "Adobe RGB (1998)", "BT.2020"]
                    currentIndex: root.settingsController ? Math.max(0, ["auto", "srgb", "display-p3", "adobe-rgb", "bt2020"].indexOf(root.settingsController.displayColorSpace)) : 0
                    onActivated: if (root.settingsController) root.settingsController.displayColorSpace = ["auto", "srgb", "display-p3", "adobe-rgb", "bt2020"][currentIndex]
                }
            }
            SettingsHeading { text: qsTr("Color processing") }
            SettingsRow {
                title: qsTr("Apply embedded ICC color profiles")
                description: root.settingsController && !root.settingsController.colorManagementAvailable ? qsTr("Color profile conversion is unavailable in this build.") : qsTr("Convert embedded RGB profiles into the selected display space.")
                controlWidth: 40
                SettingsToggle {
                    text: qsTr("Apply embedded ICC color profiles")
                    enabled: root.settingsController && root.settingsController.colorManagementAvailable
                    checked: root.settingsController ? root.settingsController.applyEmbeddedColorProfiles : true
                    onToggled: if (root.settingsController) root.settingsController.applyEmbeddedColorProfiles = checked
                }
            }
            SettingsRow {
                title: qsTr("Preserve high bit depth when available")
                description: qsTr("Keep 16-bit and floating-point samples instead of using an 8-bit display buffer.")
                controlWidth: 40
                SettingsToggle {
                    text: qsTr("Preserve high bit depth when available")
                    checked: root.settingsController ? root.settingsController.preserveHighBitDepth : true
                    onToggled: if (root.settingsController) root.settingsController.preserveHighBitDepth = checked
                }
            }
            SettingsHeading { text: qsTr("Image presentation") }
            SettingsRow {
                title: qsTr("Honor EXIF orientation")
                description: qsTr("Automatically rotate images according to their metadata.")
                controlWidth: 40
                SettingsToggle {
                    text: qsTr("Honor EXIF orientation")
                    checked: root.settingsController ? root.settingsController.honorExifOrientation : true
                    onToggled: if (root.settingsController) root.settingsController.honorExifOrientation = checked
                }
            }
            SettingsRow {
                title: qsTr("Smooth display")
                description: qsTr("Use the same interpolation setting as the workspace toolbar.")
                controlWidth: 40
                SettingsToggle {
                    objectName: "settingsSmoothDisplay"
                    text: qsTr("Smooth display")
                    checked: root.settingsController ? root.settingsController.smoothDisplay : true
                    onToggled: if (root.settingsController) root.settingsController.smoothDisplay = checked
                }
            }
            Item { width: 1; height: 16 }
            SettingsButton {
                id: colorDetails
                text: (checked ? "▾ " : "▸ ") + qsTr("About color processing")
                checkable: true
            }
            Item { width: 1; height: 8; visible: colorDetails.checked }
            BodyText {
                visible: colorDetails.checked
                text: qsTr("Auto follows the color space reported for the window surface. ICC conversion, canvas encoding, and color readouts use the selected display space. A fixed space that does not match your display may change how colors appear. These settings do not modify your source images.")
            }
        }
    }
    Component {
        id: shortcutsPage
        Column {
            width: pageLoader.width
            SettingsHeading { text: qsTr("Keyboard shortcuts") }
            Repeater {
                model: root.settingsController ? root.settingsController.shortcutEntries : []
                delegate: SettingsRow {
                    id: shortcutRow
                    required property var modelData
                    title: root.shortcutLabel(modelData.id)
                    controlWidth: 270
                    Row {
                        width: parent.width
                        spacing: 8
                        Rectangle {
                            width: parent.width - editButton.width - 8
                            height: 32
                            radius: Theme.radius
                            color: Theme.paperWhite
                            border.color: Theme.opticalGray
                            Text { anchors.centerIn: parent; text: shortcutRow.modelData.nativeSequence; color: Theme.graphiteInk; font.family: Theme.monoFont; font.pixelSize: 12 }
                        }
                        SettingsButton {
                            id: editButton
                            objectName: "editShortcut-" + shortcutRow.modelData.id
                            text: qsTr("Change")
                            Accessible.name: qsTr("Change shortcut: %1").arg(shortcutRow.title)
                            onClicked: root.beginShortcut(shortcutRow.modelData.id, editButton)
                        }
                    }
                }
            }
            Item { width: 1; height: 16 }
            SettingsButton { text: qsTr("Restore default shortcuts…"); onClicked: resetShortcutsDialog.showConfirmation() }
            Item { width: 1; height: 12 }
            SettingsNotice { text: root.shortcutMessage; severity: "success" }
        }
    }
    Component {
        id: updatesPage
        Column {
            width: pageLoader.width
            SettingsHeading { text: qsTr("Update checks") }
            SettingsRow {
                title: qsTr("Automatically check for updates")
                description: qsTr("At most once every 24 hours. Downloads and installation always require your action.")
                controlWidth: 40
                SettingsToggle {
                    text: qsTr("Automatically check for updates")
                    checked: root.settingsController ? root.settingsController.automaticUpdateChecks : true
                    onToggled: if (root.settingsController) root.settingsController.automaticUpdateChecks = checked
                }
            }
            SettingsHeading { text: qsTr("Version status") }
            BodyText { text: qsTr("Installed version %1").arg(root.settingsController ? root.settingsController.applicationVersion : ""); color: Theme.graphiteInk }
            Item { width: 1; height: 12 }
            SettingsNotice {
                objectName: "settingsUpdateStatus"
                text: root.updateStatusText()
                severity: root.settingsController && root.settingsController.updateState === "error" ? "error" : root.settingsController && ["latest", "ready"].indexOf(root.settingsController.updateState) >= 0 ? "success" : "info"
            }
            Item { width: 1; height: 12 }
            ProgressBar {
                objectName: "settingsUpdateProgress"
                width: parent.width
                visible: root.settingsController && ["checking", "downloading", "verifying", "installing"].indexOf(root.settingsController.updateState) >= 0
                indeterminate: root.settingsController && (root.settingsController.updateState !== "downloading" || root.settingsController.updateDownloadProgress < 0)
                from: 0; to: 100
                value: root.settingsController ? Math.max(0, root.settingsController.updateDownloadProgress) : 0
            }
            Item { width: 1; height: 12 }
            Flow {
                width: parent.width
                spacing: 8
                SettingsButton {
                    objectName: "checkUpdateButton"
                    text: root.settingsController && root.settingsController.updateState === "error" ? qsTr("Try again") : qsTr("Check now")
                    enabled: root.settingsController && ["checking", "downloading", "verifying", "installing"].indexOf(root.settingsController.updateState) < 0
                    onClicked: root.settingsController.checkForUpdates()
                }
                SettingsButton {
                    objectName: "downloadUpdateButton"
                    visible: root.settingsController && ["available", "downloading", "ready"].indexOf(root.settingsController.updateState) >= 0
                    primary: root.settingsController && root.settingsController.updateState !== "downloading"
                    text: !root.settingsController ? "" : root.settingsController.updateState === "downloading" ? qsTr("Cancel download") : root.settingsController.updateState === "ready" ? qsTr("Install update") : qsTr("Download update")
                    onClicked: {
                        if (root.settingsController.updateState === "downloading") root.settingsController.cancelUpdateDownload()
                        else if (root.settingsController.updateState === "ready") updateInstallDialog.showConfirmation()
                        else root.settingsController.downloadUpdate()
                    }
                }
                SettingsButton {
                    visible: root.settingsController && root.settingsController.updateState === "error" && root.settingsController.releaseUrl.toString().length > 0
                    text: qsTr("View release ↗")
                    onClicked: root.settingsController.openReleasePage()
                }
            }
        }
    }
    Component {
        id: diagnosticsPage
        DiagnosticsPage { width: pageLoader.width; settingsController: root.settingsController; diagnostics: root.diagnostics }
    }

    Popup {
        id: shortcutRecorder
        objectName: "shortcutRecorder"
        parent: Overlay.overlay
        width: Math.min(440, parent ? parent.width - 48 : 440)
        height: recorderContent.implicitHeight + 40
        x: parent ? (parent.width - width) / 2 : 0
        y: parent ? (parent.height - height) / 2 : 0
        modal: true
        focus: true
        padding: 20
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: Theme.paperWhite; radius: 8; border.color: Theme.opticalGray }
        onClosed: {
            if (root.shortcutReturnFocus)
                root.shortcutReturnFocus.forceActiveFocus()
            else
                pageFlickable.forceActiveFocus()
        }
        contentItem: Column {
            id: recorderContent
            spacing: 14
            Text { width: parent.width; text: root.shortcutLabel(root.recordingAction); color: Theme.graphiteInk; font.family: Theme.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
            BodyText { text: qsTr("Press the keys you want to use, then click Save. Esc cancels.") }
            Rectangle {
                id: captureArea
                objectName: "shortcutCaptureArea"
                width: parent.width
                height: 64
                focus: true
                activeFocusOnTab: true
                radius: Theme.radius
                color: Theme.raisedSurface
                border.width: activeFocus ? 2 : 1
                border.color: activeFocus ? Theme.probeBlue : Theme.opticalGray
                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Record keyboard shortcut")
                Text { anchors.centerIn: parent; text: root.candidateLabel || qsTr("Press a shortcut"); color: root.candidateLabel ? Theme.graphiteInk : Theme.mutedInk; font.family: Theme.monoFont; font.pixelSize: 16 }
                MouseArea { anchors.fill: parent; onClicked: captureArea.forceActiveFocus() }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Escape) {
                        event.accepted = true
                        shortcutRecorder.close()
                        return
                    }
                    event.accepted = true
                    if (event.isAutoRepeat || !root.settingsController) return
                    const candidate = root.settingsController.shortcutSequenceForKey(event.key, event.modifiers)
                    if (!candidate.sequence) return
                    root.candidateSequence = candidate.sequence
                    root.candidateLabel = candidate.nativeSequence
                    root.shortcutError = ""
                }
            }
            SettingsNotice { text: root.shortcutError; severity: "error" }
            Row {
                anchors.right: parent.right
                spacing: 8
                SettingsButton { text: qsTr("Cancel"); onClicked: shortcutRecorder.close() }
                SettingsButton { objectName: "saveShortcutButton"; text: qsTr("Save"); primary: true; enabled: root.candidateSequence.length > 0; onClicked: root.saveShortcut() }
            }
        }
    }
    AppConfirmDialog {
        id: resetShortcutsDialog
        objectName: "resetShortcutsDialog"
        parent: Overlay.overlay
        dialogTitle: qsTr("Restore default shortcuts?")
        message: qsTr("Your custom keyboard shortcuts will be replaced with the defaults.")
        confirmText: qsTr("Restore")
        onConfirmed: {
            if (root.settingsController) root.settingsController.resetShortcuts()
            root.shortcutMessage = qsTr("Default shortcuts restored.")
            close()
        }
    }
    AppConfirmDialog {
        id: updateInstallDialog
        parent: Overlay.overlay
        dialogTitle: qsTr("Install this update?")
        message: qsTr("The verified installer will open and MVP Image Viewer will close.")
        confirmText: qsTr("Open installer")
        onConfirmed: { close(); if (root.settingsController) root.settingsController.installUpdate() }
    }
    AppConfirmDialog {
        id: restoreDialog
        objectName: "settingsRestoreDialog"
        parent: Overlay.overlay
        dialogTitle: qsTr("Restore default settings?")
        message: qsTr("Language, theme, color, shortcuts, updates, diagnostics, startup, and file operation preferences will be reset. Your image files are not changed.")
        confirmText: qsTr("Restore")
        onConfirmed: { if (root.settingsController) root.settingsController.restoreDefaults(); close() }
    }
}
