import QtQuick

QtObject {
    id: root

    property string path: "/Images/Demo/sample_0001.jpg"
    property string fileName: "sample_0001.jpg"
    property bool directory: false
    property bool loading: false
    property string errorText: ""
    property bool hasRawParameters: true
    property int histogramRevision: 1
    property var basicFields: [
        { "label": "File Name", "value": "sample_0001.jpg" },
        { "label": "Location", "value": "/Images/Demo/sample_0001.jpg" },
        { "label": "Type", "value": "JPEG" },
        { "label": "File Size", "value": "13.89 MiB" },
        { "label": "Date / Time", "value": "2026-07-18T16:42:08" },
        { "label": "Dimensions", "value": "4000 × 3000" },
        { "label": "Bit Depth", "value": "8-bit valid in 8-bit storage" }
    ]
    property var exifFields: [
        { "label": "Make", "value": "Demo" },
        { "label": "Model", "value": "Reference Camera" },
        { "label": "Software", "value": "Demo Capture" },
        { "label": "Captured At", "value": "2026-07-18T16:41:52" },
        { "label": "Exposure Time", "value": "1/320 s" },
        { "label": "Aperture", "value": "f/2.8" },
        { "label": "ISO", "value": "100" },
        { "label": "Exposure Program", "value": "Manual" },
        { "label": "Metering Mode", "value": "Center weighted" },
        { "label": "Exposure Compensation", "value": "0 EV" },
        { "label": "Flash", "value": "Off" },
        { "label": "Focal Length", "value": "24 mm" },
        { "label": "Lens", "value": "Engineering reference lens" },
        { "label": "GPS", "value": "" },
        { "label": "Sensor Size", "value": "6236 × 4178" },
        { "label": "Orientation", "value": "Normal" },
        { "label": "Metadata Warning", "value": "" }
    ]
    property var rawFields: [
        { "label": "Format", "value": "MIPI RAW10" },
        { "label": "Dimensions", "value": "6236 × 4178" },
        { "label": "Header Offset", "value": "0" },
        { "label": "Row Stride", "value": "7800" },
        { "label": "Orientation", "value": "Normal" },
        { "label": "Valid Bits", "value": "10" },
        { "label": "Bayer Pattern", "value": "RGGB" },
        { "label": "Demosaic", "value": "Yes" },
        { "label": "Black Level", "value": "64" },
        { "label": "White Level", "value": "1023" },
        { "label": "White Balance", "value": "R 1.82  G 1.0  B 1.54" },
        { "label": "Color Matrix", "value": "1.1, -0.1, 0  |  -0.04, 1.08, -0.04  |  0, -0.12, 1.12" },
        { "label": "Display Gamma", "value": "2.2" }
    ]
    property var displayData: ({
        "valid": true,
        "loading": false,
        "maximumValue": 255,
        "summary": "1920 × 1440 analysis · 262,144 samples · source 4000 × 3000",
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
    property var sourceData: ({
        "valid": true, "loading": false, "maximumValue": 1023,
        "summary": "Source Bayer · 10-bit · 6236 × 4178",
        "channels": displayData.channels
    })

    function loadPath(requestedPath) {
        path = requestedPath
        fileName = requestedPath.split(/[\\/]/).pop()
    }
    function requestHistogram(source) {
        histogramRevision += 1
    }
    function histogram(source) {
        return source === 1 ? sourceData : displayData
    }
}
