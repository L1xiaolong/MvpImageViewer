import QtQuick
QtObject {
    id: root
    property string path: "/Images/Demo/sample_0001.jpg"
    property string fileName: "sample_0001.jpg"
    property bool directory: false
    property bool loading: false
    property bool metadataLoading: false
    property string errorText: ""
    property var warnings: []
    property bool hasHistogramSource: true
    property int histogramRevision: 1
    property int histogramRequests: 0
    property int lastHistogramSource: -1
    property int loadRequests: 0
    property string scenario: "jpeg"
    function row(id, label, value, mono) {
        return { id: id, label: qsTranslate("ImageProperties", label), value: value, mono: !!mono }
    }
    function group(id, title, fields) {
        return { id: id, title: qsTranslate("ImageProperties", title), fields: fields }
    }
    property var overviewGroups: !path ? [] : directory ? [group("file", "Folder", [
        row("folder-name", "Name", fileName), row("folder-type", "Type", qsTranslate("ImageProperties", "Folder")),
        row("path", "Full path", path), row("modified", "Modified", "2026-07-18 16:42")
    ])] : [group("image", "Image", [
        row("format", "Format", scenario === "raw" ? "MIPI RAW10" : scenario === "yuv" ? "P010" : "JPEG"),
        row("dimensions", "Dimensions", "4000 × 3000 px", true),
        row("bit-depth", "Bit depth", qsTranslate("ImageProperties", "%1-bit valid / %2-bit storage").arg(10).arg(16), true),
        row("source-color-space", "Source color space", "sRGB"),
        row("display-color-space", "Display color space", "Display P3")
    ]), group("file", "File", [
        row("file-size", "File size", "13.89 MiB", true), row("path", "Full path", path),
        row("modified", "Modified", "2026-07-18 16:42")
    ])].concat(scenario === "jpeg" ? [group("capture", "Capture", [
        row("camera", "Camera", "Demo Reference Camera"), row("lens", "Lens", "Engineering reference lens"),
        row("exposure", "Exposure time", "1/320 s", true), row("aperture", "Aperture", "f/2.8", true),
        row("iso", "ISO", "100", true), row("focal-length", "Focal length", "24 mm", true),
        row("captured", "Captured at", "2026-07-18 16:41")
    ])] : [])
    property var advancedGroups: !path || directory ? [] : [group("color", "Color", [
        row("source-primaries", "Source primaries", "sRGB / BT.709"),
        row("source-transfer", "Source transfer", "sRGB"), row("display-transfer", "Display transfer", "sRGB"),
        row("source-range", "Source range", qsTranslate("ImageProperties", "Full"))
    ]), group("icc", "ICC profile", [row("icc-fingerprint", "Profile fingerprint", "1cce339824ade116eb9a2c9eb9d066a432ff48505fc470c18488d4bd3fe12db76", true)])]
    .concat(scenario === "raw" || scenario === "yuv" ? [group("raw", "RAW / YUV parameters", [
        row("row-stride", "Row stride (bytes; 0 = auto)", "7800", true),
        row("header-offset", "Header offset (bytes)", "0", true),
        { id: "color-matrix", label: qsTranslate("ImageProperties", "Color correction matrix"),
          value: "1.1, -0.1, 0 | -0.04, 1.08, -0.04 | 0, -0.12, 1.12", mono: true,
          matrix: ["1.1", "-0.1", "0", "-0.04", "1.08", "-0.04", "0", "-0.12", "1.12"] }
    ])] : [])
    property var displayData: ({
        "valid": true,
        "loading": false,
        "maximumValue": 255,
        "analysisWidth": 1920, "analysisHeight": 1440, "sampleCount": 262144, "sourceWidth": 4000, "sourceHeight": 3000,
        "channels": [
            { "id": "luma", "name": "Luma", "color": "#52616B",
              "bins": [2, 4, 7, 12, 20, 35, 58, 85, 118, 146, 160, 154, 132, 101, 72, 48, 31, 20, 12, 7, 3],
              "mean": 112.42, "variance": 924.18, "min": 3, "max": 251, "median": 109.0 },
            { "id": "red", "name": "Red", "color": "#D65A5A",
              "bins": [1, 3, 8, 19, 39, 68, 99, 124, 138, 132, 111, 87, 68, 54, 42, 29, 18, 10, 5, 2],
              "mean": 118.37, "variance": 1012.30, "min": 2, "max": 255, "median": 116.0 },
            { "id": "green", "name": "Green", "color": "#4E9D69",
              "bins": [1, 4, 11, 25, 52, 88, 127, 154, 165, 158, 137, 106, 78, 55, 35, 21, 12, 7, 3, 1],
              "mean": 110.14, "variance": 862.44, "min": 4, "max": 253, "median": 108.0 },
            { "id": "blue", "name": "Blue", "color": "#527BC9",
              "bins": [3, 7, 15, 29, 50, 75, 102, 123, 131, 125, 109, 91, 72, 53, 37, 24, 14, 8, 4, 2],
              "mean": 106.82, "variance": 1094.26, "min": 1, "max": 250, "median": 104.0 }
        ]
    })
    function nativeChannel(id, name, channel, samples) {
        return Object.assign({}, channel, { id: id, name: name, sampleCount: samples })
    }
    property var sourceData: ({
        "valid": true, "loading": false,
        "maximumValue": scenario === "raw" || scenario === "yuv" ? 1023 : scenario === "high" ? 65535 : 255,
        "domain": scenario === "raw" ? "Bayer" : scenario === "yuv" ? "YUV" : scenario === "gray" ? "Gray" : "RGB",
        "validBits": scenario === "raw" || scenario === "yuv" ? 10 : scenario === "high" ? 16 : 8,
        "sourceWidth": 4000, "sourceHeight": 3000,
        "channels": scenario === "raw" ? [
            nativeChannel("r", "R", displayData.channels[1], 65536),
            nativeChannel("gr", "Gr", displayData.channels[2], 65536),
            nativeChannel("gb", "Gb", displayData.channels[2], 65536),
            nativeChannel("b", "B", displayData.channels[3], 65536)
        ] : scenario === "yuv" ? [
            nativeChannel("y", "Y", displayData.channels[0], 262144),
            nativeChannel("u", "U", displayData.channels[2], 65536),
            nativeChannel("v", "V", displayData.channels[3], 65536)
        ] : displayData.channels.map(function(channel) {
              return nativeChannel(channel.id, channel.name, channel, 262144)
          })
    })


    function loadPath(requestedPath, deferred) {
        path = requestedPath
        fileName = requestedPath.split(/[\\/]/).pop()
        loadRequests += 1
        histogramRevision += 1
    }
    function clear() { path = ""; loading = false; metadataLoading = false; errorText = ""; histogramRevision += 1 }
    function reload() { if (path) loadPath(path, false) }
    function retranslate() { }
    function requestHistogram(source) { lastHistogramSource = source; histogramRequests += 1; histogramRevision += 1 }
    function histogram(source) { return source === 1 ? sourceData : displayData }
}
