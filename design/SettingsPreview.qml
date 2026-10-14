import QtQuick
import "../src/qml/Mvp"

Item {
    id: root
    property alias card: settings
    property alias mockSettings: controller
    property alias mockDiagnostics: diagnosticsMock
    property var settingsController: controller
    property bool darkMode: settingsController.theme === "dark"
    width: 980
    height: 640
    Binding { target: Theme; property: "darkMode"; value: root.darkMode }
    Binding { target: Theme; property: "canvasBackgroundMode"; value: root.settingsController.canvasBackground }
    MockSettingsController { id: controller }
    MockDiagnosticsController { id: diagnosticsMock; language: root.settingsController.language }
    Rectangle { anchors.fill: parent; color: Theme.sensorWhite }
    SettingsCard {
        id: settings
        parent: root
        settingsController: root.settingsController
        diagnostics: diagnosticsMock
    }
}
