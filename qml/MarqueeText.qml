import QtQuick
import QtQuick.Controls

// ScrollingTrackTitle.swift: static when it fits, otherwise scrolls at 28 px/s with
// a 2 s pause at each end. Hovering shows the full title.
Item {
    id: root
    property alias text: label.text
    property alias font: label.font
    property alias color: label.color
    property int horizontalAlignment: Text.AlignLeft
    implicitHeight: label.implicitHeight
    clip: true

    readonly property real overflow: label.implicitWidth - width
    readonly property bool scrolling: overflow > 1 && !Theme.reduceMotion

    Text {
        id: label
        width: root.scrolling ? implicitWidth : root.width
        elide: root.scrolling ? Text.ElideNone : Text.ElideRight
        horizontalAlignment: root.scrolling ? Text.AlignLeft : root.horizontalAlignment
        textFormat: Text.PlainText
        maximumLineCount: 1
        x: 0
    }

    SequentialAnimation {
        id: cycle
        running: root.scrolling && root.visible
        loops: Animation.Infinite
        PropertyAction { target: label; property: "x"; value: 0 }
        PauseAnimation { duration: 2000 }
        NumberAnimation { target: label; property: "x"; to: -root.overflow; duration: Math.max(1, root.overflow / 28 * 1000) }
        PauseAnimation { duration: 2000 }
        NumberAnimation { target: label; property: "x"; to: 0; duration: Math.max(1, root.overflow / 28 * 1000) }
        onRunningChanged: if (!running) label.x = 0
    }
    // Restart when the title or width changes.
    onOverflowChanged: if (cycle.running) cycle.restart()

    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered && overflow > 1
    ToolTip.text: label.text
    ToolTip.delay: 0
}
