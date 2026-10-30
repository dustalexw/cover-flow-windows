pragma Singleton
import QtQuick

// Windows ships Segoe Fluent Icons (11) / Segoe MDL2 Assets (10) with the system;
// other platforms fall back to Unicode symbols.
QtObject {
    readonly property bool win: Qt.platform.os === "windows"
    readonly property string family: win ? (Qt.fontFamilies().indexOf("Segoe Fluent Icons") >= 0 ? "Segoe Fluent Icons" : "Segoe MDL2 Assets") : ""
    readonly property string play: win ? "" : "▶"
    readonly property string pause: win ? "" : "⏸"
    readonly property string previous: win ? "" : "⏮"
    readonly property string next: win ? "" : "⏭"
    readonly property string shuffle: win ? "" : "⤨"
    readonly property string repeatAll: win ? "" : "↻"
    readonly property string repeatOne: win ? "" : "①"
    readonly property string volumeMute: win ? "" : "🔇"
    readonly property string volumeLow: win ? "" : "🔈"
    readonly property string volumeMid: win ? "" : "🔉"
    readonly property string volumeHigh: win ? "" : "🔊"
    readonly property string lyrics: win ? "\uE8D6" : "♪"
    readonly property string queue: win ? "" : "☰"
    readonly property string search: win ? "" : "⌕"
    readonly property string more: win ? "" : "…"
    readonly property string close: win ? "" : "✕"
    readonly property string album: win ? "" : "◉"
    readonly property string pin: win ? "" : "📌"
    readonly property string unpin: win ? "" : "📍"
    readonly property string eye: win ? "" : "◎"
    readonly property string reload: win ? "" : "⟳"
    readonly property string coverFlow: win ? "\uE8B9" : "▭"
    readonly property string grid: win ? "" : "▦"
    readonly property string warning: win ? "" : "⚠"
    readonly property string reveal: win ? "" : "↗"
    readonly property string disc: win ? "" : "◎"
}
