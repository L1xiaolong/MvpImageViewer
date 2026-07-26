pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "../Isp"

Item {
    id: root
    objectName: "fullScreenPage"

    required property var controller
    property var propertiesController: null
    property bool designMode: false
    property string iconPrefix: "qrc:/icons/ui/"
    property string pixelText: "Move over the image"
    property string inspectedPath: ""
    readonly property var imageCanvas: canvasLoader.item
    readonly property var navigationData: {
        if (!imageCanvas)
            return ({ "visible": false })
        const currentNavigationRevision = imageCanvas.navigationRevision
        return imageCanvas.navigationState(0)
    }

    signal closeRequested()

    function open(paths, initialIndex) {
        propertiesDialog.close()
        pixelText = "Move over the image"
        inspectedPath = ""
        controller.open(paths, initialIndex)
        forceActiveFocus()
    }

    function showPropertiesDialog() {
        inspectedPath = controller.currentPath
        propertiesDialog.openForPath(inspectedPath)
    }

    Keys.onEscapePressed: closeRequested()
    Keys.onLeftPressed: controller.showPrevious()
    Keys.onRightPressed: controller.showNext()
    Keys.onSpacePressed: controller.showNext()
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_F && event.modifiers === Qt.NoModifier) {
            controller.fitImage()
            event.accepted = true
        } else if (event.key === Qt.Key_1 && event.modifiers === Qt.NoModifier) {
            controller.actualPixels()
            event.accepted = true
        }
    }

    Rectangle { anchors.fill: parent; color: "#A0A0A0" }

    Loader {
        id: canvasLoader
        anchors.fill: parent
        source: root.designMode
                ? Qt.resolvedUrl("../Isp/DesignFullScreenCanvas.qml")
                : Qt.resolvedUrl("../Isp/ProductionFullScreenCanvas.qml")
        onLoaded: if (item) item.controller = root.controller
    }

    Connections {
        target: root.imageCanvas
        ignoreUnknownSignals: true
        function onPixelHovered(sourceSlot, pixel, colorValue, valid) {
            if (!valid) {
                root.pixelText = "Move over the image"
                return
            }
            root.pixelText = "(" + pixel.x + "," + pixel.y + ") RGBA("
                    + Math.round(colorValue.r * 255) + ","
                    + Math.round(colorValue.g * 255) + ","
                    + Math.round(colorValue.b * 255) + ","
                    + Math.round(colorValue.a * 255) + ")"
        }
        function onContextMenuRequested(position) {
            fullScreenContextMenu.popup(position.x, position.y)
        }
    }

    Connections {
        target: root.controller
        function onCloseRequested() { root.closeRequested() }
        function onStateChanged() {
            if (propertiesDialog.opened &&
                    root.controller.currentPath !== root.inspectedPath) {
                root.inspectedPath = root.controller.currentPath
                if (root.propertiesController)
                    root.propertiesController.loadPath(root.inspectedPath)
            }
        }
    }

    Rectangle {
        id: imageInfo
        objectName: "fullScreenImageInfo"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 16
        width: Math.min(420, Math.max(220, imageName.implicitWidth + 32))
        height: 56
        radius: 7
        color: "#B81E2227"
        border.width: 1
        border.color: "#4DFFFFFF"
        z: 25

        Text {
            id: imageName
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            text: root.controller.fileName
            color: "white"
            elide: Text.ElideMiddle
            font.family: Theme.uiFont
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Text {
            anchors.left: imageName.left
            anchors.right: imageName.right
            anchors.top: imageName.bottom
            anchors.topMargin: 5
            text: root.controller.fileType + " · " + root.controller.fileSizeText
            color: "#D9FFFFFF"
            font.family: Theme.monoFont
            font.pixelSize: 10
        }
    }

    Rectangle {
        id: pixelInfo
        objectName: "fullScreenPixelInfo"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        width: Math.min(460, Math.max(190, pixelLabel.implicitWidth + 24))
        height: 34
        radius: 6
        color: "#B81E2227"
        border.width: 1
        border.color: "#4DFFFFFF"
        z: 25

        Text {
            id: pixelLabel
            anchors.centerIn: parent
            text: root.pixelText
            color: "white"
            font.family: Theme.monoFont
            font.pixelSize: 11
        }
    }

    Rectangle {
        id: navigationOverlay
        objectName: "fullScreenNavigationOverlay"
        visible: root.navigationData.visible === true
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 16
        width: root.navigationData.width || 0
        height: root.navigationData.height || 0
        radius: 3
        color: "#991E2227"
        border.width: 1
        border.color: "#80FFFFFF"
        z: 25

        Image {
            anchors.fill: parent
            anchors.margins: 3
            source: root.controller.currentPath
                    ? "image://thumbnail/" + encodeURIComponent(root.controller.currentPath)
                    : ""
            sourceSize: Qt.size(90, 65)
            fillMode: Image.Stretch
            opacity: 0.72
        }
        Rectangle {
            property rect normalizedViewport: root.navigationData.viewport || Qt.rect(0, 0, 0, 0)
            x: 3 + normalizedViewport.x * (parent.width - 6)
            y: 3 + normalizedViewport.y * (parent.height - 6)
            width: normalizedViewport.width * (parent.width - 6)
            height: normalizedViewport.height * (parent.height - 6)
            color: "#18FFFFFF"
            border.width: 1
            border.color: "white"
        }
        Label {
            objectName: "fullScreenZoomLabel"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 5
            text: root.navigationData.zoom || ""
            color: "white"
            font.pixelSize: 8
            font.weight: Font.DemiBold
            style: Text.Outline
            styleColor: "#D9000000"
        }
    }

    ImagePropertiesDialog {
        id: propertiesDialog
        parent: root
        controller: root.propertiesController
        iconPrefix: root.iconPrefix
        // Popup focus restoration is inconsistent after clicking the close button on macOS.
        // Return focus explicitly so the page-level Escape/navigation handlers keep working.
        onClosed: Qt.callLater(function() { root.forceActiveFocus() })
    }

    Rectangle {
        visible: root.controller.loading || root.controller.errorText.length > 0
        anchors.centerIn: parent
        width: Math.min(420, messageText.implicitWidth + 36)
        height: 42
        radius: 6
        color: "#DCEEF1F1"
        border.width: 1
        border.color: "#BCC5C8"
        Text {
            id: messageText
            anchors.centerIn: parent
            text: root.controller.errorText.length > 0
                  ? root.controller.errorText : "Loading " + root.controller.fileName + "…"
            color: root.controller.errorText.length > 0 ? Theme.danger : Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 11
        }
    }

    AppMenu {
        id: fullScreenContextMenu
        objectName: "fullScreenContextMenu"
        AppMenuItem { text: "Cut"; shortcutText: Qt.platform.os === "osx" ? "⌘X" : "Ctrl+X"; onTriggered: root.controller.copyCurrent(true) }
        AppMenuItem { text: "Copy"; shortcutText: Qt.platform.os === "osx" ? "⌘C" : "Ctrl+C"; onTriggered: root.controller.copyCurrent(false) }
        AppMenuItem { text: "Rename…"; onTriggered: renameDialog.openWith(root.controller.fileName) }
        AppMenuItem { text: "Move to Trash"; destructive: true; onTriggered: trashDialog.showConfirmation() }
        AppMenuSeparator {}
        AppMenuItem { text: Qt.platform.os === "osx" ? "Reveal in Finder" : "Show in File Explorer"; onTriggered: root.controller.revealCurrent() }
        AppMenuSeparator {}
        AppMenu {
            title: "Display"
            AppMenuItem { text: "1:1"; onTriggered: root.controller.actualPixels() }
            AppMenuItem { text: "Fit"; onTriggered: root.controller.fitImage() }
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "fullScreenPropertiesAction"
            text: "Properties"
            onTriggered: root.showPropertiesDialog()
        }
    }

    AppTextInputDialog {
        id: renameDialog
        parent: root
        dialogTitle: "Rename image"
        description: "Enter a new name for the current image"
        acceptText: "Rename"
        onSubmitted: function(text) { complete(root.controller.renameCurrentTo(text)) }
    }

    AppConfirmDialog {
        id: trashDialog
        parent: root
        dialogTitle: "Move to Trash?"
        message: "The current image will be moved to the system Trash."
        confirmText: "Move"
        destructive: true
        onConfirmed: complete(root.controller.moveCurrentToTrash())
    }

    Shortcut { sequence: "PageUp"; onActivated: root.controller.showPrevious() }
    Shortcut { sequence: "PageDown"; onActivated: root.controller.showNext() }
    Shortcut { sequences: [StandardKey.Copy]; onActivated: root.controller.copyCurrent(false) }
    Shortcut { sequences: [StandardKey.Cut]; onActivated: root.controller.copyCurrent(true) }
}
