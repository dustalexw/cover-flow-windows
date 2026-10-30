import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Elapsed · slider · remaining. The slider keeps its own value while dragging and
// seeks on release.
RowLayout {
    id: root
    property color textColor: Theme.secondary
    property color trackColor: Theme.hairline
    property color fillColor: Theme.text
    spacing: 8
    readonly property real duration: player.duration

    Label {
        text: Theme.formatTime(slider.pressed ? slider.value : player.position)
        color: root.textColor
        font.pixelSize: 11
        horizontalAlignment: Text.AlignRight
        Layout.preferredWidth: 42
    }
    Slider {
        id: slider
        Layout.fillWidth: true
        from: 0
        to: Math.max(0.01, root.duration)
        enabled: player.hasTrack
        value: pressed ? value : player.position
        onPressedChanged: if (!pressed) player.seek(value)
        Accessible.name: "Playback position"
        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: root.trackColor
            Rectangle { width: slider.visualPosition * parent.width; height: parent.height; radius: 2; color: root.fillColor }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 12; height: 12; radius: 6
            color: root.fillColor
            visible: slider.hovered || slider.pressed
        }
    }
    Label {
        text: "-" + Theme.formatTime(Math.max(0, root.duration - (slider.pressed ? slider.value : player.position)))
        color: root.textColor
        font.pixelSize: 11
        Layout.preferredWidth: 42
    }
}
