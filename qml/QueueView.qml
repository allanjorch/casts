import QtQuick
import QtQuick.Controls

// Up-next queue. Play over the art plays/resumes (the episode stays queued; it
// leaves only when it finishes). Click the text for details, drag the handle to
// reorder live, right-click for move / remove.
Item {
    id: page
    signal dismissRequested()
    signal clearRequested()

    function snippet(text) {
        if (!text || text.length === 0)
            return ""
        var flat = text.replace(/\s+/g, " ").trim()
        return flat.length <= 160 ? flat : flat.slice(0, 159).replace(/\s+\S*$/, "") + "…"
    }

    property real menuEpisode: 0
    property int menuIndex: -1
    readonly property real cover: 56 * theme.textScale

    function clock(seconds) {
        if (!seconds || seconds < 1)
            return ""
        var total = Math.floor(seconds)
        var hours = Math.floor(total / 3600)
        var minutes = Math.floor((total % 3600) / 60)
        var remain = total % 60
        function pad(value) { return value < 10 ? "0" + value : "" + value }
        if (hours > 0)
            return hours + ":" + pad(minutes) + ":" + pad(remain)
        return minutes + ":" + pad(remain)
    }

    Rectangle {
        anchors.fill: parent
        color: theme.background
    }
    // Swallow clicks/wheel so nothing underneath reacts.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 64 * theme.textScale
        z: 1

        IconButton {
            id: back
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            icon.name: "go-previous-symbolic"
            tip: "Back"
            onClicked: page.dismissRequested()
        }
        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            IconButton {
                visible: backend.queueCount > 0
                icon.source: "qrc:/icons/queue-clear-symbolic.svg"
                glyph: theme.dim
                tip: "Clear queue"
                onClicked: page.clearRequested()
            }
        }
        Column {
            anchors.left: back.right
            anchors.right: actions.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            Text {
                text: "Queue"
                color: theme.foreground
                font.pixelSize: theme.heading
                font.weight: Font.Normal
            }
            Text {
                text: backend.queueCount === 0 ? "Nothing queued"
                      : backend.queueCount === 1 ? "1 episode up next"
                      : backend.queueCount + " episodes up next"
                color: theme.dim
                font.pixelSize: theme.caption
                font.weight: Font.Normal
            }
        }
    }

    StatusNote {
        id: statusLine
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        height: implicitHeight * opacity
        font.pixelSize: theme.bodySmall
    }

    Column {
        anchors.centerIn: parent
        visible: backend.queueCount === 0
        spacing: 8
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "The queue is empty"
            color: theme.foreground
            font.pixelSize: theme.titleSize
            font.weight: Font.Normal
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Right-click an episode and choose Add to queue or Play next."
            color: theme.dim
            font.pixelSize: theme.caption
            font.weight: Font.Normal
        }
    }

    ListView {
        id: list
        anchors.top: statusLine.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: !dragging
        FastWheel { }
        model: backend.queueModel
        spacing: 2

        property bool dragging: false
        moveDisplaced: Transition {
            NumberAnimation { properties: "y"; duration: 90; easing.type: Easing.OutQuad }
        }

        delegate: Rectangle {
            id: row
            required property int index
            required property var model
            width: list.width
            height: page.cover + 20 * theme.textScale
            readonly property bool current: row.model.episodeId === backend.playerEpisodeId
                                            && backend.playerStatus !== "stopped"
            readonly property bool playing: row.model.episodeId === backend.playerEpisodeId
                                            && backend.playerStatus === "playing"
            color: hover.containsMouse || handle.pressed ? theme.selection
                 : row.current ? Qt.rgba(theme.selection.r, theme.selection.g, theme.selection.b, 0.5)
                 : "transparent"

            Rectangle {
                width: 3
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: theme.accent
                visible: handle.pressed || row.current
            }

            Item {
                id: handleWell
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 28 * theme.textScale
                height: parent.height
                z: 2
                IconButton {
                    anchors.centerIn: parent
                    icon.name: "list-drag-handle-symbolic"
                    glyph: theme.dim
                    background: Item {}
                    // Disabled so its hover cursor never fights the drag MouseArea.
                    enabled: false
                }
                MouseArea {
                    id: handle
                    anchors.fill: parent
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                    preventStealing: true
                    onPressed: list.dragging = true
                    onReleased: list.dragging = false
                    onCanceled: list.dragging = false
                    onPositionChanged: function(mouse) {
                        if (!pressed)
                            return
                        var p = mapToItem(list.contentItem, mouse.x, mouse.y)
                        var target = list.indexAt(10, p.y)
                        if (target < 0)
                            target = p.y < 0 ? 0 : list.count - 1
                        if (target !== row.index)
                            backend.moveInQueue(row.index, target)
                    }
                }
            }

            Item {
                id: art
                anchors.left: handleWell.right
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: page.cover
                height: page.cover
                z: 2
                Image {
                    id: artImage
                    anchors.fill: parent
                    source: row.model.cover || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: false
                    cache: true
                    sourceSize.width: page.cover * 2
                    sourceSize.height: page.cover * 2
                }
                Rectangle {
                    anchors.fill: parent
                    color: theme.selection
                    visible: artImage.status !== Image.Ready
                }
                Rectangle {
                    anchors.fill: parent
                    color: Qt.rgba(0, 0, 0, playButton.hovered || playButton.down ? 0.45 : 0.28)
                }
                IconButton {
                    id: playButton
                    anchors.fill: parent
                    icon.name: row.playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic"
                    icon.width: Math.max(16 * theme.textScale, page.cover * 0.375)
                    icon.height: icon.width
                    glyph: theme.foreground
                    tip: row.playing ? "Pause" : "Play"
                    background: Item {}
                    // playEpisode toggles the current one and resumes others from their saved spot.
                    onClicked: backend.playEpisode(row.model.episodeId)
                }
            }

            IconButton {
                id: removeButton
                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                z: 2
                icon.name: "window-close-symbolic"
                glyph: theme.dim
                tip: "Remove from queue"
                onClicked: backend.removeFromQueue(row.model.episodeId)
            }

            Column {
                anchors.left: art.right
                anchors.right: removeButton.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 14
                anchors.rightMargin: 12
                spacing: 4
                Text {
                    width: parent.width
                    text: row.model.title
                    color: theme.foreground
                    font.pixelSize: theme.titleSize
                    font.weight: Font.Normal
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    color: theme.dim
                    font.pixelSize: theme.caption
                    font.weight: Font.Normal
                    elide: Text.ElideRight
                    text: row.model.showTitle
                          + (row.model.duration > 0 ? " · " + page.clock(row.model.duration) : "")
                          + (!row.model.played && row.model.positionMs > 5000
                             ? " · In progress (" + page.clock(row.model.positionMs / 1000) + ")" : "")
                }
                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: page.snippet(row.model.description)
                    color: theme.dim
                    font.pixelSize: theme.caption
                    font.weight: Font.Normal
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
            }

            MouseArea {
                id: hover
                anchors.fill: parent
                anchors.leftMargin: handleWell.width + 8 + art.width + 8
                z: 0
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        page.menuEpisode = row.model.episodeId
                        page.menuIndex = row.index
                        rowMenu.popup()
                    } else {
                        win.openEpisodePage(row.model.episodeId)
                    }
                }
            }
        }
    }

    QuietScroll {
        view: list
        loadedCount: list.count
        anchors.top: list.top
        anchors.bottom: list.bottom
        anchors.right: parent.right
        anchors.rightMargin: 2
    }

    AppMenu {
        id: rowMenu
        AppMenuItem {
            text: "Play now"
            onTriggered: backend.playFromQueue(page.menuEpisode)
        }
        AppMenuItem {
            text: "Episode details"
            onTriggered: win.openEpisodePage(page.menuEpisode)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Move to top"
            enabled: page.menuIndex > 0
            onTriggered: backend.moveInQueue(page.menuIndex, 0)
        }
        AppMenuItem {
            text: "Move up"
            enabled: page.menuIndex > 0
            onTriggered: backend.moveInQueue(page.menuIndex, page.menuIndex - 1)
        }
        AppMenuItem {
            text: "Move down"
            enabled: page.menuIndex >= 0 && page.menuIndex < backend.queueCount - 1
            onTriggered: backend.moveInQueue(page.menuIndex, page.menuIndex + 1)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Remove from queue"
            onTriggered: backend.removeFromQueue(page.menuEpisode)
        }
    }
}
