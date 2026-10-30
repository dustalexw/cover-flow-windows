import QtQuick

// Animated equalizer bars for the playing album/track.
Row {
    id: root
    property bool animating: true
    property color color: Theme.accent
    property real barHeight: 12
    spacing: Math.max(1, barHeight * 0.12)
    height: barHeight
    Repeater {
        model: 3
        Rectangle {
            required property int index
            width: Math.max(2, root.barHeight * 0.2)
            radius: width / 2
            color: root.color
            anchors.bottom: parent.bottom
            height: root.barHeight * 0.4
            SequentialAnimation on height {
                running: root.animating
                loops: Animation.Infinite
                NumberAnimation { to: root.barHeight * (0.9 - index * 0.15); duration: 260 + index * 90; easing.type: Easing.InOutSine }
                NumberAnimation { to: root.barHeight * 0.3; duration: 300 + index * 70; easing.type: Easing.InOutSine }
            }
        }
    }
}
