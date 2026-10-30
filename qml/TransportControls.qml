import QtQuick
import QtQuick.Controls

// Shuffle · Previous · Play/Pause · Next · Repeat. Icons are drawn with text glyphs
// from Segoe Fluent/MDL2 on Windows and Unicode fallbacks elsewhere.
Row {
    id: root
    property real size: 32
    property bool showModes: true
    property color color: Theme.text
    property color accent: Theme.accent
    spacing: size * 0.35

    component IconButton: AbstractButton {
        id: b
        property string glyph
        property real glyphSize: root.size * 0.5
        property color tint: root.color
        property bool prominent: false
        width: root.size; height: root.size
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        contentItem: Text {
            text: b.glyph
            font.family: Glyphs.family
            font.pixelSize: b.glyphSize
            color: b.prominent ? Theme.base : b.tint
            opacity: b.enabled ? (b.down ? 0.6 : 1) : 0.35
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: width / 2
            color: b.prominent ? root.color : (b.hovered ? Theme.hover : "transparent")
        }
        ToolTip.visible: hovered && Accessible.name !== ""
        ToolTip.text: Accessible.name
        ToolTip.delay: 600
    }

    IconButton {
        visible: root.showModes
        glyph: Glyphs.shuffle
        tint: player.shuffle ? root.accent : root.color
        onClicked: player.shuffle = !player.shuffle
        Accessible.name: player.shuffle ? "Shuffle On" : "Shuffle Off"
        anchors.verticalCenter: parent.verticalCenter
    }
    IconButton {
        glyph: Glyphs.previous
        enabled: player.hasTrack
        onClicked: player.previous()
        Accessible.name: "Previous"
        anchors.verticalCenter: parent.verticalCenter
    }
    IconButton {
        width: root.size * 1.25; height: width
        prominent: true
        glyphSize: root.size * 0.55
        glyph: player.playing ? Glyphs.pause : Glyphs.play
        enabled: player.hasTrack
        onClicked: player.togglePlayPause()
        Accessible.name: player.playing ? "Pause" : "Play"
        anchors.verticalCenter: parent.verticalCenter
    }
    IconButton {
        glyph: Glyphs.next
        enabled: player.hasTrack
        onClicked: player.next()
        Accessible.name: "Next"
        anchors.verticalCenter: parent.verticalCenter
    }
    IconButton {
        visible: root.showModes
        glyph: player.repeatMode === 2 ? Glyphs.repeatOne : Glyphs.repeatAll
        tint: player.repeatMode === 0 ? root.color : root.accent
        onClicked: player.cycleRepeat()
        Accessible.name: ["Repeat Off", "Repeat All", "Repeat One"][player.repeatMode]
        anchors.verticalCenter: parent.verticalCenter
    }
}
