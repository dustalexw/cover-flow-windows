import QtQuick
import QtQuick.Controls

// GridView.swift: adaptive 240–360 px cells, hover play button, marquee selection.
FocusScope {
    id: root
    required property var albums
    property var selectedIds: ({})
    signal albumActivated(string albumId, int modifiers)
    signal openTracks(string albumId)
    signal marqueeSelect(var ids, bool additive)
    signal contextMenuRequested(string albumId)

    function reveal(albumId) {
        const i = albums.indexOf(albumId)
        if (i >= 0) grid.positionViewAtIndex(i, GridView.Center)
    }

    readonly property real padding: 20
    readonly property int columns: Math.max(1, Math.floor((width - padding * 2 + 20) / (240 + 20)))
    readonly property real cellSide: Math.min(360, (width - padding * 2 - (columns - 1) * 20) / columns)

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (app.browsedAlbumId) root.openTracks(app.browsedAlbumId)
            event.accepted = true
        }
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: root.padding
        anchors.topMargin: root.padding
        clip: true
        model: root.albums
        cellWidth: root.cellSide + 20
        cellHeight: root.cellSide + 24 + 44
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        interactive: !marquee.active

        delegate: Item {
            id: cell
            required property int index
            required property string albumId
            required property string title
            required property string artist
            required property string artwork
            required property bool isDemo
            readonly property bool selected: !!root.selectedIds[albumId]
            readonly property bool playing: albumId === player.currentAlbumId
            width: root.cellSide
            height: root.cellSide + 44

            Rectangle {
                anchors.fill: parent
                anchors.margins: -6
                radius: 8
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                visible: cell.selected
            }
            Item {
                id: artBox
                width: root.cellSide; height: root.cellSide
                scale: hover.hovered ? 1.02 : 1
                Behavior on scale { NumberAnimation { duration: 140 } }
                Rectangle {
                    x: img.paintedX; y: img.paintedY + (hover.hovered ? 9 : 5)
                    width: img.paintedWidth; height: img.paintedHeight
                    radius: 6; color: "black"
                    opacity: Theme.dark ? (hover.hovered ? 0.45 : 0.3) : 0.15
                }
                Image {
                    id: img
                    anchors.fill: parent
                    source: cell.artwork
                    sourceSize: Qt.size(360, 360)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    readonly property real paintedX: (width - paintedWidth) / 2
                    readonly property real paintedY: (height - paintedHeight) / 2
                }
                Rectangle {
                    visible: cell.playing
                    x: img.paintedX + 8; y: img.paintedY + 8
                    width: gbars.width + 12; height: gbars.height + 12
                    radius: 6; color: Qt.rgba(0, 0, 0, 0.55)
                    NowPlayingBars { id: gbars; anchors.centerIn: parent; barHeight: 11; color: "white"; animating: player.playing }
                }
                RoundButton {
                    visible: hover.hovered || cell.playing
                    x: img.paintedX + img.paintedWidth - width - 10
                    y: img.paintedY + img.paintedHeight - height - 10
                    width: 34; height: 34
                    focusPolicy: Qt.NoFocus
                    onClicked: app.toggleAlbum(cell.albumId)
                    contentItem: Text {
                        text: cell.playing && player.playing ? Glyphs.pause : Glyphs.play
                        font.family: Glyphs.family; font.pixelSize: 14
                        color: Theme.base
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle { radius: 17; color: Theme.accent }
                    Accessible.name: cell.playing && player.playing ? "Pause" : "Play " + cell.title
                }
            }
            Column {
                anchors.top: artBox.bottom
                anchors.topMargin: 8
                width: parent.width
                spacing: 2
                Row {
                    spacing: 6
                    width: parent.width
                    Rectangle {
                        visible: cell.isDemo
                        radius: 3; color: Theme.hover
                        width: dl.implicitWidth + 8; height: dl.implicitHeight + 2
                        Label { id: dl; anchors.centerIn: parent; text: "DEMO"; font.pixelSize: 9; font.bold: true; color: Theme.secondary }
                    }
                    Label { text: cell.title; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text; elide: Text.ElideRight; width: parent.width - (cell.isDemo ? 50 : 0) }
                }
                Label { text: cell.artist; font.pixelSize: 12; color: Theme.secondary; elide: Text.ElideRight; width: parent.width }
            }
            HoverHandler { id: hover }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: (point) => {
                    if (marquee.suppressUntil > Date.now()) return
                    root.forceActiveFocus()
                    root.albumActivated(cell.albumId, point.modifiers)
                }
                onDoubleTapped: app.playAlbum(cell.albumId)
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: root.contextMenuRequested(cell.albumId)
            }
        }
    }

    // Marquee selection: drag on empty space (or anywhere with Shift/Ctrl).
    MouseArea {
        id: marquee
        anchors.fill: grid
        anchors.leftMargin: -root.padding
        acceptedButtons: Qt.LeftButton
        propagateComposedEvents: true
        property bool active: false
        property point start
        property point current
        property bool additive: false
        property real suppressUntil: 0
        onPressed: (mouse) => {
            const p = grid.mapFromItem(marquee, mouse.x, mouse.y)
            const onCell = grid.indexAt(p.x + grid.contentX, p.y + grid.contentY) >= 0
            if (onCell && !(mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))) { mouse.accepted = false; return }
            start = Qt.point(mouse.x, mouse.y + grid.contentY)
            current = start
            additive = mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)
            active = false
        }
        onPositionChanged: (mouse) => {
            current = Qt.point(mouse.x, mouse.y + grid.contentY)
            if (!active && Math.hypot(current.x - start.x, current.y - start.y) >= 5) active = true
            if (active) {
                const x0 = Math.min(start.x, current.x) - root.padding, x1 = Math.max(start.x, current.x) - root.padding
                const y0 = Math.min(start.y, current.y), y1 = Math.max(start.y, current.y)
                const ids = []
                for (let i = 0; i < root.albums.count; ++i) {
                    const col = i % root.columns, row = Math.floor(i / root.columns)
                    const cx = col * grid.cellWidth, cy = row * grid.cellHeight
                    if (cx < x1 && cx + root.cellSide > x0 && cy < y1 && cy + root.cellSide + 44 > y0) ids.push(root.albums.idAt(i))
                }
                root.marqueeSelect(ids, additive)
            }
        }
        onReleased: {
            if (active) suppressUntil = Date.now() + 100
            else if (!additive) root.marqueeSelect([], false)
            active = false
        }
        onWheel: (wheel) => { wheel.accepted = false }
    }
    Rectangle {
        visible: marquee.active
        x: Math.min(marquee.start.x, marquee.current.x)
        y: Math.min(marquee.start.y, marquee.current.y) - grid.contentY + root.padding
        width: Math.abs(marquee.current.x - marquee.start.x)
        height: Math.abs(marquee.current.y - marquee.start.y)
        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.8)
    }
}
