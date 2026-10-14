import QtQuick

QtObject {
    property string language: "zh_CN"
    property string status: "active"
    property bool crashActive: true
    property string directory: "/Library/Application Support/MVP Image Viewer/diagnostics"
    property real diskUsage: 2202009
    property string retentionSummary: language === "en" ? "Logs: 7 days / 100 MiB. Crash reports: 30 days / 10 reports / 200 MiB." : "日志：7 天 / 100 MiB。崩溃报告：30 天 / 10 份 / 200 MiB。运行中的会话受保护。"
    property var recentReports: []
    property bool busy: false
    property int progress: 42
    property string error: ""
    property string exportedPath: ""
    function openDirectory() { }
    function clearHistory() { return "" }
    function exportLogs(destination, days, dumps) { busy = true }
    function cancelExport() { busy = false }
    function openExportDirectory() { }
}
