pragma Singleton
import QtQuick

// Support.swift palette. theme: 0 dark, 1 light, 2 match system.
QtObject {
    readonly property bool dark: app.theme === 0 || (app.theme === 2 && Application.styleHints.colorScheme !== Qt.Light)
    readonly property color base: dark ? "#101012" : Qt.rgba(0.955, 0.945, 0.925, 1)
    readonly property color bar: dark ? "#18181b" : "#ebe8e2"
    readonly property color panel: dark ? "#1c1c20" : "#f7f5f1"
    readonly property color text: dark ? "#f2f2f2" : "#1a1a1a"
    readonly property color secondary: dark ? "#a0a0a6" : "#5c5c60"
    readonly property color hairline: dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.12)
    readonly property color accent: dark ? Qt.rgba(0.95, 0.77, 0.49, 1) : Qt.rgba(0.52, 0.34, 0.09, 1)
    readonly property color fieldBg: dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06)
    readonly property color hover: dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.06)
    readonly property color danger: "#e5534b"
    readonly property string serif: Qt.platform.os === "windows" ? "Georgia" : "serif"
    readonly property string sans: Qt.platform.os === "windows" ? "Segoe UI" : "sans-serif"
    readonly property bool reduceMotion: false

    function formatTime(s) { return app.formatTime(s) }
}
