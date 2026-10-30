import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Edit Album Info: changes Library.json only, never the audio files.
Dialog {
    id: root
    property string albumId
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, parent.width - 40)
    modal: true
    title: "Edit Album Info"
    standardButtons: Dialog.Save | Dialog.Cancel
    background: Rectangle { color: Theme.panel; radius: 10; border.color: Theme.hairline }

    function start(id) {
        albumId = id
        const info = app.albumInfo(id)
        titleField.text = info.title || ""
        artistField.text = info.artist || ""
        yearField.text = info.year || ""
        genreField.text = info.genre || ""
        open()
    }
    onAccepted: app.updateAlbumInfo(albumId, titleField.text, artistField.text, yearField.text, genreField.text)

    GridLayout {
        anchors.fill: parent
        columns: 2
        columnSpacing: 12
        rowSpacing: 10
        Label { text: "Album"; color: Theme.text }
        TextField { id: titleField; Layout.fillWidth: true; selectByMouse: true }
        Label { text: "Artist"; color: Theme.text }
        TextField { id: artistField; Layout.fillWidth: true; selectByMouse: true }
        Label { text: "Year"; color: Theme.text }
        TextField { id: yearField; Layout.preferredWidth: 90; selectByMouse: true; validator: IntValidator { bottom: 0; top: 9999 } }
        Label { text: "Genre"; color: Theme.text }
        TextField { id: genreField; Layout.fillWidth: true; selectByMouse: true }
    }
}
