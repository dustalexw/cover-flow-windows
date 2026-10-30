import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Qt.labs.settings

// LyricsView.swift in its own window: synced lines follow playback, click to seek.
ApplicationWindow {
    id: root
    signal findLyrics()
    title: "Lyrics"
    width: 560; height: 700
    minimumWidth: 440; minimumHeight: 360
    color: Theme.panel
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
    property bool follow: true
    readonly property int active: app.lyricsSynced ? app.lyricsActiveIndex(player.position) : -1

    Settings { category: "lyricsWindow"; property alias x: root.x; property alias y: root.y; property alias width: root.width; property alias height: root.height; property alias follow: root.follow }
    Shortcut { sequence: "Space"; onActivated: player.togglePlayPause() }
    Shortcut { sequence: "Ctrl+W"; onActivated: root.close() }

    header: Rectangle {
        color: Theme.bar
        height: 78
        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12
            Image { Layout.preferredWidth: 54; Layout.preferredHeight: 54; source: app.currentArtwork; sourceSize: Qt.size(160, 160); fillMode: Image.PreserveAspectFit; asynchronous: true }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                MarqueeText { Layout.fillWidth: true; text: player.hasTrack ? player.title : "Nothing playing"; font.pixelSize: 18; font.weight: Font.DemiBold; color: Theme.text }
                Label { text: player.artist; color: Theme.secondary; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { text: app.lyricsSource; color: Theme.secondary; font.pixelSize: 11; opacity: 0.8; visible: text !== "" }
            }
            ToolButton {
                text: Glyphs.reload; font.family: Glyphs.family
                enabled: player.hasTrack
                onClicked: app.reloadLyrics()
                ToolTip.visible: hovered; ToolTip.text: "Reload lyrics"
            }
            Button {
                text: "Add Lyrics"
                enabled: player.hasTrack
                onClicked: addMenu.popup()
                Menu {
                    id: addMenu
                    MenuItem { text: "Find Lyrics Online…"; onTriggered: root.findLyrics() }
                    MenuItem { text: "Import LRC or Text…"; onTriggered: importDialog.open() }
                }
            }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: !player.hasTrack ? 0 : !app.hasLyrics ? 1 : app.lyricsSynced ? 2 : 3

        Label { text: "Choose a track to see its lyrics"; color: Theme.secondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 15 }

        Item {
        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 40, 400)
            spacing: 10
            Label { Layout.alignment: Qt.AlignHCenter; text: "No lyrics for this track"; color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
            Label {
                Layout.fillWidth: true
                text: app.lyricsError !== "" ? app.lyricsError : "Search for synced or plain lyrics, or import a lyric file from your computer."
                color: Theme.secondary; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Button { text: "Find Lyrics…"; highlighted: true; onClicked: root.findLyrics() }
                Button { text: "Import Lyrics…"; onClicked: importDialog.open() }
            }
        }
        }

        ListView {
            id: synced
            clip: true
            model: app.lyricLines
            spacing: 26
            topMargin: 90; bottomMargin: 90
            leftMargin: 28; rightMargin: 28
            ScrollBar.vertical: ScrollBar {}
            highlightRangeMode: ListView.ApplyRange
            preferredHighlightBegin: height / 2 - 20
            preferredHighlightEnd: height / 2 + 20
            highlightMoveDuration: Theme.reduceMotion ? 0 : 250
            currentIndex: root.follow ? root.active : -1
            delegate: Label {
                required property var modelData
                required property int index
                width: ListView.view.width - 56
                text: modelData.text
                wrapMode: Text.Wrap
                font.pixelSize: 24
                font.weight: Font.DemiBold
                color: index === root.active ? Theme.accent : Theme.secondary
                Behavior on color { ColorAnimation { duration: 150 } }
                MouseArea {
                    id: lineMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: player.seek(modelData.time)
                }
                ToolTip.visible: lineMouse.containsMouse
                ToolTip.text: "Jump to " + Theme.formatTime(modelData.time)
                ToolTip.delay: 800
            }
        }

        ScrollView {
            TextArea {
                readOnly: true
                text: app.lyricsPlain
                wrapMode: Text.Wrap
                font.pixelSize: 22
                font.weight: Font.Medium
                color: Theme.text
                leftPadding: 28; rightPadding: 28; topPadding: 28
                background: Item {}
            }
        }
    }

    footer: Rectangle {
        color: Theme.bar
        height: 72
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 2
            RowLayout {
                CheckBox {
                    visible: app.lyricsSynced
                    text: "Follow playback"
                    checked: root.follow
                    onToggled: root.follow = checked
                }
                Label { visible: !app.lyricsSynced && app.hasLyrics; text: "Untimed lyrics"; color: Theme.secondary; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                TransportControls { size: 26; showModes: false }
                Item { Layout.fillWidth: true }
            }
            SeekBar { Layout.fillWidth: true }
        }
    }

    FileDialog {
        id: importDialog
        title: "Import Lyrics for " + player.title
        currentFolder: app.currentTrackFolder()
        nameFilters: ["Lyrics (*.lrc *.txt *.lyric *.lyrics)"]
        onAccepted: app.importLyricsFile(selectedFile)
    }
}
