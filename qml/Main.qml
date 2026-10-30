import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Shapes

ApplicationWindow {
    id: window
    title: "Cover Flow"
    width: 1220; height: 820
    minimumWidth: 720; minimumHeight: 540
    visible: true
    color: Theme.base
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

    // ----- selection (AlbumSelection.swift) -----
    property var selection: ({})
    property string anchorId: ""
    readonly property int selectionCount: Object.keys(selection).length
    function setSelection(ids) { const s = {}; for (const id of ids) s[id] = true; selection = s }
    function clearSelection() { selection = ({}); anchorId = "" }
    function rangeIds(fromId, toId) {
        const a = app.albums.indexOf(fromId), b = app.albums.indexOf(toId)
        if (a < 0 || b < 0) return [toId]
        const out = []
        for (let i = Math.min(a, b); i <= Math.max(a, b); ++i) out.push(app.albums.idAt(i))
        return out
    }
    function activate(albumId, modifiers) {
        const shift = modifiers & Qt.ShiftModifier, toggle = modifiers & Qt.ControlModifier
        if (shift) {
            const range = rangeIds(anchorId || albumId, albumId)
            if (toggle) { const s = Object.assign({}, selection); for (const id of range) s[id] = true; selection = s }
            else setSelection(range)
            if (!anchorId) anchorId = albumId
        } else if (toggle) {
            const s = Object.assign({}, selection)
            if (s[albumId]) delete s[albumId]; else s[albumId] = true
            selection = s
            anchorId = albumId
        } else {
            setSelection([albumId])
            anchorId = albumId
            app.browsedAlbumId = albumId
            detail.openAlbum(albumId)
        }
    }
    function selectedIds() { return app.orderedSelection(Object.keys(selection)) }
    function reveal(albumId) {
        if (!albumId) return
        if (app.albums.indexOf(albumId) < 0) app.albums.search = ""
        app.browsedAlbumId = albumId
        if (app.browseMode === 0) coverFlow.reveal(albumId); else grid.reveal(albumId)
    }
    // Keep only visible albums selected when search/sort changes.
    Connections {
        target: app.albums
        function onModelReset() {
            const s = {}
            for (const id of Object.keys(window.selection)) if (app.albums.indexOf(id) >= 0) s[id] = true
            window.selection = s
        }
    }

    // ----- backdrop: base + radial tint of the browsed (or playing) album -----
    background: Rectangle {
        color: Theme.base
        Shape {
            anchors.fill: parent
            opacity: Theme.dark ? 0.42 : 0.30
            ShapePath {
                strokeWidth: -1
                fillGradient: RadialGradient {
                    centerX: window.width * 0.5; centerY: window.height * 0.18
                    centerRadius: 900; focalX: centerX; focalY: centerY
                    GradientStop { position: 0; color: tint.color }
                    GradientStop { position: 1; color: "transparent" }
                }
                startX: 0; startY: 0
                PathLine { x: window.width; y: 0 }
                PathLine { x: window.width; y: window.height }
                PathLine { x: 0; y: window.height }
                PathLine { x: 0; y: 0 }
            }
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(tint.color.r, tint.color.g, tint.color.b, Theme.dark ? 0.10 : 0.08) }
                GradientStop { position: 1; color: "transparent" }
            }
        }
        QtObject {
            id: tint
            readonly property string albumId: app.browsedAlbumId || player.currentAlbumId
            property color color: albumId ? app.backdropTint(albumId, Theme.dark) : Theme.base
            Behavior on color { ColorAnimation { duration: 900; easing.type: Easing.InOutQuad } }
        }
    }

    // ----- menus & shortcuts -----
    menuBar: MenuBar {
        Menu {
            title: "&File"
            Action { text: "Import &Files…"; shortcut: "Ctrl+O"; onTriggered: filePicker.open() }
            Action { text: "Import F&older…"; shortcut: "Ctrl+Shift+O"; onTriggered: folderPicker.open() }
            MenuSeparator {}
            Action { text: "E&xit"; shortcut: StandardKey.Quit; onTriggered: Qt.quit() }
        }
        Menu {
            title: "&Edit"
            Action { text: "&Find"; shortcut: StandardKey.Find; onTriggered: { searchField.forceActiveFocus(); searchField.selectAll() } }
            Action { text: "Select &All"; shortcut: StandardKey.SelectAll; enabled: !searchField.activeFocus; onTriggered: { const ids = []; for (let i = 0; i < app.albums.count; ++i) ids.push(app.albums.idAt(i)); window.setSelection(ids) } }
            Action { text: "Remove Selected Albums…"; enabled: window.selectionCount > 0; onTriggered: confirmRemove.ask(window.selectedIds()) }
        }
        Menu {
            title: "&View"
            Action { text: "Cover Flow"; shortcut: "Ctrl+1"; checkable: true; checked: app.browseMode === 0; onTriggered: app.browseMode = 0 }
            Action { text: "Grid"; shortcut: "Ctrl+2"; checkable: true; checked: app.browseMode === 1; onTriggered: app.browseMode = 1 }
            MenuSeparator {}
            Action { text: "Lyrics"; shortcut: "Ctrl+4"; onTriggered: lyricsWindow.showLyrics() }
            Action { text: "Show Queue"; shortcut: "Ctrl+U"; checkable: true; checked: app.showQueue; onTriggered: app.showQueue = !app.showQueue }
            Action { text: app.albumViewOpen ? "Close Album View" : "Open Album View"; shortcut: "Ctrl+Shift+A"; onTriggered: window.toggleAlbumView() }
            Menu {
                title: "Album View Size"
                Action { text: "Compact"; shortcut: "Ctrl+Alt+1"; onTriggered: { window.showAlbumView(); albumView.applyPreset("compact") } }
                Action { text: "Large"; shortcut: "Ctrl+Alt+2"; onTriggered: { window.showAlbumView(); albumView.applyPreset("large") } }
                Action { text: "Immersive"; shortcut: "Ctrl+Alt+3"; onTriggered: { window.showAlbumView(); albumView.applyPreset("immersive") } }
            }
            MenuSeparator {}
            Menu {
                title: "Sort By"
                Repeater {
                    model: ["Artist", "Title", "Recently Added", "Recently Played"]
                    MenuItem { required property int index; required property string modelData; text: modelData; checkable: true; checked: app.albums.sortOrder === index; onTriggered: app.albums.sortOrder = index }
                }
            }
            Menu {
                title: "Theme"
                Repeater {
                    model: ["Dark", "Light", "Match System"]
                    MenuItem { required property int index; required property string modelData; text: modelData; checkable: true; checked: app.theme === index; onTriggered: app.theme = index }
                }
            }
            Action { text: "Reveal Playing Album"; shortcut: "Ctrl+L"; enabled: player.hasTrack; onTriggered: window.reveal(player.currentAlbumId) }
        }
        Menu {
            title: "&Controls"
            Action { text: player.playing ? "Pause" : "Play"; onTriggered: player.togglePlayPause() }
            Action { text: "Next"; shortcut: "Ctrl+Right"; onTriggered: player.next() }
            Action { text: "Previous"; shortcut: "Ctrl+Left"; onTriggered: player.previous() }
            Action { text: "Skip Forward 10 s"; shortcut: "Ctrl+Alt+Right"; onTriggered: player.skip(10) }
            Action { text: "Skip Back 10 s"; shortcut: "Ctrl+Alt+Left"; onTriggered: player.skip(-10) }
            MenuSeparator {}
            Action { text: "Volume Up"; shortcut: "Ctrl+Up"; onTriggered: player.volumeStep(0.1) }
            Action { text: "Volume Down"; shortcut: "Ctrl+Down"; onTriggered: player.volumeStep(-0.1) }
            MenuSeparator {}
            Action { text: "Shuffle"; checkable: true; checked: player.shuffle; onTriggered: player.shuffle = !player.shuffle }
            Action { text: ["Repeat Off", "Repeat All", "Repeat One"][player.repeatMode]; onTriggered: player.cycleRepeat() }
        }
        Menu {
            title: "&Help"
            Action { text: "About Cover Flow…"; onTriggered: about.open() }
        }
    }
    Shortcut { sequences: ["Space"]; enabled: !searchField.activeFocus && !detail.opened; onActivated: player.togglePlayPause() }
    Shortcut { sequences: ["Media Play", "Media Pause", "Toggle Media Play/Pause"]; context: Qt.ApplicationShortcut; onActivated: player.togglePlayPause() }
    Shortcut { sequence: "Media Next"; context: Qt.ApplicationShortcut; onActivated: player.next() }
    Shortcut { sequence: "Media Previous"; context: Qt.ApplicationShortcut; onActivated: player.previous() }
    Shortcut { sequence: "Esc"; enabled: window.selectionCount > 0 && !detail.opened; onActivated: window.clearSelection() }

    // ----- toolbar -----
    header: ToolBar {
        background: Item {}
        height: 48
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12; anchors.rightMargin: 12
            spacing: 8
            Row {
                spacing: 0
                Repeater {
                    model: [[Glyphs.coverFlow, "Cover Flow"], [Glyphs.grid, "Grid"]]
                    Button {
                        required property int index
                        required property var modelData
                        checkable: true
                        checked: app.browseMode === index
                        onClicked: app.browseMode = index
                        focusPolicy: Qt.NoFocus
                        implicitWidth: 38; implicitHeight: 28
                        contentItem: Text { text: modelData[0]; font.family: Glyphs.family; font.pixelSize: 14; color: parent.checked ? Theme.base : Theme.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { color: parent.checked ? Theme.accent : Theme.fieldBg; radius: 6 }
                        ToolTip.visible: hovered; ToolTip.text: modelData[1]
                        Accessible.name: modelData[1]
                    }
                }
            }
            Label {
                text: app.albums.search ? app.albums.count + " of " + app.albums.totalCount + " albums" : (app.albums.totalCount === 1 ? "1 album" : app.albums.totalCount + " albums")
                color: Theme.secondary
                font.pixelSize: 12
                leftPadding: 8
            }
            Item { Layout.fillWidth: true }
            RowLayout {
                visible: app.importStatus !== ""
                BusyIndicator { implicitWidth: 18; implicitHeight: 18; running: parent.visible }
                Label { text: app.importStatus; color: Theme.secondary; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.maximumWidth: 360 }
            }
            Item { Layout.fillWidth: true }
            TextField {
                id: searchField
                placeholderText: "Search music"
                Layout.preferredWidth: 210
                implicitHeight: 28
                selectByMouse: true
                text: app.albums.search
                onTextEdited: app.albums.search = text
                Keys.onEscapePressed: { text = ""; app.albums.search = ""; focus = false }
                leftPadding: 26
                Text { x: 8; anchors.verticalCenter: parent.verticalCenter; text: Glyphs.search; font.family: Glyphs.family; font.pixelSize: 12; color: Theme.secondary }
                background: Rectangle { radius: 6; color: Theme.fieldBg; border.color: searchField.activeFocus ? Theme.accent : "transparent" }
            }
            ToolButton { text: Glyphs.lyrics; font.family: Glyphs.family; focusPolicy: Qt.NoFocus; onClicked: lyricsWindow.showLyrics(); ToolTip.visible: hovered; ToolTip.text: "Lyrics" }
            ToolButton { text: Glyphs.queue; font.family: Glyphs.family; checkable: true; checked: app.showQueue; focusPolicy: Qt.NoFocus; onClicked: app.showQueue = !app.showQueue; ToolTip.visible: hovered; ToolTip.text: "Queue" }
            ToolButton {
                text: Glyphs.more; font.family: Glyphs.family; focusPolicy: Qt.NoFocus
                onClicked: moreMenu.popup()
                Menu {
                    id: moreMenu
                    MenuItem { text: "Import Files…"; onTriggered: filePicker.open() }
                    MenuItem { text: "Import Folder…"; onTriggered: folderPicker.open() }
                    MenuSeparator {}
                    Repeater {
                        model: ["Artist", "Title", "Recently Added", "Recently Played"]
                        MenuItem { required property int index; required property string modelData; text: "Sort by " + modelData; checkable: true; checked: app.albums.sortOrder === index; onTriggered: app.albums.sortOrder = index }
                    }
                }
            }
        }
    }

    // ----- content -----
    RowLayout {
        anchors.fill: parent
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                StackLayout {
                    anchors.fill: parent
                    currentIndex: app.albums.totalCount === 0 ? 2 : app.albums.count === 0 ? 3 : app.browseMode
                    CoverFlowView {
                        id: coverFlow
                        albums: app.albums
                        selectedIds: window.selection
                        focus: app.browseMode === 0
                        onAlbumActivated: (id, mods) => window.activate(id, mods)
                        onOpenTracks: (id) => detail.openAlbum(id)
                        onExtendSelection: (id) => window.setSelection(window.rangeIds(window.anchorId || id, id))
                    }
                    AlbumGridView {
                        id: grid
                        albums: app.albums
                        selectedIds: window.selection
                        focus: app.browseMode === 1
                        onAlbumActivated: (id, mods) => window.activate(id, mods)
                        onOpenTracks: (id) => detail.openAlbum(id)
                        onMarqueeSelect: (ids, additive) => {
                            if (additive) { const s = Object.assign({}, window.selection); for (const id of ids) s[id] = true; window.selection = s }
                            else window.setSelection(ids)
                        }
                        onContextMenuRequested: (id) => { if (!window.selection[id]) window.setSelection([id]); albumMenu.albumIds = window.selectedIds(); albumMenu.popup() }
                    }
                    // Empty collection.
                    ColumnLayout {
                        spacing: 14
                        Item { Layout.fillHeight: true }
                        Row {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: -30
                            Repeater {
                                model: 3
                                Rectangle {
                                    required property int index
                                    width: 110; height: 110; radius: 4
                                    color: Qt.hsva(0.08 + index * 0.25, 0.32, 0.36, 1)
                                    rotation: (index - 1) * 10
                                    border.color: Theme.hairline
                                }
                            }
                        }
                        Label { Layout.alignment: Qt.AlignHCenter; text: "Your record collection is empty"; font.family: Theme.serif; font.pixelSize: 24; color: Theme.text }
                        Label {
                            Layout.alignment: Qt.AlignHCenter; Layout.maximumWidth: 460
                            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                            text: "Import audio files or folders to browse them by their covers. Cover Flow reads them where they are — nothing is copied or moved."
                            color: Theme.secondary
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            Button { text: "Import Folder…"; highlighted: true; onClicked: folderPicker.open() }
                            Button { text: "Import Files…"; onClicked: filePicker.open() }
                        }
                        Label { Layout.alignment: Qt.AlignHCenter; text: "You can also drag files or folders into this window."; color: Theme.secondary; font.pixelSize: 11 }
                        Item { Layout.fillHeight: true }
                    }
                    // No search results.
                    ColumnLayout {
                        spacing: 10
                        Item { Layout.fillHeight: true }
                        Label { Layout.alignment: Qt.AlignHCenter; text: "No albums match “" + app.albums.search + "”"; font.pixelSize: 20; color: Theme.text }
                        Label { Layout.alignment: Qt.AlignHCenter; text: "Search looks at album titles, artists and track names."; color: Theme.secondary }
                        Button { Layout.alignment: Qt.AlignHCenter; text: "Clear Search"; onClicked: { app.albums.search = ""; searchField.text = "" } }
                        Item { Layout.fillHeight: true }
                    }
                }

                // Banner.
                Rectangle {
                    id: banner
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: app.bannerText !== "" ? 12 : -height - 20
                    visible: y > -height - 20
                    Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
                    width: Math.min(560, parent.width - 40)
                    height: bannerCol.implicitHeight + 20
                    radius: 12
                    color: Theme.panel
                    border.color: app.bannerTone === "error" ? Theme.danger : app.bannerTone === "warning" ? "#d99a2b" : Theme.hairline
                    ColumnLayout {
                        id: bannerCol
                        anchors.fill: parent
                        anchors.margins: 10
                        anchors.rightMargin: 34
                        spacing: 2
                        Label { Layout.fillWidth: true; text: app.bannerText; wrapMode: Text.Wrap; color: app.bannerTone === "error" ? Theme.danger : Theme.text; font.weight: Font.Medium }
                        Label { Layout.fillWidth: true; visible: text !== ""; text: app.bannerDetails; wrapMode: Text.Wrap; color: Theme.secondary; font.pixelSize: 12 }
                    }
                    ToolButton {
                        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 4
                        text: Glyphs.close; font.family: Glyphs.family; font.pixelSize: 9
                        onClicked: app.dismissBanner()
                    }
                }

                // Drop target.
                DropArea {
                    id: drop
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: (d) => { if (d.hasUrls) app.importUrls(d.urls) }
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: drop.containsDrag
                    color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.6)
                    radius: 14
                    border.color: Theme.accent
                    border.width: 2
                    Label { anchors.centerIn: parent; text: "Drop to add to your collection"; color: Theme.accent; font.pixelSize: 18 }
                }
            }

            // Selection bar.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                visible: window.selectionCount > 1
                color: Theme.panel
                Rectangle { width: parent.width; height: 1; color: Theme.hairline }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14; anchors.rightMargin: 14
                    Label { text: window.selectionCount + " selected"; color: Theme.text; font.weight: Font.Medium }
                    Item { Layout.fillWidth: true }
                    Button { text: "Play"; onClicked: app.playAlbums(window.selectedIds()) }
                    Button { text: "Play Next"; onClicked: app.playNextAlbums(window.selectedIds()) }
                    Button { text: "Add to Queue"; onClicked: app.addAlbumsToQueue(window.selectedIds()) }
                    Button { text: "Remove…"; onClicked: confirmRemove.ask(window.selectedIds()) }
                    Button { text: "Clear Selection"; flat: true; onClicked: window.clearSelection() }
                }
            }

            PlaybackBar {
                Layout.fillWidth: true
                onRevealAlbum: (id) => window.reveal(id)
                onToggleAlbumView: window.toggleAlbumView()
                onOpenLyrics: lyricsWindow.showLyrics()
            }
        }
        QueuePanel {
            visible: app.showQueue
            Layout.preferredWidth: 300
            Layout.minimumWidth: 260
            Layout.maximumWidth: 420
            Layout.fillHeight: true
        }
    }

    // ----- album context menu (grid) -----
    Menu {
        id: albumMenu
        property var albumIds: []
        readonly property bool many: albumIds.length > 1
        MenuItem { text: albumMenu.many ? "Play " + albumMenu.albumIds.length + " Albums" : "Play"; onTriggered: app.playAlbums(albumMenu.albumIds) }
        MenuItem { text: "Shuffle"; onTriggered: { player.shuffle = true; app.playAlbums(albumMenu.albumIds) } }
        MenuItem { text: "Play Next"; onTriggered: app.playNextAlbums(albumMenu.albumIds) }
        MenuItem { text: "Add to Queue"; onTriggered: app.addAlbumsToQueue(albumMenu.albumIds) }
        MenuSeparator {}
        MenuItem { text: "Show Tracks"; enabled: !albumMenu.many; onTriggered: detail.openAlbum(albumMenu.albumIds[0]) }
        MenuItem { text: "Edit Album Info…"; enabled: !albumMenu.many; onTriggered: editDialog.start(albumMenu.albumIds[0]) }
        MenuItem { text: "Find Artwork Online…"; enabled: !albumMenu.many; onTriggered: artworkDialog.start(albumMenu.albumIds[0]) }
        MenuSeparator {}
        MenuItem { text: albumMenu.many ? "Remove " + albumMenu.albumIds.length + " Albums…" : "Remove from Collection…"; onTriggered: confirmRemove.ask(albumMenu.albumIds) }
    }

    AlbumDetail {
        id: detail
        onEditRequested: (id) => editDialog.start(id)
        onFindArtworkRequested: (id) => artworkDialog.start(id)
        onRemoveRequested: (ids) => confirmRemove.ask(ids)
    }
    EditAlbumDialog { id: editDialog }
    FindArtworkDialog { id: artworkDialog }
    FindLyricsDialog { id: findLyrics }

    Dialog {
        id: confirmRemove
        property var albumIds: []
        function ask(ids) { albumIds = ids; open() }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: albumIds.length > 1 ? "Remove " + albumIds.length + " albums from your collection?" : "Remove this album from your collection?"
        Label {
            width: 400
            wrapMode: Text.Wrap
            text: "Your imported audio files stay on disk. Generated demo audio belonging to these albums will be deleted."
        }
        footer: DialogButtonBox {
            Button { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { text: confirmRemove.albumIds.length > 1 ? "Remove Selected Albums" : "Remove"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAccepted: { app.removeAlbums(albumIds); window.clearSelection(); detail.close() }
    }

    Dialog {
        id: about
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "About Cover Flow"
        standardButtons: Dialog.Close
        ColumnLayout {
            spacing: 6
            Image { Layout.alignment: Qt.AlignHCenter; source: "qrc:/qt/qml/CoverFlow/resources/icon-256.png"; sourceSize: Qt.size(96, 96) }
            Label { Layout.alignment: Qt.AlignHCenter; text: "Cover Flow for Windows " + app.version; font.pixelSize: 16; font.bold: true }
            Label { Layout.alignment: Qt.AlignHCenter; text: "Browse your music by its covers."; color: Theme.secondary }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "<a href='https://github.com/dustalexw/cover-flow'>Source code and releases</a>"
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
            Label { Layout.alignment: Qt.AlignHCenter; text: "Uses Qt, FFmpeg (LGPL) and TagLib (LGPL). Lyrics by LRCLIB."; color: Theme.secondary; font.pixelSize: 11 }
        }
    }

    FileDialog {
        id: filePicker
        title: "Import Music"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Audio files (*.mp3 *.m4a *.m4b *.aac *.flac *.wav *.aif *.aiff *.ogg *.oga *.opus *.ape *.wv *.wma *.dsf *.dff *.mka *.webm *.mpc *.tta *.tak *.alac *.caf *.mp4)", "All files (*)"]
        onAccepted: app.importUrls(selectedFiles)
    }
    FolderDialog {
        id: folderPicker
        title: "Import a Music Folder"
        onAccepted: app.importUrls([selectedFolder])
    }

    // ----- secondary windows -----
    LyricsPanel {
        id: lyricsWindow
        visible: false
        function showLyrics() { show(); raise(); requestActivate() }
        onFindLyrics: { window.raise(); window.requestActivate(); findLyrics.start() }
    }
    AlbumViewWindow {
        id: albumView
        visible: false
        onVisibleChanged: app.albumViewOpen = visible
        onRevealAlbum: (id) => { window.raise(); window.requestActivate(); window.reveal(id) }
        onOpenLyrics: lyricsWindow.showLyrics()
    }
    function showAlbumView() { albumView.show(); albumView.raise() }
    function toggleAlbumView() { if (albumView.visible) albumView.close(); else showAlbumView() }
    Component.onCompleted: {
        if (app.albumViewOpen) Qt.callLater(showAlbumView)
        if (app.debugState) Qt.callLater(applyDebugState, app.debugState)
    }
    // Developer aid for headless screenshots (COVERFLOW_DEBUG_STATE).
    function applyDebugState(state) {
        const m = /album=(\d+)/.exec(state)
        const first = app.albums.idAt(Math.min(m ? parseInt(m[1]) : 4, app.albums.count - 1))
        if (state.indexOf("grid") >= 0) app.browseMode = 1
        if (state.indexOf("flow") >= 0) app.browseMode = 0
        if (state.indexOf("browse") >= 0) coverFlow.snapTo(4)
        if (state.indexOf("play") >= 0) app.playAlbum(first, 0, false)
        if (state.indexOf("queue") >= 0) app.showQueue = true
        if (state.indexOf("select") >= 0) window.setSelection([app.albums.idAt(1), app.albums.idAt(2), app.albums.idAt(3)])
        if (state.indexOf("detail") >= 0) detail.openAlbum(first)
        if (state.indexOf("lyrics") >= 0) lyricsWindow.showLyrics()
        if (state.indexOf("albumview") >= 0) showAlbumView()
        if (state.indexOf("light") >= 0) app.theme = 1
    }
}
