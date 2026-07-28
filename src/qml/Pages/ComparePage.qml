pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCore
import QtQuick.Dialogs as PlatformDialogs
import QtQuick.Layouts
import "../Isp"

Item {
    id: root
    objectName: "comparePage"

    required property var controller
    property bool designMode: false
    property var paths: []
    property var pixelValues: []
    property string transientMessage: ""
    property bool transientError: false
    property int selectedSlot: 0
    readonly property var comparisonCanvas: comparisonCanvasLoader.item

    signal closeRequested()
    signal screenshotFinished(bool success, url destination)

    function open(selectedPaths) {
        paths = selectedPaths
        pixelValues = []
        transientMessage = ""
        transientError = false
        selectedSlot = 0
        root.controller.setHoldCandidate(false)
        root.controller.setPresentationMode(0)
        root.controller.setSplitAmount(0.5)
        root.controller.setSynchronized(true)
        root.controller.setPaths(selectedPaths)
        root.controller.fitAll()
        if (root.controller.pixelValueVisible)
            initializePixelPlaceholders()
        forceActiveFocus()
    }

    function initializePixelPlaceholders() {
        const placeholders = []
        for (let slot = 0; slot < root.paths.length; ++slot)
            placeholders.push("Move over an image")
        pixelValues = placeholders
    }

    function showTransientMessage(message, isError) {
        transientMessage = message
        transientError = isError
        transientMessageTimer.restart()
    }

    function saveScreenshot() {
        const pictures = StandardPaths.writableLocation(StandardPaths.PicturesLocation)
        if (pictures && pictures.toString().length > 0)
            screenshotSaveDialog.currentFolder = pictures
        screenshotSaveDialog.selectedFile = screenshotSaveDialog.currentFolder
                + "/screen_shot_" + Date.now() + ".png"
        screenshotSaveDialog.open()
    }

    function captureScreenshot(destination) {
        const scheduled = stage.grabToImage(function(result) {
            const saved = result.saveToFile(destination)
            if (saved)
                root.showTransientMessage("Screenshot saved", false)
            else
                root.showTransientMessage("Unable to save screenshot", true)
            root.screenshotFinished(saved, destination)
        })
        if (!scheduled) {
            root.showTransientMessage("Unable to capture comparison", true)
            root.screenshotFinished(false, destination)
        }
    }

    function comparisonFileText(slot) {
        const currentRevision = root.controller.revision
        return root.controller.fileText(slot) || "Loading…"
    }

    function comparisonExifText(slot) {
        const currentRevision = root.controller.revision
        return root.controller.cameraText(slot)
    }

    function comparisonColumns() {
        return root.paths.length === 4 ? 2 : Math.max(1, root.paths.length)
    }

    function comparisonRows() {
        return root.paths.length === 4 ? 2 : 1
    }

    function cellX(slot, availableWidth) {
        if (root.controller.presentationMode !== 0)
            return 0
        const columns = comparisonColumns()
        const availableCellWidth = (availableWidth - 1 * (columns - 1)) / columns
        return (slot % columns) * (availableCellWidth + 1)
    }

    function cellY(slot, availableHeight) {
        if (root.controller.presentationMode !== 0)
            return 0
        const rows = comparisonRows()
        const availableCellHeight = (availableHeight - 1 * (rows - 1)) / rows
        return Math.floor(slot / comparisonColumns()) * (availableCellHeight + 1)
    }

    function cellWidth(availableWidth) {
        if (root.controller.presentationMode !== 0)
            return availableWidth
        return (availableWidth - 1 * (comparisonColumns() - 1)) / comparisonColumns()
    }

    function cellHeight(availableHeight) {
        if (root.controller.presentationMode !== 0)
            return availableHeight
        return (availableHeight - 1 * (comparisonRows() - 1)) / comparisonRows()
    }

    function requestHistograms() {
        for (let slot = 0; slot < root.paths.length; ++slot)
            root.controller.requestHistogram(slot)
    }

    PlatformDialogs.FileDialog {
        id: screenshotSaveDialog
        objectName: "comparisonScreenshotSaveDialog"
        title: "Save comparison screenshot"
        fileMode: PlatformDialogs.FileDialog.SaveFile
        nameFilters: ["PNG image (*.png)"]
        defaultSuffix: "png"
        onAccepted: root.captureScreenshot(selectedFile)
    }

    Keys.onEscapePressed: closeRequested()
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_B && event.modifiers === Qt.NoModifier && !event.isAutoRepeat) {
            root.controller.setHoldCandidate(true)
            event.accepted = true
        }
    }
    Keys.onReleased: function(event) {
        if (event.key === Qt.Key_B && event.modifiers === Qt.NoModifier && !event.isAutoRepeat) {
            root.controller.setHoldCandidate(false)
            event.accepted = true
        }
    }

    Connections {
        target: root.controller

        function onFrameChanged(slot, fullResolution) {
            if (root.controller.histogramVisible)
                root.controller.requestHistogram(slot)
        }

        function onHistogramVisibleChanged() {
            if (root.controller.histogramVisible)
                root.requestHistograms()
        }

        function onPixelValueVisibleChanged() {
            if (root.controller.pixelValueVisible && root.pixelValues.length === 0)
                root.initializePixelPlaceholders()
        }
    }

    Connections {
        target: root.Window.window

        function onActiveChanged() {
            if (root.Window.window && !root.Window.window.active)
                root.controller.setHoldCandidate(false)
        }
    }

    Timer {
        id: transientMessageTimer
        interval: 3000
        onTriggered: root.transientMessage = ""
    }

    Rectangle {
        anchors.fill: parent
        color: "#A0A0A0"
    }

    Rectangle {
        id: toolbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 38
        color: "#F8F9F8"

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: "#D7DBDD"
        }

        RowLayout {
            anchors.centerIn: parent
            spacing: 2

            AppIconButton {
                controlSize: 28
                renderedIconSize: 16
                checkable: true
                checked: root.controller.presentationMode === 0
                iconSource: "qrc:/icons/ui/side-by-side.svg"
                toolTipText: "Side by side · Hold Ctrl while zooming or panning to adjust one image"
                onClicked: root.controller.setPresentationMode(0)
            }
            AppIconButton {
                controlSize: 28
                renderedIconSize: 16
                enabled: root.paths.length === 2
                checkable: true
                checked: root.controller.presentationMode === 1
                iconSource: "qrc:/icons/ui/split-vertical.svg"
                toolTipText: "Vertical split"
                onClicked: root.controller.setPresentationMode(1)
            }
            AppIconButton {
                controlSize: 28
                renderedIconSize: 16
                enabled: root.paths.length === 2 && root.controller.presentationMode === 0
                checkable: true
                checked: root.controller.holdCandidate
                iconSource: "qrc:/icons/ui/cover.svg"
                toolTipText: "Hold B to inspect the candidate"
                onPressed: root.controller.setHoldCandidate(true)
                onReleased: root.controller.setHoldCandidate(false)
            }

            Rectangle {
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                implicitWidth: 1
                implicitHeight: 17
                color: "#D7DBDD"
            }

            AppIconButton {
                controlSize: 28
                renderedIconSize: 16
                iconSource: "qrc:/icons/ui/fit.svg"
                toolTipText: "Fit all images"
                onClicked: root.controller.fitAll()
            }
            AppIconButton {
                controlSize: 28
                renderedIconSize: 16
                iconSource: "qrc:/icons/ui/actual-size.svg"
                toolTipText: "Actual pixels (1:1)"
                onClicked: root.controller.actualPixelsAll()
            }
            AppIconButton {
                objectName: "compareRotateCounterClockwiseButton"
                controlSize: 28
                renderedIconSize: 16
                enabled: root.paths.length > 0
                iconSource: "qrc:/icons/ui/rotate-ccw-square.svg"
                toolTipText: "Rotate selected image 90° counterclockwise"
                onClicked: {
                    const error = root.controller.rotateCounterClockwise(root.selectedSlot)
                    if (error.length > 0) root.showTransientMessage(error, true)
                    else root.showTransientMessage("Image rotated and saved", false)
                }
            }
            AppIconButton {
                objectName: "compareRotateClockwiseButton"
                controlSize: 28
                renderedIconSize: 16
                enabled: root.paths.length > 0
                iconSource: "qrc:/icons/ui/rotate-cw-square.svg"
                toolTipText: "Rotate selected image 90° clockwise"
                onClicked: {
                    const error = root.controller.rotateClockwise(root.selectedSlot)
                    if (error.length > 0) root.showTransientMessage(error, true)
                    else root.showTransientMessage("Image rotated and saved", false)
                }
            }

            Rectangle {
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                implicitWidth: 1
                implicitHeight: 17
                color: "#D7DBDD"
            }

            AppIconButton {
                controlSize: 28
                renderedIconSize: 16
                checkable: true
                checked: root.controller.fileInformationVisible
                iconSource: "qrc:/icons/ui/info.svg"
                toolTipText: "File information"
                onClicked: root.controller.setFileInformationVisible(checked)
            }
            AppIconButton {
                controlSize: 28
                renderedIconSize: 16
                checkable: true
                checked: root.controller.exifVisible
                iconSource: "qrc:/icons/ui/exif.svg"
                toolTipText: "EXIF information"
                onClicked: root.controller.setExifVisible(checked)
            }
            AppIconButton {
                controlSize: 28
                renderedIconSize: 16
                checkable: true
                checked: root.controller.histogramVisible
                iconSource: "qrc:/icons/ui/histogram.svg"
                toolTipText: "Luma histogram"
                onClicked: root.controller.setHistogramVisible(checked)
            }
            AppIconButton {
                controlSize: 28
                renderedIconSize: 16
                checkable: true
                checked: root.controller.pixelValueVisible
                iconSource: "qrc:/icons/ui/pixel-probe.svg"
                toolTipText: "Pixel values"
                onClicked: root.controller.setPixelValueVisible(checked)
            }

            Rectangle {
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                implicitWidth: 1
                implicitHeight: 17
                color: "#D7DBDD"
            }

            AppIconButton {
                objectName: "saveComparisonScreenshotButton"
                controlSize: 28
                renderedIconSize: 16
                iconSource: "qrc:/icons/ui/screenshot.svg"
                toolTipText: "Save screenshot"
                onClicked: root.saveScreenshot()
            }
        }
    }

    Item {
        id: stage
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true

        Loader {
            id: comparisonCanvasLoader
            anchors.fill: parent
            active: root.visible || root.designMode
            source: root.designMode
                    ? Qt.resolvedUrl("../Isp/DesignCompareCanvas.qml")
                    : Qt.resolvedUrl("../Isp/ProductionCompareCanvas.qml")
            onLoaded: {
                if (item)
                    item.controller = root.controller
            }
        }

        Connections {
            target: root.comparisonCanvas
            ignoreUnknownSignals: true
            function onPixelHovered(sourceSlot, pixel, colorValue, valid) {
                if (valid)
                    root.pixelValues = root.controller.pixelTexts(sourceSlot, pixel.x, pixel.y)
            }
            function onSlotActivated(slot) { root.selectedSlot = slot }
        }

        Rectangle {
            visible: root.controller.presentationMode === 0 && root.comparisonColumns() > 1
            x: root.cellWidth(parent.width)
            width: 1
            height: parent.height
            color: "#B8B8B8"
        }
        Rectangle {
            visible: root.controller.presentationMode === 0 && root.paths.length === 3
            x: root.cellWidth(parent.width) * 2 + 1
            width: 1
            height: parent.height
            color: "#B8B8B8"
        }
        Rectangle {
            visible: root.controller.presentationMode === 0 && root.paths.length === 4
            y: root.cellHeight(parent.height)
            width: parent.width
            height: 1
            color: "#B8B8B8"
        }
        Rectangle {
            visible: root.controller.presentationMode === 1 && root.paths.length === 2
            x: root.comparisonCanvas ? root.comparisonCanvas.dividerPosition - 0.5 : 0
            width: 1
            height: parent.height
            color: "#E6E8E8"
        }

        Repeater {
            model: root.paths

            delegate: Item {
                id: comparisonCell

                required property int index
                property var navigationData: {
                    if (!root.comparisonCanvas)
                        return { "visible": false }
                    const currentNavigationRevision = root.comparisonCanvas.navigationRevision
                    return root.comparisonCanvas.navigationState(comparisonCell.index)
                }

                x: root.controller.presentationMode === 1
                   ? (comparisonCell.index === 0 ? 0 : root.comparisonCanvas.dividerPosition)
                   : root.cellX(comparisonCell.index, stage.width)
                y: root.cellY(comparisonCell.index, stage.height)
                width: root.controller.presentationMode === 1
                       ? (comparisonCell.index === 0
                          ? root.comparisonCanvas.dividerPosition
                          : stage.width - root.comparisonCanvas.dividerPosition)
                       : root.cellWidth(stage.width)
                height: root.cellHeight(stage.height)
                visible: root.controller.presentationMode === 0
                         || (root.controller.presentationMode === 1
                             && comparisonCell.index < 2)
                clip: true

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: root.selectedSlot === comparisonCell.index ? 2 : 0
                    border.color: "#7893A6"
                    z: 20
                }

                Rectangle {
                    visible: comparisonCell.navigationData.visible === true
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    width: comparisonCell.navigationData.width || 0
                    height: comparisonCell.navigationData.height || 0
                    radius: 3
                    color: "#991E2227"
                    border.width: 1
                    border.color: "#80FFFFFF"

                    Image {
                        anchors.fill: parent
                        anchors.margins: 3
                        source: "image://thumbnail/"
                                + encodeURIComponent(root.paths[comparisonCell.index])
                        sourceSize: Qt.size(90, 65)
                        fillMode: Image.Stretch
                        opacity: 0.72
                    }
                    Rectangle {
                        property rect normalizedViewport: comparisonCell.navigationData.viewport
                                                                 || Qt.rect(0, 0, 0, 0)
                        x: 3 + normalizedViewport.x * (parent.width - 6)
                        y: 3 + normalizedViewport.y * (parent.height - 6)
                        width: normalizedViewport.width * (parent.width - 6)
                        height: normalizedViewport.height * (parent.height - 6)
                        color: "#18FFFFFF"
                        border.width: 1
                        border.color: "white"
                    }
                    Label {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 5
                        text: comparisonCell.navigationData.zoom || ""
                        color: "white"
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                        style: Text.Outline
                        styleColor: "#D9000000"
                    }
                }

                Column {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 8
                    spacing: 2

                    Rectangle {
                        visible: root.controller.fileInformationVisible
                        width: Math.min(comparisonCell.width - 16,
                                        Math.min(250, fileInformation.implicitWidth + 14))
                        height: 20
                        radius: 3
                        color: "#A61E2227"

                        Label {
                            id: fileInformation
                            anchors.fill: parent
                            anchors.leftMargin: 7
                            anchors.rightMargin: 7
                            verticalAlignment: Text.AlignVCenter
                            text: root.comparisonFileText(comparisonCell.index)
                            color: "#F4F5F2"
                            font.pixelSize: 10
                            font.weight: Font.Medium
                            elide: Text.ElideMiddle
                        }
                    }

                    Rectangle {
                        visible: root.controller.exifVisible
                        width: Math.min(comparisonCell.width - 16,
                                        Math.min(380, exifInformation.implicitWidth + 14))
                        height: 19
                        radius: 3
                        color: "#A61E2227"

                        Label {
                            id: exifInformation
                            anchors.fill: parent
                            anchors.leftMargin: 7
                            anchors.rightMargin: 7
                            verticalAlignment: Text.AlignVCenter
                            text: root.comparisonExifText(comparisonCell.index)
                            color: "#E4E7E8"
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }
                    }

                    CompareLumaHistogram {
                        controller: root.controller
                        visible: root.controller.histogramVisible
                        width: Math.min(implicitWidth, comparisonCell.width - 16)
                        height: implicitHeight
                        slot: comparisonCell.index

                        Component.onCompleted: {
                            if (visible)
                                request()
                        }
                        onVisibleChanged: {
                            if (visible)
                                request()
                        }
                    }
                }

                Rectangle {
                    visible: root.controller.pixelValueVisible
                             && root.pixelValues[comparisonCell.index]
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 8
                    width: Math.min(parent.width - 16, pixelValue.implicitWidth + 14)
                    height: 20
                    radius: 3
                    color: "#A61E2227"

                    Label {
                        id: pixelValue
                        anchors.fill: parent
                        anchors.leftMargin: 7
                        anchors.rightMargin: 7
                        verticalAlignment: Text.AlignVCenter
                        text: root.pixelValues[comparisonCell.index] || ""
                        color: "#F4F5F2"
                        font.pixelSize: 9
                        font.family: Theme.monoFont
                    }
                }
            }
        }

        Rectangle {
            visible: root.transientMessage.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 10
            width: transientLabel.implicitWidth + 22
            height: 28
            radius: 4
            color: root.transientError ? "#E8B33B3B" : "#D91E2227"

            Label {
                id: transientLabel
                anchors.centerIn: parent
                text: root.transientMessage
                color: "white"
                font.pixelSize: 10
            }
        }
    }
}
