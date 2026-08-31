pragma Singleton
import QtQuick

QtObject {
    property bool darkMode: false
    property string canvasBackgroundMode: "neutral"

    // VS Code 1.19-era Dark workbench hierarchy (late 2017).
    readonly property color sensorWhite: darkMode ? "#1E1E1E" : "#F4F5F2"
    readonly property color paperWhite: darkMode ? "#252526" : "#FCFCFA"
    readonly property color raisedSurface: darkMode ? "#3C3C3C" : "#FFFFFF"
    readonly property color opticalGray: darkMode ? "#3F3F46" : "#DDE1E3"
    readonly property color softHover: darkMode ? "#2A2D2E" : "#EDF0F0"
    readonly property color pressedSurface: darkMode ? "#37373D" : "#E2E9ED"
    readonly property color graphiteInk: darkMode ? "#F0F0F0" : "#25303A"
    readonly property color mutedInk: darkMode ? "#C8C8C8" : "#69747D"
    readonly property color faintInk: darkMode ? "#9D9D9D" : "#9AA5AD"
    readonly property color probeBlue: darkMode ? "#78A5FF" : "#356AE6"
    readonly property color exposureAmber: darkMode ? "#E0A357" : "#B87524"
    readonly property color danger: darkMode ? "#F17A7A" : "#C94E4E"
    readonly property color dangerSurface: darkMode ? "#432A2D" : "#FBEDED"
    readonly property color dangerBorder: darkMode ? "#724248" : "#E5BABA"
    readonly property color success: darkMode ? "#85C79C" : "#3E7452"
    readonly property color successSurface: darkMode ? "#243A2C" : "#EAF5EE"
    readonly property color successBorder: darkMode ? "#3E674C" : "#BCD9C6"
    readonly property color accentBorder: darkMode ? "#59758A" : "#B7C6D0"
    readonly property color primaryButton: darkMode ? "#7896AA" : "#526F82"
    readonly property color primaryButtonHover: darkMode ? "#86A6BB" : "#607E92"
    readonly property color primaryButtonText: darkMode ? "#172027" : "#FFFFFF"
    readonly property color overlay: darkMode ? "#73000000" : "#660E1820"
    readonly property color searchFieldSurface: darkMode ? "#252526" : "#FFFFFF"
    readonly property color searchFieldBorder: darkMode ? "#6A6A6A" : "#C5CCD1"
    readonly property color searchFieldHoverBorder: darkMode ? "#858585" : "#95A3AC"
    readonly property color searchFieldFocusBorder: darkMode ? "#007ACC" : "#356AE6"
    readonly property color canvasBackground:
        canvasBackgroundMode === "dark" ? "#2D2D30"
        : canvasBackgroundMode === "black" ? "#000000"
        : canvasBackgroundMode === "white" ? "#FFFFFF"
        : "#A0A0A0"

    // Image inspection overlays sit on top of arbitrary pixels, so they intentionally
    // remain theme-independent and use a high-contrast photographic HUD palette.
    readonly property color inspectionOverlay: "#E61B1F23"
    readonly property color inspectionOverlayMuted: "#D91B1F23"
    readonly property color inspectionOverlayBorder: "#66FFFFFF"
    readonly property color inspectionText: "#FFFFFFFF"
    readonly property color inspectionMutedText: "#DDE7EBEE"
    readonly property color inspectionAccentText: "#FF9BCBFF"

    // Compact file-type badges used by image metadata rows.
    readonly property color encodedBadgeSurface: darkMode ? "#22364D" : "#E8F1FC"
    readonly property color encodedBadgeBorder: darkMode ? "#36587A" : "#C5D9F1"
    readonly property color encodedBadgeText: darkMode ? "#A9CFF4" : "#315F91"
    readonly property color yuvBadgeSurface: darkMode ? "#203C39" : "#E5F5F2"
    readonly property color yuvBadgeBorder: darkMode ? "#35605B" : "#B9DED7"
    readonly property color yuvBadgeText: darkMode ? "#8BD6CA" : "#267266"
    readonly property color rawBadgeSurface: darkMode ? "#453320" : "#FFF1DE"
    readonly property color rawBadgeBorder: darkMode ? "#6D4F2D" : "#EDCFA4"
    readonly property color rawBadgeText: darkMode ? "#F3BE78" : "#955817"

    // VS Code-style file tree
    readonly property color explorerSelectionBg: darkMode ? "#094771" : "#E4E6F1"
    readonly property color explorerHoverBg: softHover
    readonly property color explorerIndentGuide: darkMode ? "#404040" : "#D5D9DC"
    readonly property string iconPrefix: darkMode ? "qrc:/icons/ui-dark/" : "qrc:/icons/ui/"
    readonly property real disabledOpacity: darkMode ? 0.52 : 0.38
    readonly property int metadataFontSize: darkMode ? 11 : 10

    readonly property int unit: 4
    readonly property int toolbarHeight: 38
    readonly property int sidebarWidth: 272
    readonly property int radius: 4
    readonly property int iconSize: 20
    readonly property int touchTarget: 32
    readonly property int fast: 120
    readonly property int normal: 180
    // The application provides the platform's current UI and fixed-width fonts.
    // Empty fallbacks keep standalone QML tooling and tests on their own defaults.
    readonly property string uiFont:
        typeof systemUiFontFamily !== "undefined" ? systemUiFontFamily : ""
    readonly property string monoFont:
        typeof systemFixedFontFamily !== "undefined" ? systemFixedFontFamily : ""
}
