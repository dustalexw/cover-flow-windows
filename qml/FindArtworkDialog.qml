import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Find Album Artwork: MusicBrainz search, Cover Art Archive preview (up to 1200 px).
Dialog {
    id: root
    property string albumId
    property int selected: -1
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(740, parent.width - 40)
    height: Math.min(540, parent.height - 40)
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { color: Theme.panel; radius: 10; border.color: Theme.hairline }

    function start(id) {
        albumId = id
        const info = app.albumInfo(id)
        titleField.text = info.title || ""
        artistField.text = info.artist === "Various Artists" ? "" : (info.artist || "")
        selected = -1
        app.clearLookup()
        open()
        search()
    }
    function search() { selected = -1; app.searchArtwork(titleField.text, artistField.text) }
    Connections { target: app; function onArtworkSaved() { if (root.opened) root.close() } }
    onClosed: app.clearLookup()

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Label { text: "Find Album Artwork"; font.pixelSize: 20; font.bold: true; color: Theme.text; Layout.fillWidth: true }
            Button { text: "Close"; onClicked: root.close() }
        }
        RowLayout {
            TextField { id: titleField; Layout.fillWidth: true; placeholderText: "Album title"; onAccepted: root.search(); selectByMouse: true }
            TextField { id: artistField; Layout.fillWidth: true; placeholderText: "Artist"; onAccepted: root.search(); selectByMouse: true }
            Button { text: "Search"; enabled: !app.lookupBusy && titleField.text.trim() !== ""; onClicked: root.search() }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: app.artworkResults
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    highlighted: root.selected === index
                    onClicked: { root.selected = index; app.previewArtwork(index) }
                    contentItem: ColumnLayout {
                        spacing: 1
                        Label { text: modelData.title; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { text: modelData.artist + (modelData.year ? " · " + modelData.year : ""); color: Theme.secondary; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                }
            }
            Rectangle {
                Layout.preferredWidth: 220; Layout.preferredHeight: 220
                Layout.alignment: Qt.AlignTop
                color: Theme.fieldBg; radius: 6
                Image { anchors.fill: parent; source: app.artworkPreview; fillMode: Image.PreserveAspectFit; asynchronous: true }
                BusyIndicator { anchors.centerIn: parent; running: app.lookupBusy; visible: running }
            }
        }
        Label { visible: app.lookupMessage !== "" && !app.lookupBusy; text: app.lookupMessage; color: Theme.secondary; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            Label {
                Layout.fillWidth: true
                text: "Album search by MusicBrainz. Covers from the Cover Art Archive, up to 1200 pixels. Availability varies by edition."
                color: Theme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap
            }
            Button { text: "Use Artwork"; highlighted: true; enabled: app.artworkPreview !== ""; onClicked: app.useArtworkPreview(root.albumId) }
        }
    }
}
