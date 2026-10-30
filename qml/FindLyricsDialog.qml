import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// MusicDownloadViews.swift LyricsDownloadView: search LRCLIB, preview, save.
Dialog {
    id: root
    property int selected: -1
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(620, parent.width - 40)
    height: Math.min(640, parent.height - 40)
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { color: Theme.panel; radius: 10; border.color: Theme.hairline }

    function start() {
        titleField.text = player.title
        artistField.text = player.artist
        selected = -1
        app.clearLookup()
        open()
        if (titleField.text) search()
    }
    function search() {
        selected = -1
        app.searchLyrics(titleField.text, artistField.text)
    }
    Connections { target: app; function onLyricsSaved() { root.close() } }
    onClosed: app.clearLookup()

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Label { text: "Find Lyrics"; font.pixelSize: 20; font.bold: true; color: Theme.text; Layout.fillWidth: true }
            Button { text: "Close"; onClicked: root.close() }
        }
        RowLayout {
            TextField { id: titleField; Layout.fillWidth: true; Layout.preferredWidth: 3; placeholderText: "Track title"; onAccepted: root.search(); selectByMouse: true }
            TextField { id: artistField; Layout.fillWidth: true; Layout.preferredWidth: 3; placeholderText: "Artist"; onAccepted: root.search(); selectByMouse: true }
            Button { text: "Search"; enabled: !app.lookupBusy && titleField.text.trim() !== ""; onClicked: root.search() }
        }
        Label {
            Layout.fillWidth: true
            text: "Search LRCLIB for " + (player.title || "this track") + ". Choose the recording and timing that fit your track."
            color: Theme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap
        }
        ListView {
            id: results
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 2
            clip: true
            model: app.lyricResults
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: ListView.view.width
                highlighted: root.selected === index
                onClicked: root.selected = index
                contentItem: ColumnLayout {
                    spacing: 1
                    Label { text: modelData.track; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { text: modelData.detail; color: Theme.secondary; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { text: modelData.info; color: modelData.synced ? Theme.accent : Theme.secondary; font.pixelSize: 11 }
                }
            }
            BusyIndicator { anchors.centerIn: parent; running: app.lookupBusy; visible: running }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 1
            visible: root.selected >= 0
            TextArea {
                readOnly: true
                text: root.selected >= 0 ? app.lyricPreview(root.selected) : ""
                color: Theme.text
                font.pixelSize: 12
                background: Rectangle { color: Theme.fieldBg; radius: 6 }
            }
        }
        Label {
            visible: app.lookupMessage !== "" && !app.lookupBusy
            Layout.fillWidth: true
            text: app.lookupMessage
            color: Theme.secondary
            wrapMode: Text.Wrap
        }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button {
                text: root.selected >= 0 && app.lyricResults[root.selected] && app.lyricResults[root.selected].synced ? "Use Synced Lyrics" : "Use Lyrics"
                enabled: root.selected >= 0
                highlighted: true
                onClicked: app.useLyricResult(root.selected)
            }
        }
        Label {
            Layout.fillWidth: true
            text: "Lyrics provided by LRCLIB. Searches send the title and artist; your audio stays on this device."
            color: Theme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap
        }
    }
}
