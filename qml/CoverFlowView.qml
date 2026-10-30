import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// CoverFlowView.swift. One continuous `position` drives every cover; the snap is a
// spring (response 0.42 s, damping 0.86) integrated per frame.
FocusScope {
    id: root
    required property var albums          // AlbumModel
    property var selectedIds: ({})
    signal albumActivated(string albumId, int modifiers)   // click on a cover
    signal openTracks(string albumId)
    signal extendSelection(string albumId)      // Shift+arrow

    readonly property int count: albums.count
    property real position: 0
    property int target: 0
    property real velocity: 0
    readonly property int index: count === 0 ? 0 : Math.max(0, Math.min(count - 1, Math.round(position)))
    readonly property string currentId: { revision; return albums.idAt(index) }
    property int revision: 0                // bumps when album data changes

    // ----- layout (CoverFlowLayout) -----
    readonly property bool compactCaption: width >= 900
    readonly property real captionHeight: compactCaption ? 100 : 148
    readonly property bool showScrubber: count > 6
    readonly property real scrubberHeight: showScrubber ? 20 : 0
    readonly property real coverSize: Math.max(1, Math.min((height - captionHeight - scrubberHeight - 4 - 16 - 3) / 1.14, width * 0.58))
    readonly property real unit: coverSize * 0.3

    Connections {
        target: root.albums
        function onModelReset() { root.revision++; root.jumpToBrowsed() }
        function onDataChanged() { root.revision++ }
    }
    Component.onCompleted: jumpToBrowsed()
    onIndexChanged: if (count > 0) app.browsedAlbumId = albums.idAt(index)

    function jumpToBrowsed() {
        const i = albums.indexOf(app.browsedAlbumId)
        position = i >= 0 ? i : 0
        target = Math.round(position)
        velocity = 0
        spring.running = false
    }
    function snapTo(i) {
        target = Math.max(0, Math.min(count - 1, i))
        if (Theme.reduceMotion) { position = target; return }
        spring.running = true
    }
    function step(delta) {
        const base = Math.abs(target - position) < 1.5 ? target : index
        snapTo(base + delta)
    }
    function reveal(albumId) {
        const i = albums.indexOf(albumId)
        if (i >= 0) snapTo(i)
    }
    // Rubber band beyond the ends.
    function banded(p) {
        const max = Math.max(0, count - 1)
        if (p < 0) return -0.25 * (-p) / (1 - p)
        if (p > max) { const d = p - max; return max + 0.25 * d / (1 + d) }
        return p
    }

    // Spring: stiffness (2π/0.42)², damping 4π·0.86/0.42.
    FrameAnimation {
        id: spring
        onTriggered: {
            const k = 223.8, c = 25.7
            let dt = Math.min(frameTime, 0.05)
            const steps = Math.ceil(dt / 0.004)
            const h = dt / steps
            for (let s = 0; s < steps; ++s) {
                const a = -k * (root.position - root.target) - c * root.velocity
                root.velocity += a * h
                root.position += root.velocity * h
            }
            if (Math.abs(root.position - root.target) < 0.0005 && Math.abs(root.velocity) < 0.002) {
                root.position = root.target
                root.velocity = 0
                running = false
            }
        }
    }

    // ----- keyboard -----
    Keys.onPressed: (event) => {
        const shift = event.modifiers & Qt.ShiftModifier
        switch (event.key) {
        case Qt.Key_Left: step(-1); if (shift) root.extendSelection(albums.idAt(target)); event.accepted = true; break
        case Qt.Key_Right: step(1); if (shift) root.extendSelection(albums.idAt(target)); event.accepted = true; break
        case Qt.Key_Home: snapTo(0); event.accepted = true; break
        case Qt.Key_End: snapTo(count - 1); event.accepted = true; break
        case Qt.Key_PageUp: step(-5); event.accepted = true; break
        case Qt.Key_PageDown: step(5); event.accepted = true; break
        case Qt.Key_Return: case Qt.Key_Enter: if (count) root.openTracks(currentId); event.accepted = true; break
        }
    }

    // ----- stage -----
    Item {
        id: stage
        anchors.top: parent.top
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        height: root.coverSize * 1.14 + 3

        readonly property int windowStart: Math.max(0, root.index - 8)
        readonly property int windowEnd: Math.min(root.count - 1, root.index + 8)

        Repeater {
            model: root.count === 0 ? 0 : stage.windowEnd - stage.windowStart + 1
            delegate: Item {
                id: cover
                required property int index
                readonly property int albumIndex: stage.windowStart + index
                readonly property var album: { root.revision; return root.albums.get(albumIndex) }
                readonly property real size: root.coverSize
                readonly property real d: albumIndex - root.position
                readonly property real a: Math.abs(d)
                readonly property real sign: d < 0 ? -1 : 1
                readonly property real t: Math.min(a, 1)
                readonly property real eased: t * t * (3 - 2 * t)
                readonly property real gap: size * 0.68
                readonly property real spacing: size * (Theme.reduceMotion ? 0.30 : 0.18)
                readonly property real offsetX: sign * (a * spacing + (gap - spacing) * eased)
                readonly property real angle: -sign * (Theme.reduceMotion ? 0 : 62) * eased
                readonly property real scaleFactor: 1 - (Theme.reduceMotion ? 0.22 : 0.10) * eased
                readonly property real shade: Math.min(a, 1)
                readonly property real emphasis: Math.max(0, 1 - a)
                readonly property bool selected: !!root.selectedIds[album.albumId]
                readonly property bool playing: album.albumId === player.currentAlbumId

                width: size
                height: size * 1.14 + 3
                x: (stage.width - width) / 2
                z: -a
                opacity: Math.max(0, Math.min(1, 8 - a))
                // Rotation about the vertical centre line with perspective (eye ≈ size / 0.42),
                // then scale, then horizontal offset.
                transform: [
                    Matrix4x4 {
                        matrix: {
                            // Anchor = centre of the cover + reflection column (as on macOS).
                            const cx = cover.width / 2, cy = cover.height / 2
                            const dist = Math.max(cover.height, 1) / 0.42
                            const m = Qt.matrix4x4()
                            m.translate(Qt.vector3d(cx, cy, 0))
                            const p = Qt.matrix4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -1 / dist, 1)
                            const r = Qt.matrix4x4()
                            r.rotate(cover.angle, Qt.vector3d(0, 1, 0))
                            const back = Qt.matrix4x4()
                            back.translate(Qt.vector3d(-cx, -cy, 0))
                            return m.times(p).times(r).times(back)
                        }
                    },
                    Scale { origin.x: cover.width / 2; origin.y: cover.height / 2; xScale: cover.scaleFactor; yScale: cover.scaleFactor },
                    Translate { x: cover.offsetX }
                ]

                // Soft shadow under the cover: a few widening, fading layers.
                Repeater {
                    model: 4
                    Rectangle {
                        required property int index
                        readonly property real spread: 2 + index * (3 + 3 * cover.emphasis)
                        x: art.paintedX - spread; y: art.paintedY - spread + 4 + 8 * cover.emphasis
                        width: art.paintedWidth + spread * 2; height: art.paintedHeight + spread * 2
                        radius: spread + 3
                        color: "black"
                        opacity: (Theme.dark ? 0.14 : 0.035) * (0.4 + 0.6 * cover.emphasis)
                    }
                }
                Rectangle {   // selection ring
                    visible: cover.selected
                    x: art.paintedX - 5; y: art.paintedY - 5
                    width: art.paintedWidth + 10; height: art.paintedHeight + 10
                    radius: 6; color: "transparent"
                    border.color: Theme.accent; border.width: 3
                }
                Image {
                    id: art
                    width: cover.size; height: cover.size
                    source: cover.album.artwork || ""
                    sourceSize: Qt.size(cover.size > 360 ? 720 : 360, cover.size > 360 ? 720 : 360)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true
                    readonly property real paintedX: (width - paintedWidth) / 2
                    readonly property real paintedY: (height - paintedHeight) / 2
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                    Rectangle {
                        x: art.paintedX; y: art.paintedY; width: art.paintedWidth; height: art.paintedHeight
                        color: "black"
                        opacity: (Theme.dark ? 0.40 : 0.22) * cover.shade
                    }
                }
                Rectangle {   // now playing badge
                    visible: cover.playing
                    anchors.right: art.right; anchors.top: art.top
                    anchors.rightMargin: art.paintedX + cover.size * 0.035
                    anchors.topMargin: art.paintedY + cover.size * 0.035
                    width: bars.width + 14; height: bars.height + 14
                    radius: 6; color: Qt.rgba(0, 0, 0, 0.55)
                    NowPlayingBars {
                        id: bars
                        anchors.centerIn: parent
                        barHeight: Math.max(10, cover.size * 0.035)
                        color: "white"
                        animating: player.playing
                    }
                }
                // Reflection: the bottom 14 % of the cover, mirrored, fading out.
                Item {
                    y: cover.size + 3
                    width: cover.size; height: cover.size * 0.14
                    clip: true
                    opacity: (1 - cover.shade * 0.5) * (Theme.dark ? 0.22 : 0.16)
                    Image {
                        width: cover.size; height: cover.size
                        y: -(cover.size - art.paintedY - art.paintedHeight)
                        source: art.source
                        sourceSize: art.sourceSize
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        transform: Scale { origin.y: cover.size / 2; yScale: -1 }
                    }
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0; color: "transparent" }
                            GradientStop { position: 1; color: Theme.base }
                        }
                    }
                }
                function hit(px, py) {
                    const p = cover.mapFromItem(stage, px, py)
                    return p.x >= art.paintedX && p.x <= art.paintedX + art.paintedWidth && p.y >= art.paintedY && p.y <= art.paintedY + art.paintedHeight
                }
            }
        }

        // Drag, click and wheel handling for the whole stage.
        MouseArea {
            id: input
            anchors.fill: parent
            property real pressX: 0
            property real dragStart: 0
            property bool dragging: false
            property var samples: []
            acceptedButtons: Qt.LeftButton
            onPressed: (mouse) => {
                root.forceActiveFocus()
                pressX = mouse.x
                dragStart = root.position
                dragging = false
                samples = [{ x: mouse.x, t: Date.now() }]
            }
            onPositionChanged: (mouse) => {
                const dx = mouse.x - pressX
                if (!dragging && Math.abs(dx) >= 4 && !(mouse.modifiers & Qt.ShiftModifier)) {
                    dragging = true
                    spring.running = false
                    root.velocity = 0
                }
                if (dragging) {
                    root.position = root.banded(dragStart - dx / root.unit)
                    samples.push({ x: mouse.x, t: Date.now() })
                    if (samples.length > 6) samples.shift()
                }
            }
            onReleased: (mouse) => {
                if (dragging) {
                    const first = samples[0], last = samples[samples.length - 1]
                    const dt = Math.max(1, last.t - first.t) / 1000
                    const v = (last.x - first.x) / dt
                    const predicted = (mouse.x - pressX) + v * 0.25
                    const travel = Math.max(-12, Math.min(12, predicted / root.unit))
                    root.velocity = 0
                    root.snapTo(Math.round(dragStart - travel))
                    dragging = false
                    return
                }
                // Click: topmost cover under the pointer.
                let best = null
                for (let i = 0; i < stage.children.length; ++i) {
                    const c = stage.children[i]
                    if (c.hit && c.hit(mouse.x, mouse.y) && (!best || c.z > best.z)) best = c
                }
                if (!best) return
                const id = best.album.albumId
                if (!(mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))) root.snapTo(best.albumIndex)
                root.albumActivated(id, mouse.modifiers)
            }
            onDoubleClicked: (mouse) => { if (root.count) app.toggleAlbum(root.currentId) }

            // Mouse wheel: one album per notch. Touchpads (pixel deltas) scroll
            // continuously and snap after 0.18 s idle.
            property real accumulated: 0
            property real scrollStart: NaN
            property real scrollDelta: 0
            onWheel: (wheel) => {
                const usePixel = wheel.pixelDelta.x !== 0 || wheel.pixelDelta.y !== 0
                if (usePixel) {
                    const delta = Math.abs(wheel.pixelDelta.x) > Math.abs(wheel.pixelDelta.y) ? wheel.pixelDelta.x : wheel.pixelDelta.y
                    if (isNaN(scrollStart)) { scrollStart = root.position; scrollDelta = 0; spring.running = false; root.velocity = 0 }
                    scrollDelta += delta
                    const raw = Math.max(-2, Math.min(root.count + 1, scrollStart - scrollDelta / root.unit))
                    root.position = root.banded(raw)
                    idle.restart()
                } else {
                    const d = Math.abs(wheel.angleDelta.x) > Math.abs(wheel.angleDelta.y) ? wheel.angleDelta.x : wheel.angleDelta.y
                    accumulated += d / 120
                    if (Math.abs(accumulated) >= 0.9) {
                        root.step(accumulated > 0 ? -1 : 1)
                        accumulated = 0
                    }
                }
                wheel.accepted = true
            }
            Timer {
                id: idle
                interval: 180
                onTriggered: { input.scrollStart = NaN; root.snapTo(root.index) }
            }
        }
    }

    // ----- caption -----
    Item {
        id: caption
        anchors.top: stage.bottom
        anchors.topMargin: 4
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 48, 980)
        height: root.captionHeight
        visible: root.count > 0
        readonly property var album: { root.revision; return root.albums.get(root.index) }
        readonly property bool isCurrent: album.albumId === player.currentAlbumId

        GridLayout {
            anchors.fill: parent
            columns: root.compactCaption ? 2 : 1
            columnSpacing: 24
            rowSpacing: 6

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: root.compactCaption ? Qt.AlignVCenter : Qt.AlignHCenter
                spacing: 2
                RowLayout {
                    Layout.alignment: root.compactCaption ? Qt.AlignLeft : Qt.AlignHCenter
                    Layout.maximumWidth: caption.width
                    spacing: 8
                    Rectangle {
                        visible: !!caption.album.isDemo
                        radius: 4; color: Theme.hover
                        implicitWidth: demo.implicitWidth + 10; implicitHeight: demo.implicitHeight + 4
                        Label { id: demo; anchors.centerIn: parent; text: "DEMO"; font.pixelSize: 10; font.bold: true; color: Theme.secondary }
                    }
                    Label {
                        text: caption.album.title || ""
                        font.family: Theme.serif
                        font.pixelSize: root.compactCaption ? 24 : 26
                        font.weight: Font.DemiBold
                        color: Theme.text
                        elide: Text.ElideRight
                        Layout.maximumWidth: caption.width - 60
                    }
                }
                Label {
                    Layout.alignment: root.compactCaption ? Qt.AlignLeft : Qt.AlignHCenter
                    text: caption.album.artist || ""
                    font.pixelSize: 15; font.weight: Font.Medium
                    color: Theme.secondary
                    elide: Text.ElideRight
                    Layout.maximumWidth: caption.width
                }
                Label {
                    Layout.alignment: root.compactCaption ? Qt.AlignLeft : Qt.AlignHCenter
                    text: (caption.album.meta || "") + (caption.album.missingMetadata ? "  ·  Missing album info" : "")
                    font.pixelSize: 12
                    color: Theme.secondary
                    opacity: 0.85
                }
            }
            RowLayout {
                Layout.alignment: root.compactCaption ? (Qt.AlignRight | Qt.AlignVCenter) : Qt.AlignHCenter
                spacing: 10
                Button {
                    text: caption.isCurrent ? (player.playing ? "Pause" : "Resume") : "Play"
                    highlighted: true
                    onClicked: app.toggleAlbum(caption.album.albumId)
                    background: Rectangle { implicitWidth: 92; implicitHeight: 30; radius: 15; color: parent.down ? Qt.darker(Theme.accent, 1.2) : Theme.accent }
                    contentItem: Label { text: parent.text; color: Theme.base; font.pixelSize: 13; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
                Button {
                    text: "Tracks"
                    onClicked: root.openTracks(caption.album.albumId)
                    background: Rectangle { implicitWidth: 80; implicitHeight: 30; radius: 15; color: parent.hovered ? Theme.hover : "transparent"; border.color: Theme.hairline }
                    contentItem: Label { text: parent.text; color: Theme.text; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
                RowLayout {
                    visible: caption.isCurrent
                    spacing: 6
                    NowPlayingBars { barHeight: 11; animating: player.playing }
                    Label { text: player.playing ? "Now Playing" : "Paused"; color: Theme.accent; font.pixelSize: 12 }
                }
                Label {
                    visible: !caption.isCurrent && player.hasTrack
                    text: "<a href='#'>Playing: " + player.albumTitle + "</a>"
                    textFormat: Text.StyledText
                    linkColor: Theme.secondary
                    font.pixelSize: 12
                    onLinkActivated: root.reveal(player.currentAlbumId)
                    elide: Text.ElideRight
                    Layout.maximumWidth: 220
                }
            }
        }
    }

    // ----- scrubber -----
    Slider {
        visible: root.showScrubber
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(560, parent.width - 80)
        height: 20
        opacity: 0.55
        from: 0; to: Math.max(1, root.count - 1); stepSize: 1
        value: pressed ? value : root.index
        onMoved: root.snapTo(Math.round(value))
        focusPolicy: Qt.NoFocus
        Accessible.name: "Browse albums"
    }

    Accessible.role: Accessible.Slider
    Accessible.name: "Cover Flow"
    Accessible.description: count ? (caption.album.title + " by " + caption.album.artist + ", album " + (index + 1) + " of " + count) : ""
}
