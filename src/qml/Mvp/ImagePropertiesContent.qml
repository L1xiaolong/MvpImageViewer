pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "."

Rectangle {
    id: root
    objectName: "imagePropertiesContent"
    property var controller: null
    property string iconPrefix: Theme.iconPrefix
    property bool draggable: false
    property bool inspectionActive: true
    property int selectionCount: 1
    property int currentTab: 0
    property var scrollPositions: [0, 0, 0]
    property string displayedPath: controller ? controller.path : ""
    readonly property bool hasItem: !!controller && controller.path.length > 0
    readonly property int effectiveTab: controller && controller.directory ? 0 : currentTab
    readonly property bool textSelectionActive: {
        const item = root.Window.window ? root.Window.window.activeFocusItem : null
        return !!item && item.readOnly === true && item.selectedText !== undefined
    }
    readonly property var groups: !hasItem ? [] : effectiveTab === 0
        ? controller.overviewGroups : effectiveTab === 2 ? controller.advancedGroups : []
    signal closeRequested()
    signal headerDragStarted(point position)
    signal headerDragMoved(point position)
    color: Theme.paperWhite
    clip: true

    function selectTab(index) {
        scrollPositions[effectiveTab] = scroll.contentY
        currentTab = index
        Qt.callLater(function() {
            body.forceLayout()
            scroll.contentY = Math.min(scrollPositions[effectiveTab] || 0,
                                       Math.max(0, scroll.contentHeight - scroll.height))
        })
    }
    onDisplayedPathChanged: {
        scrollPositions = [0, 0, 0]
        scroll.contentY = 0
    }

    InspectorHeader {
        id: header
        width: parent.width
        title: qsTr("Properties")
        fileName: root.hasItem ? root.controller.fileName : qsTr("No item selected")
        iconPrefix: root.iconPrefix
        draggable: root.draggable
        dragObjectName: "imagePropertiesDragHeader"
        closeObjectName: "closePropertiesButton"
        closeToolTip: qsTr("Close properties")
        onCloseRequested: root.closeRequested()
        onDragStarted: function(position) { root.headerDragStarted(position) }
        onDragMoved: function(position) { root.headerDragMoved(position) }
    }
    InspectorTabs {
        id: tabs
        objectName: "propertiesTabs"
        y: header.height
        width: parent.width
        visible: root.hasItem && !root.controller.directory
        labels: [qsTr("Overview"), qsTr("Histogram"), qsTr("Advanced")]
        currentIndex: root.currentTab
        tabObjectNamePrefix: "imagePropertiesTab-"
        onSelected: function(index) { root.selectTab(index) }
    }
    Flickable {
        id: scroll
        objectName: "imagePropertiesScroll"
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        clip: true
        contentWidth: width
        contentHeight: body.implicitHeight + 32
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar { }
        Column {
            id: body
            x: 12; y: 16
            width: scroll.width - 24
            spacing: 12
            Text {
                visible: !root.hasItem
                width: parent.width
                text: root.selectionCount > 1
                    ? qsTr("%1 items selected. Select one item to view its properties.").arg(root.selectionCount)
                    : qsTr("Select an item to view its properties.")
                wrapMode: Text.Wrap
                color: Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: 13
            }
            Text {
                visible: root.hasItem && (root.controller.loading || root.controller.metadataLoading)
                width: parent.width
                text: root.controller && root.controller.loading ? qsTr("Loading image…") : qsTr("Reading metadata…")
                wrapMode: Text.Wrap
                color: Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: 12
            }
            Column {
                visible: root.hasItem && root.controller.errorText.length > 0
                width: parent.width
                spacing: 8
                Text {
                    width: parent.width
                    text: root.controller ? root.controller.errorText : ""
                    wrapMode: Text.WrapAnywhere
                    color: Theme.danger
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                }
                SettingsButton {
                    objectName: "retryPropertiesButton"
                    text: qsTr("Retry")
                    onClicked: root.controller.reload()
                }
            }
            SettingsButton {
                visible: root.hasItem && root.effectiveTab === 0 && root.controller.warnings.length > 0
                width: parent.width
                text: qsTr("Color or metadata warnings (%1)").arg(root.controller ? root.controller.warnings.length : 0)
                onClicked: root.selectTab(2)
            }
            Repeater {
                model: root.groups || []
                delegate: Column {
                    id: group
                    required property var modelData
                    objectName: "propertyGroup-" + modelData.id
                    width: body.width
                    spacing: 8
                    Text {
                        width: parent.width
                        text: group.modelData.title
                        color: Theme.graphiteInk
                        font.family: Theme.uiFont
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                    }
                    Rectangle {
                        width: parent.width
                        height: fields.implicitHeight
                        radius: Theme.radius
                        color: Theme.raisedSurface
                        border.color: Theme.opticalGray
                        PropertyFieldList {
                            id: fields
                            width: parent.width
                            fields: group.modelData.fields
                        }
                    }
                }
            }
            Text {
                visible: root.hasItem && root.effectiveTab === 0 && !root.controller.directory
                    && !root.controller.loading && !root.controller.metadataLoading
                    && root.controller.errorText.length === 0
                    && !root.groups.some(function(g) { return g.id === "capture" || g.id === "description" })
                width: parent.width
                text: qsTr("No capture or descriptive metadata is available.")
                wrapMode: Text.Wrap
                color: Theme.mutedInk
                font.family: Theme.uiFont
                font.pixelSize: 12
            }
            PropertiesHistogram {
                width: parent.width
                visible: root.hasItem && root.effectiveTab === 1
                height: visible ? implicitHeight : 0
                active: visible && root.inspectionActive
                controller: root.controller
            }
            Repeater {
                model: root.hasItem && root.effectiveTab === 2 ? root.controller.warnings : []
                delegate: Rectangle {
                    required property string modelData
                    width: body.width
                    height: warning.implicitHeight + 24
                    radius: Theme.radius
                    color: Theme.dangerSurface
                    border.color: Theme.dangerBorder
                    Text {
                        id: warning
                        x: 12; y: 12
                        width: parent.width - 24
                        text: parent.modelData
                        wrapMode: Text.WrapAnywhere
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                        color: Theme.danger
                    }
                }
            }

        }
    }
}
