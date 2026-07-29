pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "."

Popup {
    id: root
    objectName: "settingsCard"
    width: Math.min(780, parent ? parent.width - 56 : 780)
    height: Math.min(570, parent ? parent.height - 56 : 570)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle {
        color: Theme.overlay
    }

    background: Rectangle {
        color: Theme.sensorWhite
        radius: 12
        border.width: 1
        border.color: Theme.opticalGray
    }

    property int currentSection: 0
    property var settingsController: null
    property string shortcutMessage: ""

    function shortcutLabel(action) {
        const labels = {
            "openFolder": qsTr("Open folder"),
            "find": qsTr("Find files"),
            "settings": qsTr("Open settings"),
            "toggleNavigator": qsTr("Show or hide sidebar"),
            "compare": qsTr("Compare selected images"),
            "rename": qsTr("Rename selected item"),
            "newFolder": qsTr("Create new folder")
        }
        return labels[action] || action
    }

    function updateStatusText() {
        if (!root.settingsController)
            return qsTr("Update service is unavailable.")
        switch (root.settingsController.updateState) {
        case "checking":
            return qsTr("Checking GitHub Releases…")
        case "available":
            return qsTr("Version %1 is available.").arg(root.settingsController.latestVersion)
        case "latest":
            return qsTr("You’re up to date. Version %1 is the latest release.")
                    .arg(root.settingsController.latestVersion)
        case "error":
            return qsTr("Couldn’t check for updates. Check your connection and try again.")
        default:
            return qsTr("Check for a newer published version of MVP Image Viewer.")
        }
    }

    component SettingLabel: Text {
        color: Theme.graphiteInk
        font.family: Theme.uiFont
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }

    component SettingDescription: Text {
        color: Theme.mutedInk
        font.family: Theme.uiFont
        font.pixelSize: 12
        wrapMode: Text.Wrap
        lineHeight: 1.25
    }

    component SettingsCheckBox: CheckBox {
        id: checkControl
        implicitWidth: checkRow.implicitWidth
        implicitHeight: 40
        spacing: 10
        indicator: Rectangle {
            x: checkControl.leftPadding
            anchors.verticalCenter: parent.verticalCenter
            width: 24
            height: 24
            radius: 4
            color: checkControl.checked ? Theme.primaryButton : Theme.raisedSurface
            border.width: checkControl.activeFocus ? 2 : 1
            border.color: checkControl.activeFocus ? Theme.probeBlue : Theme.opticalGray
            Text {
                anchors.centerIn: parent
                visible: checkControl.checked
                text: "✓"
                color: Theme.primaryButtonText
                font.family: Theme.uiFont
                font.pixelSize: 17
                font.weight: Font.Bold
            }
        }
        contentItem: Row {
            id: checkRow
            leftPadding: checkControl.indicator.width + checkControl.spacing
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: checkControl.text
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 13
            }
        }
    }

    component SettingsComboBox: ComboBox {
        id: comboControl
        leftPadding: 12
        rightPadding: 36
        contentItem: Text {
            text: comboControl.displayText
            color: Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"
            color: Theme.mutedInk
            font.family: Theme.uiFont
            font.pixelSize: 17
        }
        background: Rectangle {
            radius: 5
            color: Theme.raisedSurface
            border.width: comboControl.activeFocus ? 2 : 1
            border.color: comboControl.activeFocus ? Theme.probeBlue : Theme.opticalGray
        }
        delegate: ItemDelegate {
            id: comboDelegate
            required property int index
            required property var modelData
            width: comboControl.width - 2
            height: 36
            contentItem: Text {
                text: comboDelegate.modelData
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: comboDelegate.highlighted
                       ? Theme.explorerSelectionBg : Theme.raisedSurface
            }
        }
        popup: Popup {
            y: comboControl.height + 3
            width: comboControl.width
            implicitHeight: Math.min(contentItem.implicitHeight + 2, 180)
            padding: 1
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: comboControl.popup.visible ? comboControl.delegateModel : null
                currentIndex: comboControl.highlightedIndex
            }
            background: Rectangle {
                radius: 5
                color: Theme.raisedSurface
                border.width: 1
                border.color: Theme.opticalGray
            }
        }
    }

    component SettingsActionButton: Button {
        id: actionButton
        height: 34
        leftPadding: 14
        rightPadding: 14
        contentItem: Text {
            text: actionButton.text
            color: actionButton.enabled ? Theme.graphiteInk : Theme.faintInk
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.family: Theme.uiFont
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        background: Rectangle {
            radius: 6
            color: actionButton.down ? Theme.pressedSurface
                  : actionButton.hovered ? Theme.softHover : Theme.raisedSurface
            border.width: 1
            border.color: actionButton.activeFocus ? Theme.probeBlue : Theme.opticalGray
        }
    }

    Rectangle {
        id: sidebar
        anchors.left: parent.left
        anchors.leftMargin: 1
        anchors.top: parent.top
        anchors.topMargin: 1
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 1
        width: 207
        radius: 11
        color: Theme.paperWhite

        // Keep the navigation edge square while preserving the outer left corners.
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.radius
            color: parent.color
        }

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.opticalGray
        }

        Column {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 4

            Text {
                text: qsTr("ISP")
                color: Theme.exposureAmber
                font.family: Theme.monoFont
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 2
            }
            Text {
                text: qsTr("Settings")
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 22
                font.weight: Font.Bold
                bottomPadding: 18
            }

            Repeater {
                model: [
                    { label: qsTr("General"), color: Theme.probeBlue },
                    { label: qsTr("Appearance"), color: Theme.exposureAmber },
                    { label: qsTr("Color & display"), color: Theme.primaryButton },
                    { label: qsTr("Shortcuts"), color: Theme.success },
                    { label: qsTr("Updates"), color: Theme.danger },
                    { label: qsTr("Help"), color: Theme.faintInk }
                ]
                delegate: Button {
                    id: navigationButton
                    required property int index
                    required property var modelData
                    width: 172
                    height: 40
                    text: navigationButton.modelData.label
                    hoverEnabled: true
                    onClicked: root.currentSection = navigationButton.index

                    contentItem: Row {
                        spacing: 10
                        leftPadding: 12
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 7
                            height: 7
                            radius: 4
                            color: navigationButton.modelData.color
                            opacity: root.currentSection === navigationButton.index
                                     ? 1 : Theme.disabledOpacity
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: navigationButton.modelData.label
                            color: Theme.graphiteInk
                            font.family: Theme.uiFont
                            font.pixelSize: 13
                            font.weight: root.currentSection === navigationButton.index
                                         ? Font.DemiBold : Font.Normal
                        }
                    }
                    background: Rectangle {
                        radius: 6
                        color: root.currentSection === navigationButton.index
                               ? Theme.explorerSelectionBg
                               : navigationButton.hovered ? Theme.softHover : "transparent"
                        border.width: navigationButton.activeFocus ? 1 : 0
                        border.color: Theme.probeBlue
                    }
                }
            }

            Item { width: 1; height: 14 }
            Rectangle { width: 172; height: 1; color: Theme.opticalGray }
            Text {
                width: 172
                topPadding: 12
                text: qsTr("Version %1").arg(root.settingsController
                                             ? root.settingsController.applicationVersion : "")
                color: Theme.faintInk
                font.family: Theme.monoFont
                font.pixelSize: Theme.metadataFontSize
            }
        }
    }

    Item {
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.top

        Flickable {
            id: pageFlickable
            anchors.fill: parent
            contentWidth: width
            contentHeight: Math.max(height, pageLoader.item
                                    ? pageLoader.y + pageLoader.item.implicitHeight + 30 : 500)
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Loader {
                id: pageLoader
                x: 34
                y: 30
                width: parent.width - 68
                sourceComponent: [generalPage, appearancePage, colorDisplayPage, shortcutsPage,
                                  updatesPage, helpPage][root.currentSection]
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
        height: 63
        radius: 11
        color: Theme.paperWhite

        // Square the footer's inner edges while retaining the outer bottom-right corner.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.radius
            color: parent.color
        }
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: parent.radius
            color: parent.color
        }

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.opticalGray
        }

        Button {
            id: restoreButton
            anchors.left: parent.left
            anchors.leftMargin: 34
            anchors.verticalCenter: parent.verticalCenter
            width: 154
            height: 34
            text: qsTr("Restore defaults")
            onClicked: restoreDialog.showConfirmation()
            contentItem: Text {
                text: restoreButton.text
                color: Theme.graphiteInk
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.uiFont
                font.pixelSize: 12
            }
            background: Rectangle {
                radius: 6
                color: restoreButton.down ? Theme.pressedSurface
                      : restoreButton.hovered ? Theme.softHover : Theme.paperWhite
                border.width: 1
                border.color: Theme.opticalGray
            }
        }

        Button {
            id: doneButton
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            width: 88
            height: 34
            text: qsTr("Done")
            onClicked: root.close()
            contentItem: Text {
                text: doneButton.text
                color: Theme.primaryButtonText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.family: Theme.uiFont
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            background: Rectangle {
                radius: 6
                color: doneButton.hovered ? Theme.primaryButtonHover : Theme.primaryButton
            }
        }
    }

    Component {
        id: generalPage
        Column {
            spacing: 0

            Text {
                text: qsTr("General")
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 22
                font.weight: Font.Bold
            }
            SettingDescription {
                width: parent.width
                topPadding: 6
                bottomPadding: 26
                text: qsTr("Choose how the viewer starts and handles everyday file operations.")
            }

            SettingLabel { text: qsTr("Language") }
            SettingDescription {
                width: parent.width
                topPadding: 4
                bottomPadding: 10
                text: qsTr("Changes are applied immediately across open windows.")
            }
            SettingsComboBox {
                id: languageCombo
                width: 250
                height: 36
                model: [qsTr("Use system language"), "简体中文", "English"]
                currentIndex: !root.settingsController
                              || root.settingsController.language === "system" ? 0
                              : root.settingsController.language === "zh_CN" ? 1 : 2
                onActivated: {
                    if (root.settingsController)
                        root.settingsController.language =
                                currentIndex === 0 ? "system"
                                : currentIndex === 1 ? "zh_CN" : "en"
                }
            }

            Item { width: 1; height: 24 }
            Rectangle { width: parent.width; height: 1; color: Theme.opticalGray }

            SettingLabel {
                topPadding: 24
                text: qsTr("Startup and file operations")
            }
            SettingsCheckBox {
                topPadding: 10
                text: qsTr("Restore the last opened directory")
                checked: root.settingsController
                         ? root.settingsController.restoreLastDirectory : true
                onToggled: {
                    if (root.settingsController)
                        root.settingsController.restoreLastDirectory = checked
                }
            }
            SettingsCheckBox {
                text: qsTr("Ask before moving files to the Trash")
                checked: root.settingsController ? root.settingsController.confirmTrash : true
                onToggled: {
                    if (root.settingsController)
                        root.settingsController.confirmTrash = checked
                }
            }
        }
    }

    Component {
        id: appearancePage
        Column {
            spacing: 0

            Text {
                text: qsTr("Appearance")
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 22
                font.weight: Font.Bold
            }
            SettingDescription {
                width: parent.width
                topPadding: 6
                bottomPadding: 26
                text: qsTr("Tune the workspace for bright studios or low-light inspection.")
            }
            SettingLabel { text: qsTr("Theme") }
            SettingDescription {
                width: parent.width
                topPadding: 4
                bottomPadding: 14
                text: qsTr("The preview uses the same surfaces and signals as the image workspace.")
            }

            Row {
                spacing: 12
                Repeater {
                    model: [
                        { value: "system", label: qsTr("System"), dark: false },
                        { value: "light", label: qsTr("Light"), dark: false },
                        { value: "dark", label: qsTr("Dark"), dark: true }
                    ]
                    delegate: Button {
                        id: themeCard
                        required property var modelData
                        width: 158
                        height: 174
                        checked: root.settingsController
                                 && root.settingsController.theme === themeCard.modelData.value
                        onClicked: {
                            if (root.settingsController)
                                root.settingsController.theme = themeCard.modelData.value
                        }
                        contentItem: Column {
                            spacing: 10
                            Rectangle {
                                width: 142
                                height: 126
                                radius: 5
                                color: themeCard.modelData.dark ? "#1E1E1E" : "#F5F6F3"
                                border.width: 1
                                border.color: themeCard.modelData.dark ? "#3F3F46" : "#D4DADD"
                                Rectangle {
                                    x: 8; y: 8; width: 34; height: 110
                                    radius: 3
                                    color: themeCard.modelData.dark ? "#252526" : "#FFFFFF"
                                }
                                Rectangle {
                                    x: 49; y: 10; width: 83; height: 14
                                    radius: 2
                                    color: themeCard.modelData.dark ? "#333333" : "#E4E9EB"
                                }
                                Rectangle {
                                    x: 49; y: 31; width: 39; height: 55
                                    radius: 3
                                    color: themeCard.modelData.dark ? "#094771" : "#DCE8F0"
                                }
                                Rectangle {
                                    x: 93; y: 31; width: 39; height: 55
                                    radius: 3
                                    color: themeCard.modelData.dark ? "#3C3C3C" : "#F1E5D3"
                                }
                                Repeater {
                                    model: 5
                                    Rectangle {
                                        required property int index
                                        x: 49; y: 96 + index * 4
                                        width: 16 + index * 12; height: 2
                                        color: index === 4 ? "#007ACC"
                                              : themeCard.modelData.dark ? "#858585" : "#ABB6BC"
                                    }
                                }
                            }
                            Text {
                                width: 142
                                text: themeCard.modelData.label
                                horizontalAlignment: Text.AlignHCenter
                                color: Theme.graphiteInk
                                font.family: Theme.uiFont
                                font.pixelSize: 12
                                font.weight: themeCard.checked ? Font.DemiBold : Font.Normal
                            }
                        }
                        background: Rectangle {
                            radius: 7
                            color: themeCard.hovered ? Theme.softHover : "transparent"
                            border.width: themeCard.checked || themeCard.activeFocus ? 2 : 1
                            border.color: themeCard.checked ? Theme.probeBlue : Theme.opticalGray
                        }
                    }
                }
            }
        }
    }

    Component {
        id: colorDisplayPage
        Column {
            spacing: 0

            Text {
                text: qsTr("Color & display")
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 22
                font.weight: Font.Bold
            }
            SettingDescription {
                width: parent.width
                topPadding: 6
                bottomPadding: 22
                text: qsTr("Control how encoded images are interpreted and presented on the canvas.")
            }

            Rectangle {
                width: parent.width
                height: 74
                radius: 8
                color: Theme.paperWhite
                border.width: 1
                border.color: Theme.opticalGray
                Row {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 14
                    Rectangle {
                        width: 42
                        height: 42
                        radius: 7
                        color: Theme.explorerSelectionBg
                        Text {
                            anchors.centerIn: parent
                            text: "RGB"
                            color: Theme.probeBlue
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                            font.weight: Font.Bold
                        }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        SettingLabel { text: qsTr("Display color space: sRGB") }
                        SettingDescription {
                            width: 380
                            text: qsTr("Embedded RGB profiles are converted to the app’s fixed sRGB display space.")
                        }
                    }
                }
            }

            SettingsCheckBox {
                topPadding: 10
                text: qsTr("Apply embedded ICC color profiles")
                enabled: root.settingsController
                         && root.settingsController.colorManagementAvailable
                checked: root.settingsController
                         ? root.settingsController.applyEmbeddedColorProfiles : true
                onToggled: {
                    if (root.settingsController)
                        root.settingsController.applyEmbeddedColorProfiles = checked
                }
            }
            SettingDescription {
                width: parent.width
                leftPadding: 34
                bottomPadding: 4
                text: root.settingsController
                      && !root.settingsController.colorManagementAvailable
                      ? qsTr("Color profile conversion is unavailable in this build.")
                      : qsTr("Recommended for JPEG and PNG files created in Adobe RGB or other RGB spaces.")
            }
            SettingsCheckBox {
                text: qsTr("Preserve high bit depth when available")
                checked: root.settingsController
                         ? root.settingsController.preserveHighBitDepth : true
                onToggled: {
                    if (root.settingsController)
                        root.settingsController.preserveHighBitDepth = checked
                }
            }
            SettingDescription {
                width: parent.width
                leftPadding: 34
                bottomPadding: 8
                text: qsTr("Keeps 16-bit and floating-point samples for GPU display. Disable to convert them to 8-bit sRGB.")
            }

            Rectangle { width: parent.width; height: 1; color: Theme.opticalGray }
            SettingLabel {
                topPadding: 18
                text: qsTr("Image presentation")
            }
            SettingsCheckBox {
                topPadding: 5
                text: qsTr("Honor EXIF orientation")
                checked: root.settingsController
                         ? root.settingsController.honorExifOrientation : true
                onToggled: {
                    if (root.settingsController)
                        root.settingsController.honorExifOrientation = checked
                }
            }
            SettingDescription {
                width: parent.width
                leftPadding: 34
                bottomPadding: 10
                text: qsTr("Automatically rotates JPEG and PNG images according to their metadata.")
            }
            SettingLabel { text: qsTr("Canvas background") }
            SettingDescription {
                width: parent.width
                topPadding: 4
                bottomPadding: 9
                text: qsTr("Choose a surround that makes exposure, edges, and transparency easier to inspect.")
            }
            SettingsComboBox {
                id: canvasBackgroundCombo
                width: 250
                height: 36
                model: [qsTr("Neutral gray"), qsTr("Dark gray"),
                        qsTr("Black"), qsTr("White")]
                currentIndex: !root.settingsController
                              || root.settingsController.canvasBackground === "neutral" ? 0
                              : root.settingsController.canvasBackground === "dark" ? 1
                              : root.settingsController.canvasBackground === "black" ? 2 : 3
                onActivated: {
                    if (root.settingsController)
                        root.settingsController.canvasBackground =
                                currentIndex === 0 ? "neutral"
                                : currentIndex === 1 ? "dark"
                                : currentIndex === 2 ? "black" : "white"
                }
            }
        }
    }

    Component {
        id: shortcutsPage
        Column {
            spacing: 0

            Text {
                text: qsTr("Keyboard shortcuts")
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 22
                font.weight: Font.Bold
            }
            SettingDescription {
                width: parent.width
                topPadding: 6
                bottomPadding: 20
                text: qsTr("Enter shortcuts such as Ctrl+O, Ctrl+Shift+N, F2, or C. Duplicate assignments are rejected.")
            }

            Repeater {
                model: root.settingsController ? root.settingsController.shortcutEntries : []
                delegate: Item {
                    id: shortcutRow
                    required property var modelData
                    width: parent.width
                    height: 50

                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 230
                        text: root.shortcutLabel(shortcutRow.modelData.id)
                        color: Theme.graphiteInk
                        elide: Text.ElideRight
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                    }
                    TextField {
                        id: shortcutField
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 210
                        height: 34
                        text: shortcutRow.modelData.sequence
                        selectByMouse: true
                        color: Theme.graphiteInk
                        placeholderTextColor: Theme.faintInk
                        font.family: Theme.monoFont
                        font.pixelSize: 12
                        onEditingFinished: {
                            if (!root.settingsController)
                                return
                            const result = root.settingsController.setShortcut(
                                             shortcutRow.modelData.id, text)
                            if (result === "invalid" || result === "unknown") {
                                text = root.settingsController.shortcutFor(
                                            shortcutRow.modelData.id)
                                root.shortcutMessage = qsTr("Enter a valid keyboard shortcut.")
                            } else if (result.length > 0) {
                                text = root.settingsController.shortcutFor(
                                            shortcutRow.modelData.id)
                                root.shortcutMessage =
                                        qsTr("This shortcut is already assigned to “%1”.")
                                        .arg(root.shortcutLabel(result))
                            } else {
                                root.shortcutMessage = qsTr("Shortcut saved.")
                            }
                        }
                        background: Rectangle {
                            radius: 5
                            color: Theme.raisedSurface
                            border.width: shortcutField.activeFocus ? 2 : 1
                            border.color: shortcutField.activeFocus
                                          ? Theme.probeBlue : Theme.opticalGray
                        }
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: Theme.opticalGray }
            Item { width: 1; height: 14 }
            Row {
                spacing: 12
                SettingsActionButton {
                    width: 146
                    text: qsTr("Reset shortcuts")
                    onClicked: {
                        if (root.settingsController)
                            root.settingsController.resetShortcuts()
                        root.shortcutMessage = qsTr("Default shortcuts restored.")
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 290
                    text: root.shortcutMessage
                    color: Theme.mutedInk
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: 11
                }
            }
        }
    }

    Component {
        id: updatesPage
        Column {
            spacing: 0

            Text {
                text: qsTr("Updates")
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 22
                font.weight: Font.Bold
            }
            SettingDescription {
                width: parent.width
                topPadding: 6
                bottomPadding: 24
                text: qsTr("Keep the viewer current without interrupting image work.")
            }
            SettingsCheckBox {
                text: qsTr("Automatically check for updates")
                checked: root.settingsController
                         ? root.settingsController.automaticUpdateChecks : true
                onToggled: {
                    if (root.settingsController)
                        root.settingsController.automaticUpdateChecks = checked
                }
            }
            SettingDescription {
                width: parent.width
                bottomPadding: 20
                text: qsTr("Checks at most once every 24 hours. Updates are never downloaded or installed without you.")
            }

            Rectangle {
                width: parent.width
                height: 132
                radius: 8
                color: Theme.paperWhite
                border.width: 1
                border.color: root.settingsController
                              && root.settingsController.updateState === "available"
                              ? Theme.successBorder : Theme.opticalGray
                Column {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 7
                    SettingLabel {
                        text: qsTr("Installed version %1").arg(
                                  root.settingsController
                                  ? root.settingsController.applicationVersion : "")
                    }
                    SettingDescription {
                        width: parent.width
                        text: root.updateStatusText()
                    }
                    Row {
                        spacing: 10
                        SettingsActionButton {
                            width: 142
                            text: root.settingsController
                                  && root.settingsController.updateState === "checking"
                                  ? qsTr("Checking…") : qsTr("Check now")
                            enabled: root.settingsController
                                     && root.settingsController.updateState !== "checking"
                            onClicked: root.settingsController.checkForUpdates()
                        }
                        SettingsActionButton {
                            width: 150
                            visible: root.settingsController
                                     && root.settingsController.updateState === "available"
                            text: qsTr("View update")
                            onClicked: root.settingsController.openReleasePage()
                        }
                    }
                }
            }
        }
    }

    Component {
        id: helpPage
        Column {
            spacing: 0

            Text {
                text: qsTr("Help and guide")
                color: Theme.graphiteInk
                font.family: Theme.uiFont
                font.pixelSize: 22
                font.weight: Font.Bold
            }
            SettingDescription {
                width: parent.width
                topPadding: 6
                bottomPadding: 22
                text: qsTr("A quick reference for the main image inspection workflows.")
            }

            Repeater {
                model: [
                    {
                        title: qsTr("Browse"),
                        body: qsTr("Choose a folder in the sidebar, then use grid, list, or gallery view to inspect files.")
                    },
                    {
                        title: qsTr("Compare"),
                        body: qsTr("Select 2–4 images and press C to open synchronized comparison.")
                    },
                    {
                        title: qsTr("Inspect"),
                        body: qsTr("Open an image full screen, then use Fit, 100%, EXIF, histogram, and pixel probe tools.")
                    }
                ]
                delegate: Rectangle {
                    id: guideRow
                    required property var modelData
                    width: parent.width
                    height: 74
                    radius: 7
                    color: Theme.paperWhite
                    border.width: 1
                    border.color: Theme.opticalGray
                    Column {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4
                        SettingLabel { text: guideRow.modelData.title }
                        SettingDescription {
                            width: parent.width
                            text: guideRow.modelData.body
                        }
                    }
                }
            }
            Item { width: 1; height: 14 }
            SettingsActionButton {
                width: 170
                text: qsTr("Open online guide")
                onClicked: {
                    if (root.settingsController)
                        root.settingsController.openUserGuide()
                }
            }
        }
    }

    AppConfirmDialog {
        id: restoreDialog
        parent: root.contentItem
        dialogTitle: qsTr("Restore default settings?")
        message: qsTr("Language, theme, shortcuts, updates, startup, and confirmation preferences will be reset.")
        confirmText: qsTr("Restore")
        onConfirmed: {
            if (root.settingsController)
                root.settingsController.restoreDefaults()
            close()
        }
    }

}
