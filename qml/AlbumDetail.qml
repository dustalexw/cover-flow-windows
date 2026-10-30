import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// AlbumDetailView.swift: modal track panel coloured from the cover.
Popup {
    id: root
    property string albumId: ""
    property var info: ({})
    property var colors: ({ background: Theme.panel, foreground: Theme.text, secondary: Theme.secondary, accent: Theme.accent, dark: Theme.dark })
    property var tracks: []
    signal editRequested(string albumId)
    signal findArtworkRequested(string albumId)
    signal removeRequested(var albumIds)

    function openAlbum(id) {
        albumId = id
        reload()
        open()
    }
    function reload() {
        info = app.albumInfo(albumId)
        if (!info.albumId) { close(); return }
        colors = app.albumPalette(albumId)
        tracks = app.albumTracks(albumId)
    }
    Connections {
        target: app.albums
        function onModelReset() { if (root.opened) root.reload() }
        function onDataChanged() { if (root.opened) root.reload() }
    }

    // Two columns when >9 tracks and no title needs a wide column (221 px at 13 px).
    TextMetrics { id: metrics; font.pixelSize: 13 }
    readonly property bool twoColumns: {
        if (tracks.length <= 9) return false
        for (const t of tracks) { metrics.text = t.title; if (t.title.indexOf("\n") >= 0 || metrics.width > 221) return false }
        return true
    }
    readonly property int splitAt: {
        const n = tracks.length
        let mid = Math.floor((n + 1) / 2)
        if (!twoColumns) return n
        const reach = Math.max(2, Math.floor(n / 6))
        for (let off = 0; off <= reach; ++off)
            for (const c of [mid - off, mid + off])
                if (c > 0 && c < n && tracks[c].disc !== tracks[c - 1].disc) return c
        return mid
    }
    readonly property int rowsPerColumn: twoColumns ? Math.max(splitAt, tracks.length - splitAt) : tracks.length

    parent: Overlay.overlay
    modal: true
    dim: true
    anchors.centerIn: parent
    width: Math.min(1080, parent.width - 40)
    height: Math.min(parent.height - 40, Math.max(540, Math.min(660, rowsPerColumn * 32 + 220)))
    padding: 30
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.45) }

    background: Rectangle {
        radius: 10
        color: root.colors.background
        border.color: Qt.rgba(root.colors.foreground.r, root.colors.foreground.g, root.colors.foreground.b, 0.1)
    }

    // Space toggles playback even when a button has focus.
    Shortcut { sequence: "Space"; enabled: root.opened; onActivated: player.togglePlayPause() }

    component PanelButton: Button {
        id: pb
        font.pixelSize: 12
        font.weight: Font.Medium
        focusPolicy: Qt.NoFocus
        contentItem: Label { text: pb.text; font: pb.font; color: root.colors.foreground; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        background: Rectangle {
            implicitWidth: 72; implicitHeight: 26
            radius: 4
            color: Qt.tint(root.colors.background, pb.down ? Qt.rgba(root.colors.foreground.r, root.colors.foreground.g, root.colors.foreground.b, 0.08) : Qt.rgba(1, 1, 1, 0.12))
            border.color: Qt.rgba(root.colors.foreground.r, root.colors.foreground.g, root.colors.foreground.b, 0.24)
        }
    }

    contentItem: RowLayout {
        spacing: 30
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    TextEdit {
                        Layout.fillWidth: true
                        text: root.info.title || ""
                        readOnly: true; selectByMouse: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 28; font.bold: true
                        color: root.colors.foreground
                        selectionColor: root.colors.accent
                    }
                    RowLayout {
                        spacing: 8
                        Label { text: root.info.artist || ""; font.pixelSize: 17; color: root.colors.accent }
                        Rectangle {
                            visible: !!root.info.isDemo
                            radius: 3; color: Qt.rgba(root.colors.foreground.r, root.colors.foreground.g, root.colors.foreground.b, 0.12)
                            implicitWidth: dlabel.implicitWidth + 8; implicitHeight: dlabel.implicitHeight + 2
                            Label { id: dlabel; anchors.centerIn: parent; text: "DEMO"; font.pixelSize: 9; font.bold: true; color: root.colors.secondary }
                        }
                    }
                    Label { visible: text !== ""; text: root.info.meta || ""; font.pixelSize: 12; color: root.colors.secondary }
                    Label { visible: !!root.info.missingMetadata; text: "Some info was guessed from file names"; font.pixelSize: 11; font.italic: true; color: root.colors.secondary }
                }
                ToolButton {
                    Layout.alignment: Qt.AlignTop
                    focusPolicy: Qt.NoFocus
                    onClicked: root.close()
                    contentItem: Text { text: Glyphs.close; font.family: Glyphs.family; font.pixelSize: 12; color: root.colors.secondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    Accessible.name: "Close"
                }
            }

            RowLayout {
                spacing: 8
                PanelButton { text: "Play"; onClicked: app.playAlbum(root.albumId, 0, false) }
                PanelButton { text: "Shuffle"; onClicked: app.playAlbum(root.albumId, 0, true) }
                PanelButton {
                    text: "…"
                    implicitWidth: 34
                    onClicked: moreMenu.popup()
                    Menu {
                        id: moreMenu
                        MenuItem { text: "Play Next"; onTriggered: app.playNextAlbums([root.albumId]) }
                        MenuItem { text: "Add to Queue"; onTriggered: app.addAlbumsToQueue([root.albumId]) }
                        MenuSeparator {}
                        MenuItem { text: "Edit Album Info…"; onTriggered: root.editRequested(root.albumId) }
                        MenuItem { text: "Find Artwork Online…"; onTriggered: root.findArtworkRequested(root.albumId) }
                        MenuItem { text: "Replace Artwork…"; onTriggered: artworkPicker.open() }
                        MenuItem { text: "Remove Artwork"; enabled: !!root.info.hasArtwork && !root.info.isDemo; onTriggered: app.removeArtwork(root.albumId) }
                        MenuSeparator {}
                        MenuItem { text: "Remove from Collection…"; onTriggered: root.removeRequested([root.albumId]) }
                    }
                }
                Item { Layout.fillWidth: true }
            }

            // Track list (one or two columns).
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 24
                Repeater {
                    model: root.twoColumns ? 2 : 1
                    ListView {
                        required property int index
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: index === 0 ? root.tracks.slice(0, root.splitAt) : root.tracks.slice(root.splitAt)
                        ScrollBar.vertical: ScrollBar {}
                        delegate: TrackRow {
                            required property var modelData
                            required property int index
                            track: modelData
                            width: ListView.view.width
                            showDiscHeading: modelData.multiDisc && (index === 0 || ListView.view.model[index - 1].disc !== modelData.disc)
                        }
                    }
                }
            }
            Label {
                text: root.info.footer || ""
                font.pixelSize: 11
                color: root.colors.secondary
            }
        }

        Image {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 340
            Layout.preferredHeight: 340
            source: root.info.artwork || ""
            sourceSize: Qt.size(720, 720)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }
    }

    component TrackRow: Column {
        id: row
        property var track
        property bool showDiscHeading: false
        readonly property bool current: track.trackId === player.currentTrackId

        Label {
            visible: row.showDiscHeading
            text: "Disc " + row.track.disc
            font.pixelSize: 11; font.bold: true
            color: root.colors.secondary
            topPadding: 6; bottomPadding: 4
        }
        Rectangle {
            width: row.width
            height: Math.max(30, content.implicitHeight + 10)
            radius: 4
            color: hover.hovered ? Qt.rgba(root.colors.foreground.r, root.colors.foreground.g, root.colors.foreground.b, 0.10)
                 : row.current ? Qt.rgba(root.colors.foreground.r, root.colors.foreground.g, root.colors.foreground.b, 0.07) : "transparent"
            RowLayout {
                id: content
                anchors.fill: parent
                anchors.leftMargin: 6; anchors.rightMargin: 8
                spacing: 10
                Item {
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 16
                    Label {
                        anchors.centerIn: parent
                        visible: !row.current && !hover.hovered
                        text: row.track.number
                        font.pixelSize: 12
                        color: root.colors.secondary
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: hover.hovered && !row.current
                        text: Glyphs.play; font.family: Glyphs.family; font.pixelSize: 11
                        color: root.colors.foreground
                    }
                    NowPlayingBars {
                        anchors.centerIn: parent
                        visible: row.current
                        barHeight: 11
                        color: root.colors.accent
                        animating: player.playing
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        text: row.track.title
                        wrapMode: Text.Wrap
                        font.pixelSize: 13
                        font.weight: row.current ? Font.Medium : Font.Normal
                        color: row.current ? root.colors.accent : root.colors.foreground
                    }
                    Label {
                        visible: row.track.showArtist
                        text: row.track.artist
                        font.pixelSize: 11
                        color: root.colors.secondary
                    }
                }
                Text {
                    visible: row.track.missing
                    text: Glyphs.warning; font.family: Glyphs.family; font.pixelSize: 12
                    color: "#e0a030"
                    HoverHandler { id: warnHover }
                    ToolTip.visible: warnHover.hovered
                    ToolTip.text: "File not found: " + row.track.path
                }
                Label {
                    text: row.track.duration
                    font.pixelSize: 12
                    color: root.colors.secondary
                }
            }
            HoverHandler { id: hover }
            TapHandler {
                onTapped: {
                    if (row.current) player.togglePlayPause()
                    else app.playAlbum(root.albumId, row.track.index, false)
                }
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: trackMenu.popup()
            }
            Menu {
                id: trackMenu
                MenuItem { text: "Play"; onTriggered: app.playAlbum(root.albumId, row.track.index, false) }
                MenuItem { text: "Play Next"; onTriggered: app.playTrackNext(row.track.trackId) }
                MenuItem { text: "Add to Queue"; onTriggered: app.addTrackToQueue(row.track.trackId) }
                MenuSeparator {}
                MenuItem { text: Qt.platform.os === "windows" ? "Show in Explorer" : "Show in Folder"; onTriggered: app.revealTrack(row.track.trackId) }
                MenuSeparator {}
                MenuItem { text: "Remove from Collection…"; onTriggered: { confirmTrack.track = row.track; confirmTrack.open() } }
            }
        }
    }

    Dialog {
        id: confirmTrack
        property var track: ({})
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Remove Track from Collection?"
        standardButtons: Dialog.Cancel
        Label {
            width: 380
            wrapMode: Text.Wrap
            text: "“" + (confirmTrack.track.title || "") + "” will be removed from this album and the playback queue. "
                  + (confirmTrack.track.isDemo ? "Its generated demo audio will be deleted." : "Your original audio file will stay on disk.")
        }
        footer: DialogButtonBox {
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { text: "Remove"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAccepted: app.removeTrack(track.trackId)
    }

    FileDialog {
        id: artworkPicker
        title: "Choose Artwork"
        nameFilters: ["Images (*.jpg *.jpeg *.png *.webp *.bmp *.tif *.tiff)"]
        onAccepted: app.replaceArtwork(root.albumId, selectedFile)
    }
}
