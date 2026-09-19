import QtQuick

QtObject {
    id: root

    signal stateChanged()
    signal filesystemChanged()
    signal closeRequested()

    property var paths: [
        "/Images/Demo/sample_0001.jpg",
        "/Images/Demo/sample_0002.jpg",
        "/Images/Demo/sample_0003.jpg"
    ]
    property int currentIndex: 0
    readonly property string currentPath: paths.length > 0 ? paths[currentIndex] : ""
    readonly property string fileName: currentPath.split(/[\\/]/).pop()
    readonly property string fileType: "JPEG"
    readonly property string fileSizeText: "13.9MB"
    readonly property size imageSize: Qt.size(4000, 3000)
    readonly property string positionText: paths.length > 0
                                           ? (currentIndex + 1) + " / " + paths.length : ""
    readonly property bool canGoPrevious: currentIndex > 0
    readonly property bool canGoNext: currentIndex + 1 < paths.length
    property bool loading: false
    property string errorText: ""
    property int fitRequestCount: 0
    property int actualPixelsRequestCount: 0

    function open(requestedPaths, initialIndex) {
        if (requestedPaths && requestedPaths.length > 0)
            paths = requestedPaths
        currentIndex = Math.max(0, Math.min(initialIndex, paths.length - 1))
        stateChanged()
    }
    function attachCanvas(canvas) {}
    function showPrevious() {
        if (canGoPrevious) {
            currentIndex -= 1
            stateChanged()
        }
    }
    function showNext() {
        if (canGoNext) {
            currentIndex += 1
            stateChanged()
        }
    }
    function fitImage() { fitRequestCount += 1 }
    function actualPixels() { actualPixelsRequestCount += 1 }
    function copyCurrent(cut) {}
    function renameCurrentTo(name) { return "" }
    function moveCurrentToTrash() { return "" }
    function revealCurrent() { return "" }
}
