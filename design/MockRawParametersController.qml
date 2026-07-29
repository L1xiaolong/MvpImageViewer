import QtQuick

QtObject {
    id: root

    signal stateChanged()
    signal parametersApplied(string path)
    signal notificationRequested(string message, bool error)

    property string path: "/Images/Demo/sample_0010.raw"
    property string fileName: "sample_0010.raw"
    property var values: ({
        "format": 4, "width": 6236, "height": 4178,
        "rowStride": 7800, "chromaStride": 0, "headerOffset": 0,
        "validBits": 0, "bayerPattern": 0, "yuvMatrix": 1, "range": 1,
        "orientation": 0, "littleEndian": true, "msbAligned": false,
        "demosaic": true, "blackLevel": 64, "whiteLevel": 1023,
        "whiteBalance": [1.82, 1.0, 1.54],
        "colorMatrix": [1.1, -0.1, 0, -0.04, 1.08, -0.04, 0, -0.12, 1.12],
        "displayGamma": 2.2
    })
    property var presetNames: ["6236_4178_MIPI_RAW10", "Reference daylight"]
    property string selectedPreset: ""
    readonly property bool yuvFormat: Number(values.format) <= 3
    readonly property bool raw16Format: Number(values.format) === 6
    readonly property bool endianControlsVisible: Number(values.format) === 3 || raw16Format
    readonly property string suggestedPresetName: values.width + "_" + values.height + "_MIPI_RAW10"

    function loadPath(requestedPath) {
        path = requestedPath
        fileName = requestedPath.split(/[\\/]/).pop()
        stateChanged()
    }
    function setValue(key, value) {
        const copy = Object.assign({}, values)
        copy[key] = value
        values = copy
        selectedPreset = ""
        stateChanged()
    }
    function setListValue(key, index, value) {
        const copy = values[key].slice()
        copy[index] = value
        setValue(key, copy)
    }
    function selectPreset(name) { selectedPreset = name; stateChanged() }
    function savePreset(name, applyAfter) {
        if (name.trim().length === 0) return "Enter a configuration name."
        selectedPreset = name.trim()
        if (presetNames.indexOf(selectedPreset) < 0)
            presetNames = presetNames.concat([selectedPreset])
        notificationRequested("Configuration saved: " + selectedPreset, false)
        return ""
    }
    function deleteSelectedPreset() { selectedPreset = ""; return "" }
    function applyToFolder() {
        notificationRequested("Configuration applied to 8 RAW file(s) in this folder", false)
        return ""
    }
}
