import QtQuick

Rectangle {
    id: root

    required property var controller
    property int slot: 0
    property var histogramData: {
        const revision = root.controller.histogramRevision
        return root.controller.histogram(root.slot)
    }

    implicitWidth: 156
    implicitHeight: 64
    radius: 3
    color: Theme.inspectionOverlayMuted
    border.width: 1
    border.color: Theme.inspectionOverlayBorder
    clip: true

    function request() {
        root.controller.requestHistogram(root.slot)
    }

    onHistogramDataChanged: graph.requestPaint()

    Canvas {
        id: graph
        anchors.fill: parent
        anchors.margins: 5
        antialiasing: true

        onPaint: {
            const context = getContext("2d")
            context.reset()

            const channels = root.histogramData.channels || []
            const channel = channels.length > 0 ? channels[0] : null
            if (!channel || !channel.bins || channel.bins.length === 0)
                return

            const samples = Math.max(2, Math.min(channel.bins.length, Math.floor(width)))
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

            const bottom = height - 1
            context.beginPath()
            context.moveTo(0, bottom)
            for (let point = 0; point < samples; ++point) {
                const x = point * width / (samples - 1)
                const y = bottom - (height - 3) * values[point] / maximum
                context.lineTo(x, y)
            }
            context.lineTo(width, bottom)
            context.closePath()
            context.fillStyle = channel.color || "#F4F5F2"
            context.fill()
            context.strokeStyle = "#E8F4F5F2"
            context.lineWidth = 1
            context.stroke()
        }
    }
}
