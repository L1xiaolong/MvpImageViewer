import QtQuick

QtObject {
    property string language: "system"
    property string theme: "system"
    property bool restoreLastDirectory: true
    property bool confirmTrash: true
    property bool automaticUpdateChecks: true
    property bool applyEmbeddedColorProfiles: true
    property bool preserveHighBitDepth: true
    property string displayColorSpace: "auto"
    property string resolvedDisplayColorSpace: "Display P3"
    property bool honorExifOrientation: true
    property string canvasBackground: "neutral"
    property bool smoothDisplay: true
    property bool colorManagementAvailable: true
    property string applicationVersion: "0.2.17"
    property bool loggingEnabled: true
    property string logLevel: "Info"
    property bool crashReportingEnabled: true
    property string updateState: "idle"
    property string latestVersion: "0.3.0"
    property int updateDownloadProgress: 42
    property string updateError: ""
    property url releaseUrl: ""
    property int shortcutsRevision: 0
    property var shortcutEntries: [
        { id: "openFolder", sequence: "Ctrl+O", nativeSequence: "⌘O" },
        { id: "find", sequence: "Ctrl+F", nativeSequence: "⌘F" },
        { id: "settings", sequence: "Ctrl+,", nativeSequence: "⌘," },
        { id: "toggleNavigator", sequence: "Ctrl+B", nativeSequence: "⌘B" },
        { id: "compare", sequence: "C", nativeSequence: "C" },
        { id: "rename", sequence: "F2", nativeSequence: "F2" },
        { id: "newFolder", sequence: "Ctrl+Shift+N", nativeSequence: "⇧⌘N" }
    ]
    property int deleteCalls: 0
    function deleteSoftwareCache() { deleteCalls++; return "" }
    function setShortcut(action, sequence) { return "find" }
    function checkForUpdates() { updateState = "checking" }
    function downloadUpdate() { updateState = "downloading" }
    function cancelUpdateDownload() { updateState = "available" }
    function installUpdate() { updateState = "installing" }
    function openReleasePage() { }
    function openRepository() { }
    function restoreDefaults() { }
    function resetShortcuts() { }
}
