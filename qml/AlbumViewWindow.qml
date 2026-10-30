import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// AlbumViewController/AlbumViewScreen: a resizable mini-player of the playing album
// with Compact / Large / Immersive presets, keep-on-top and auto-hiding controls.
ApplicationWindow {
    id: root
    palette.window: Theme.base
    palette.windowText: Theme.text
    palette.base: Theme.panel
    palette.text: Theme.text
    palette.button: Theme.fieldBg
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.base
    palette.placeholderText: Theme.secondary
    palette.toolTipBase: Theme.panel
    palette.toolTipText: Theme.text
    palette.mid: Theme.hairline
    palette.light: Theme.panel
    palette.dark: Theme.secondary
    signal revealAlbum(string albumId)
    signal openLyrics()
    title: "Album View"
    width: 320; height: 430
    minimumWidth: 260; minimumHeight: 340
    color: Theme.dark ? Qt.rgba(0.06, 0.06, 0.06, 1) : Theme.base
    flags: Qt.Window | (app.albumViewKeepOnTop ? Qt.WindowStaysOnTopHint : 0)

    property string preset: "compact"     // compact | large | immersive | ""
    property rect beforeImmersive
    property string presetBeforeImmersive: "compact"
    property bool focusMode: false
    readonly property bool immersive: preset === "immersive"
    readonly property bool narrow: width < 430
    readonly property bool short_: height < 520
    readonly property real controlsHeight: immersive ? Math.min(118, height * 0.12) : (narrow || short_ ? 104 : 150)
    readonly property real topInset: immersive ? Math.max(24, height * 0.025) : 38

    // Geometry, preset and focus mode persist (albumView/* keys).
    function saveState() {
        app.setSetting("albumView/geometry", Qt.rect(x, y, width, height))
        app.setSetting("albumView/preset", immersive ? presetBeforeImmersive : preset)
        app.setSetting("albumView/focusMode", focusMode)
    }
    onClosing: saveState()
    Component.onCompleted: {
        const g = app.setting("albumView/geometry")
        if (g && g.width > 0) { x = g.x; y = g.y; width = g.width; height = g.height }
        else {
            // First open: bottom-right of the screen.
            x = Screen.desktopAvailableWidth - 340
            y = Screen.desktopAvailableHeight - height - 110
        }
        preset = app.setting("albumView/preset", "compact")
        const f = app.setting("albumView/focusMode", false)
        focusMode = f === true || f === "true"
        // Keep at least 80×80 px on screen.
        x = Math.max(-width + 80, Math.min(x, Screen.desktopAvailableWidth - 80))
        y = Math.max(0, Math.min(y, Screen.desktopAvailableHeight - 80))
    }

    function applyPreset(p) {
        const vw = Screen.desktopAvailableWidth, vh = Screen.desktopAvailableHeight
        if (p === "immersive" && !immersive) { beforeImmersive = Qt.rect(x, y, width, height); presetBeforeImmersive = preset || "" }
        if (p === "compact") {
            const cx = x + width / 2, cy = y + height / 2
            width = 320; height = 430; x = cx - 160; y = cy - 215
        } else if (p === "large") {
            const w = Math.min(vw * 0.9, vh * 0.88), h = vh * 0.94
            const cx = x + width / 2
            width = w; height = h; x = Math.max(0, Math.min(cx - w / 2, vw - w)); y = (vh - h) / 2
        } else if (p === "immersive") {
            width = vw * 0.96; height = vh * 0.96; x = vw * 0.02; y = vh * 0.02
        }
        preset = p
    }
    function exitImmersive() {
        if (!immersive) return
        if (beforeImmersive.width > 0) { x = beforeImmersive.x; y = beforeImmersive.y; width = beforeImmersive.width; height = beforeImmersive.height; preset = presetBeforeImmersive }
        else applyPreset("large")
    }

    // ----- auto-hide (focus mode or immersive, while playing) -----
    property bool controlsVisible: true
    readonly property bool autoHide: (focusMode || immersive) && player.hasTrack
    Timer { id: hideTimer; interval: 3200; onTriggered: if (root.autoHide) root.controlsVisible = false }
    function poke() { controlsVisible = true; if (autoHide) hideTimer.restart() }
    onAutoHideChanged: poke()

    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => {
            root.poke()
            if (e.key === Qt.Key_Space) player.togglePlayPause()
            else if (e.key === Qt.Key_Left) (e.modifiers & Qt.AltModifier) ? player.skip(-10) : player.previous()
            else if (e.key === Qt.Key_Right) (e.modifiers & Qt.AltModifier) ? player.skip(10) : player.next()
            else if (e.key === Qt.Key_Escape) { if (root.immersive) root.exitImmersive(); else if (root.focusMode) root.focusMode = false; else root.close() }
            else if (e.key === Qt.Key_1) root.applyPreset("compact")
            else if (e.key === Qt.Key_2) root.applyPreset("large")
            else if (e.key === Qt.Key_3) root.applyPreset("immersive")
            else if (e.key === Qt.Key_F) root.focusMode = !root.focusMode
            else return
            e.accepted = true
        }

        // Backdrop tinted by the playing album.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: player.currentAlbumId ? app.backdropTint(player.currentAlbumId, true) : root.color }
                GradientStop { position: 1; color: root.color }
            }
            opacity: 0.55
        }
        // Immersive: blurred-looking cover fill behind everything.
        Image {
            anchors.fill: parent
            visible: root.immersive && player.hasTrack
            source: app.currentArtwork
            sourceSize: Qt.size(48, 48)        // tiny decode, upscaled = soft blur
            fillMode: Image.PreserveAspectCrop
            smooth: true
            opacity: 0.65
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.32) }
                    GradientStop { position: 0.35; color: "transparent" }
                    GradientStop { position: 0.65; color: "transparent" }
                    GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.48) }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onPositionChanged: root.poke()
            onPressed: if (!root.immersive) root.startSystemMove()
        }

        // Artwork.
        Item {
            id: artBox
            x: (parent.width - width) / 2
            y: root.topInset
            width: parent.width - (root.narrow ? 28 : 56)
            height: Math.max(80, parent.height - root.topInset - root.controlsHeight - (root.immersive ? 18 : 22))
            Image {
                id: hero
                anchors.fill: parent
                visible: player.hasTrack
                source: app.currentArtwork
                sourceSize: Qt.size(Math.min(4096, Math.max(width, height) * 2), Math.min(4096, Math.max(width, height) * 2))
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                MouseArea {
                    x: hero.x + (hero.width - hero.paintedWidth) / 2; y: (hero.height - hero.paintedHeight) / 2
                    width: hero.paintedWidth; height: hero.paintedHeight
                    onDoubleClicked: root.immersive ? root.exitImmersive() : root.applyPreset("immersive")
                    onPressed: if (!root.immersive) root.startSystemMove()
                }
            }
            ColumnLayout {
                anchors.centerIn: parent
                visible: !player.hasTrack
                Text { Layout.alignment: Qt.AlignHCenter; text: Glyphs.disc; font.family: Glyphs.family; font.pixelSize: 48; color: Theme.secondary }
                Label { Layout.alignment: Qt.AlignHCenter; text: "Nothing playing"; color: Theme.secondary; font.pixelSize: 15 }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    visible: app.browsedAlbumId !== ""
                    text: "Play " + (app.albumInfo(app.browsedAlbumId).title || "")
                    onClicked: app.playAlbum(app.browsedAlbumId)
                }
            }
        }

        // Top bar: presets, lyrics, focus, pin.
        RowLayout {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 8
            opacity: root.controlsVisible ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: root.controlsVisible ? 250 : 600 } }
            Label { visible: !root.narrow; text: "ALBUM VIEW"; font.pixelSize: 10; font.letterSpacing: 1.5; color: Theme.secondary }
            Item { Layout.fillWidth: true }
            Repeater {
                model: [["compact", "Compact", "1"], ["large", "Large", "2"], ["immersive", "Immersive", "3"]]
                Button {
                    required property var modelData
                    text: root.narrow ? modelData[2] : modelData[1]
                    checkable: true
                    checked: root.preset === modelData[0]
                    focusPolicy: Qt.NoFocus
                    onClicked: modelData[0] === "immersive" && root.immersive ? root.exitImmersive() : root.applyPreset(modelData[0])
                    font.pixelSize: 11
                    implicitHeight: 24
                    ToolTip.visible: hovered; ToolTip.text: modelData[1] + " (" + modelData[2] + ")"
                }
            }
            ToolButton { text: Glyphs.lyrics; font.family: Glyphs.family; focusPolicy: Qt.NoFocus; onClicked: root.openLyrics(); ToolTip.visible: hovered; ToolTip.text: "Lyrics" }
            ToolButton { text: Glyphs.eye; font.family: Glyphs.family; checkable: true; checked: root.focusMode; focusPolicy: Qt.NoFocus; onToggled: root.focusMode = checked; ToolTip.visible: hovered; ToolTip.text: "Artwork focus (F)" }
            ToolButton { text: app.albumViewKeepOnTop ? Glyphs.pin : Glyphs.unpin; font.family: Glyphs.family; focusPolicy: Qt.NoFocus; onClicked: app.albumViewKeepOnTop = !app.albumViewKeepOnTop; ToolTip.visible: hovered; ToolTip.text: "Keep on top" }
        }

        // Controls.
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: root.narrow ? 14 : 28
            anchors.rightMargin: root.narrow ? 14 : 28
            anchors.bottomMargin: 10
            height: root.controlsHeight - 10
            spacing: 2
            opacity: root.controlsVisible ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: root.controlsVisible ? 250 : 600 } }
            MarqueeText {
                Layout.fillWidth: true
                text: player.title
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.serif
                font.pixelSize: root.narrow ? 15 : 18
                color: Theme.text
            }
            Label {
                Layout.fillWidth: true
                text: player.hasTrack ? player.artist + " — " + player.albumTitle : ""
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                color: Theme.secondary
                font.pixelSize: 12
            }
            SeekBar { Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                ToolButton {
                    visible: !root.narrow
                    text: player.muted ? Glyphs.volumeMute : Glyphs.volumeHigh; font.family: Glyphs.family
                    focusPolicy: Qt.NoFocus
                    onClicked: player.muted = !player.muted
                }
                Item { Layout.fillWidth: true }
                TransportControls { size: root.narrow ? 28 : 32; showModes: !root.narrow }
                Item { Layout.fillWidth: true }
                ToolButton {
                    text: Glyphs.album; font.family: Glyphs.family
                    focusPolicy: Qt.NoFocus
                    enabled: player.hasTrack
                    onClicked: root.revealAlbum(player.currentAlbumId)
                    ToolTip.visible: hovered; ToolTip.text: "Show album in collection"
                }
            }
        }

        // Resize grip (bottom-right).
        MouseArea {
            anchors.right: parent.right; anchors.bottom: parent.bottom
            width: 26; height: 26
            cursorShape: Qt.SizeFDiagCursor
            onPressed: { root.preset = ""; root.startSystemResize(Qt.RightEdge | Qt.BottomEdge) }
        }
    }
}
