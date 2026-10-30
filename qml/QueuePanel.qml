import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// QueueView.swift: Now Playing + Up Next, drag to reorder, right-click to move.
Rectangle {
    id: root
    color: Theme.panel
    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.hairline }

    readonly property var items: app.queueItems
    readonly property int current: player.currentIndex
    readonly property var upNext: items.filter(i => i.queueIndex > current)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10
        RowLayout {
            Label { text: "Queue"; font.family: Theme.serif; font.pixelSize: 18; color: Theme.text; Layout.fillWidth: true }
            Button {
                text: "Clear Up Next"
                enabled: root.upNext.length > 0
                flat: true
                onClicked: player.clearUpNext()
                contentItem: Label { text: parent.text; font.pixelSize: 12; color: parent.enabled ? Theme.accent : Theme.secondary }
                background: Item {}
            }
        }

        Label { text: "Now Playing"; font.pixelSize: 11; font.bold: true; color: Theme.secondary; visible: root.current >= 0 && root.items.length > 0 }
        Loader {
            Layout.fillWidth: true
            active: root.current >= 0 && root.current < root.items.length
            sourceComponent: QueueRow { item: root.items[root.current]; isCurrent: true }
        }

        Label { text: "Up Next · " + root.upNext.length; font.pixelSize: 11; font.bold: true; color: Theme.secondary; visible: root.upNext.length > 0 }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: root.upNext
            ScrollBar.vertical: ScrollBar {}
            delegate: QueueRow {
                required property var modelData
                item: modelData
                width: ListView.view.width
            }
        }
        Label {
            visible: root.upNext.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignTop
            text: "Nothing up next. Use Play Next or Add to Queue on any album or track."
            color: Theme.secondary
            font.pixelSize: 12
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "Drag to reorder, or right-click a track to move it. The queue and position are saved."
            color: Theme.secondary
            font.pixelSize: 11
            opacity: 0.8
        }
    }

    component QueueRow: Rectangle {
        id: row
        property var item: ({})
        property bool isCurrent: false
        implicitHeight: Math.max(46, col.implicitHeight + 10)
        radius: 6
        color: hover.hovered ? Theme.hover : "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.margins: 5
            spacing: 8
            Image {
                Layout.preferredWidth: 36; Layout.preferredHeight: 36
                source: row.item.artwork || ""
                sourceSize: Qt.size(160, 160)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                NowPlayingBars { anchors.centerIn: parent; visible: row.isCurrent; barHeight: 12; color: "white"; animating: player.playing }
            }
            ColumnLayout {
                id: col
                Layout.fillWidth: true
                spacing: 0
                Label { Layout.fillWidth: true; text: row.item.title || ""; wrapMode: Text.Wrap; font.pixelSize: 12; color: row.isCurrent ? Theme.accent : Theme.text }
                Label { Layout.fillWidth: true; text: row.item.artist || ""; elide: Text.ElideRight; font.pixelSize: 11; color: Theme.secondary }
            }
            Label { visible: !hover.hovered || row.isCurrent; text: row.item.duration || ""; font.pixelSize: 11; color: Theme.secondary }
            ToolButton {
                visible: hover.hovered && !row.isCurrent
                implicitWidth: 22; implicitHeight: 22
                focusPolicy: Qt.NoFocus
                onClicked: player.removeFromQueue(row.item.queueIndex)
                contentItem: Text { text: Glyphs.close; font.family: Glyphs.family; font.pixelSize: 9; color: Theme.secondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                Accessible.name: "Remove from Queue"
            }
        }
        HoverHandler { id: hover }
        TapHandler { onDoubleTapped: player.jumpTo(row.item.queueIndex) }
        TapHandler { acceptedButtons: Qt.RightButton; onTapped: menu.popup() }
        // Drag to reorder within Up Next.
        DragHandler {
            enabled: !row.isCurrent
            target: null
            yAxis.enabled: true; xAxis.enabled: false
            onActiveChanged: if (!active) {
                const steps = Math.round(translation.y / row.height)
                if (steps !== 0) {
                    const to = Math.max(root.current + 1, Math.min(root.items.length - 1, row.item.queueIndex + steps))
                    player.moveInQueue(row.item.queueIndex, to)
                }
            }
        }
        Menu {
            id: menu
            MenuItem { text: "Play Now"; onTriggered: player.jumpTo(row.item.queueIndex) }
            MenuItem { text: "Move to Top"; enabled: !row.isCurrent; onTriggered: player.moveInQueue(row.item.queueIndex, root.current + 1) }
            MenuItem { text: "Move Up"; enabled: !row.isCurrent && row.item.queueIndex > root.current + 1; onTriggered: player.moveInQueue(row.item.queueIndex, row.item.queueIndex - 1) }
            MenuItem { text: "Move Down"; enabled: !row.isCurrent && row.item.queueIndex < root.items.length - 1; onTriggered: player.moveInQueue(row.item.queueIndex, row.item.queueIndex + 1) }
            MenuItem { text: "Remove from Queue"; onTriggered: player.removeFromQueue(row.item.queueIndex) }
        }
    }
}
