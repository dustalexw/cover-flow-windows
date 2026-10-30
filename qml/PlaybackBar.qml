import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// PlaybackControls.swift: 80 px Now Playing bar with three width breakpoints.
Rectangle {
    id: root
    signal revealAlbum(string albumId)
    signal toggleAlbumView()
    signal openLyrics()
    height: 80
    color: Theme.bar
    readonly property bool wide: width > 1040
    readonly property bool medium: width > 860

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.hairline }

    component BarButton: AbstractButton {
        id: b
        property string glyph
        property bool active: false
        implicitWidth: 30; implicitHeight: 30
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        contentItem: Text {
            text: b.glyph; font.family: Glyphs.family; font.pixelSize: 15
            color: b.active ? Theme.accent : Theme.text
            opacity: b.enabled ? 1 : 0.35
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { radius: 6; color: b.hovered ? Theme.hover : "transparent" }
        ToolTip.visible: hovered
        ToolTip.text: Accessible.name
        ToolTip.delay: 600
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 16

        // Left: artwork + titles.
        RowLayout {
            Layout.preferredWidth: root.wide ? 280 : root.medium ? 210 : 150
            Layout.maximumWidth: Layout.preferredWidth
            spacing: 10
            Item {
                Layout.preferredWidth: 52; Layout.preferredHeight: 52
                Rectangle { anchors.fill: parent; radius: 4; color: Theme.hover; visible: !player.hasTrack }
                Image {
                    anchors.fill: parent
                    visible: player.hasTrack
                    source: app.currentArtwork
                    sourceSize: Qt.size(160, 160)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.toggleAlbumView() }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                MarqueeText {
                    Layout.fillWidth: true
                    text: player.hasTrack ? player.title : "Nothing playing"
                    font.pixelSize: 13; font.weight: Font.DemiBold
                    color: Theme.text
                }
                Label {
                    Layout.fillWidth: true
                    text: player.hasTrack ? player.artist + " — " + player.albumTitle : "Choose an album and press Play"
                    font.pixelSize: 12
                    color: Theme.secondary
                    elide: Text.ElideRight
                    MouseArea {
                        anchors.fill: parent
                        enabled: player.hasTrack
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.revealAlbum(player.currentAlbumId)
                    }
                }
            }
        }

        // Centre: transport + seek bar.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: 560
            Layout.alignment: Qt.AlignHCenter
            spacing: 2
            TransportControls {
                Layout.alignment: Qt.AlignHCenter
                size: 28
                showModes: root.medium
            }
            SeekBar { Layout.fillWidth: true }
        }

        // Right: volume, lyrics, queue, Album View.
        RowLayout {
            Layout.preferredWidth: root.wide ? 468 : root.medium ? 378 : 288
            Layout.maximumWidth: Layout.preferredWidth
            spacing: 6
            Item { Layout.fillWidth: true }
            BarButton {
                glyph: player.muted || player.volume === 0 ? Glyphs.volumeMute : player.volume < 0.34 ? Glyphs.volumeLow : player.volume < 0.67 ? Glyphs.volumeMid : Glyphs.volumeHigh
                onClicked: player.muted = !player.muted
                Accessible.name: player.muted ? "Unmute" : "Mute"
            }
            Slider {
                visible: root.wide
                Layout.preferredWidth: 92
                from: 0; to: 1
                value: player.muted ? 0 : player.volume
                onMoved: player.volume = value
                focusPolicy: Qt.NoFocus
                Accessible.name: "Volume"
            }
            BarButton { glyph: Glyphs.lyrics; onClicked: root.openLyrics(); Accessible.name: "Lyrics" }
            BarButton { glyph: Glyphs.queue; active: app.showQueue; onClicked: app.showQueue = !app.showQueue; Accessible.name: "Queue" }
            Button {
                text: "Album View"
                focusPolicy: Qt.NoFocus
                onClicked: root.toggleAlbumView()
                contentItem: Label { text: parent.text; font.pixelSize: 12; color: app.albumViewOpen ? Theme.base : Theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { implicitWidth: 92; implicitHeight: 26; radius: 13; color: app.albumViewOpen ? Theme.accent : (parent.hovered ? Theme.hover : "transparent"); border.color: Theme.hairline }
            }
        }
    }
}
