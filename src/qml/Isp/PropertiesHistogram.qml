pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "."

Item {
    id: root
    objectName: "propertiesHistogram"

    property var controller: null
    property int source: 0
    property int selectedChannel: 0
    property var histogramData: {
        const revision = controller ? controller.histogramRevision : 0
        return controller ? controller.histogram(source) : ({})
    }
    readonly property var channels: histogramData.channels || []

    implicitHeight: 408

    function requestCurrentSource() {
        if (controller)
            controller.requestHistogram(source)
    }

    onSourceChanged: {
        selectedChannel = 0
        requestCurrentSource()
        graph.requestPaint()
    }
    onHistogramDataChanged: {
        if (selectedChannel >= channels.length)
            selectedChannel = 0
        graph.requestPaint()
    }
    Component.onCompleted: requestCurrentSource()

    Column {
        anchors.fill: parent
        spacing: 8

        Row {
            width: parent.width
            height: 26
            spacing: 6

            Repeater {
                model: ["Display RGB", "Source planes"]
                delegate: Button {
                    id: sourceButton
                    required property int index
                    required property string modelData
                    width: index === 0 ? 104 : 116
                    height: 26
                    text: modelData
                    enabled: index === 0 || (root.controller && root.controller.hasRawParameters)
                    onClicked: root.source = index
                    contentItem: Text {
                        text: sourceButton.text
                        color: !sourceButton.enabled ? Theme.faintInk
                              : root.source === sourceButton.index ? Theme.probeBlue : Theme.mutedInk
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: Theme.uiFont
                        font.pixelSize: 10
                        font.weight: root.source === sourceButton.index ? Font.DemiBold : Font.Medium
                    }
                    background: Rectangle {
                        radius: 5
                        color: root.source === sourceButton.index ? Theme.explorerSelectionBg
                              : sourceButton.hovered ? Theme.softHover : "transparent"
                        border.width: root.source === sourceButton.index ? 1 : 0
                        border.color: Theme.accentBorder
                    }
                }
            }

            Item { width: 8; height: 1 }

            Repeater {
                model: root.channels
                delegate: Button {
                    id: channelButton
                    required property int index
                    required property var modelData
                    height: 26
                    width: Math.max(48, channelName.implicitWidth + 22)
                    onClicked: {
                        root.selectedChannel = index
                        graph.requestPaint()
                    }
                    contentItem: Row {
                        anchors.centerIn: parent
                        spacing: 5
                        Rectangle {
                            width: 6
                            height: 6
                            radius: 3
                            anchors.verticalCenter: parent.verticalCenter
                            color: channelButton.modelData.color || Theme.mutedInk
                        }
                        Text {
                            id: channelName
                            text: channelButton.modelData.name || ""
                            color: root.selectedChannel === channelButton.index
                                   ? Theme.graphiteInk : Theme.mutedInk
                            font.family: Theme.uiFont
                            font.pixelSize: 10
                            font.weight: root.selectedChannel === channelButton.index
                                         ? Font.DemiBold : Font.Normal
                        }
                    }
                    background: Rectangle {
                        radius: 5
                        color: root.selectedChannel === channelButton.index ? Theme.softHover
                              : channelButton.hovered ? Theme.softHover : "transparent"
                    }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: 180
            radius: 6
            color: Theme.raisedSurface
            border.width: 1
            border.color: Theme.opticalGray
            clip: true

            Canvas {
                id: graph
                objectName: "propertiesHistogramGraph"
                anchors.fill: parent
                anchors.margins: 8
                antialiasing: true

                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const context = getContext("2d")
                    context.reset()
                    const left = 28
                    const top = 8
                    const right = width - 4
                    const bottom = height - 22

                    context.strokeStyle = Theme.opticalGray
                    context.lineWidth = 1
                    for (let division = 0; division <= 4; ++division) {
                        const y = top + (bottom - top) * division / 4
                        context.beginPath()
                        context.moveTo(left, Math.round(y) + 0.5)
                        context.lineTo(right, Math.round(y) + 0.5)
                        context.stroke()
                    }
                    context.fillStyle = Theme.mutedInk
                    context.font = "9px " + Theme.monoFont
                    context.textAlign = "center"
                    const domain = Number(root.histogramData.maximumValue || 255)
                    for (let tick = 0; tick <= 4; ++tick) {
                        const x = left + (right - left) * tick / 4
                        context.fillText(String(Math.round(domain * tick / 4)), x, height - 5)
                    }

                    if (root.histogramData.loading === true) {
                        context.fillStyle = Theme.mutedInk
                        context.font = "11px " + Theme.uiFont
                        context.fillText("Analyzing…", (left + right) / 2, (top + bottom) / 2)
                        return
                    }
                    const channel = root.channels.length > root.selectedChannel
                                  ? root.channels[root.selectedChannel] : null
                    if (!channel || !channel.bins || channel.bins.length < 2) {
                        context.fillStyle = Theme.faintInk
                        context.font = "11px " + Theme.uiFont
                        context.fillText(root.histogramData.message || "No histogram",
                                         (left + right) / 2, (top + bottom) / 2)
                        return
                    }
                    const samples = Math.max(2, Math.min(channel.bins.length,
                                                         Math.floor(right - left)))
                    const values = []
                    let maximum = 0
                    for (let point = 0; point < samples; ++point) {
                        const start = Math.floor(point * channel.bins.length / samples)
                        const end = Math.max(start + 1,
                                             Math.floor((point + 1) * channel.bins.length / samples))
                        let count = 0
                        for (let bin = start; bin < end; ++bin)
                            count += Number(channel.bins[bin])
                        values.push(count)
                        maximum = Math.max(maximum, count)
                    }
                    if (maximum <= 0)
                        return
                    context.beginPath()
                    context.moveTo(left, bottom)
                    for (let point = 0; point < samples; ++point) {
                        const x = left + point * (right - left) / (samples - 1)
                        const y = bottom - (bottom - top) * values[point] / maximum
                        context.lineTo(x, y)
                    }
                    context.lineTo(right, bottom)
                    context.closePath()
                    const color = channel.color || Theme.mutedInk
                    context.fillStyle = color
                    context.fill()
                    context.strokeStyle = color
                    context.lineWidth = 1.25
                    context.stroke()
                }
            }
        }

        Text {
            width: parent.width
            height: 18
            text: root.histogramData.summary || root.histogramData.message || ""
            color: Theme.mutedInk
            font.family: Theme.monoFont
            font.pixelSize: 10
            elide: Text.ElideRight
        }

        Rectangle {
            width: parent.width
            height: 22 + Math.max(1, root.channels.length) * 24
            radius: 6
            color: Theme.raisedSurface
            border.width: 1
            border.color: Theme.opticalGray
            clip: true

            Column {
                anchors.fill: parent
                Rectangle {
                    width: parent.width
                    height: 22
                    color: Theme.softHover
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: ["Channel", "Mean", "Variance", "Min", "Max", "Median"]
                            delegate: Text {
                                required property string modelData
                                width: parent.width / 6
                                height: parent.height
                                text: modelData
                                color: Theme.mutedInk
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.family: Theme.uiFont
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
                Repeater {
                    model: root.channels
                    delegate: Rectangle {
                        id: channelRow
                        required property int index
                        required property var modelData
                        width: parent.width
                        height: 24
                        color: index % 2 === 0 ? Theme.raisedSurface : Theme.softHover
                        Row {
                            anchors.fill: parent
                            Repeater {
                                model: [channelRow.modelData.name || "",
                                    Number(channelRow.modelData.mean || 0).toFixed(2),
                                    Number(channelRow.modelData.variance || 0).toFixed(2),
                                    String(channelRow.modelData.min ?? ""),
                                    String(channelRow.modelData.max ?? ""),
                                    Number(channelRow.modelData.median || 0).toFixed(1)]
                                delegate: Text {
                                    required property string modelData
                                    width: parent.width / 6
                                    height: parent.height
                                    text: modelData
                                    color: Theme.graphiteInk
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.family: Theme.monoFont
                                    font.pixelSize: 9
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
