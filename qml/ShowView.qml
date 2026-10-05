import QtQuick
import QtQuick.Controls

Item {
    id: page
    signal markAllRequested()
    signal removeRequested()

    property var markedEpisode: 0
    property bool markedPlayed: false
    // Which show this page is holding. Main keeps one page per visited show.
    property double showId: 0
    property var episodeModel: null
    // Same five list-cover steps as ShelfView. 0 matches Shelf's original 44px.
    readonly property var listCoverSteps: [44, 64, 88, 120, 160]
    readonly property int listCoverBase: listCoverSteps[Math.max(0, Math.min(backend.episodeListSize, listCoverSteps.length - 1))]
    readonly property real listCover: listCoverBase * theme.textScale
    readonly property real listRowHeight: listCover + 20 * theme.textScale
    readonly property int descriptionLimit: 140

    property real zoomPending: 0

    function zoomIn() {
        backend.setEpisodeListSize(backend.episodeListSize + 1)
    }

    function zoomOut() {
        backend.setEpisodeListSize(backend.episodeListSize - 1)
    }

    function takeWheel(wheel) {
        if (!(wheel.modifiers & Qt.ControlModifier)) {
            wheel.accepted = false
            return
        }
        var delta = wheel.angleDelta.y
        if (delta === 0 && wheel.pixelDelta.y !== 0)
            delta = wheel.pixelDelta.y > 0 ? 120 : -120
        applyZoomDelta(delta)
        wheel.accepted = true
    }

    function applyZoomDelta(delta) {
        zoomPending += delta
        while (zoomPending >= 120) {
            zoomPending -= 120
            zoomIn()
        }
        while (zoomPending <= -120) {
            zoomPending += 120
            zoomOut()
        }
    }

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

    function day(unix) {
        if (!unix)
            return ""
        return Qt.formatDateTime(new Date(unix * 1000), "d MMM yyyy")
    }

    function snippet(text) {
        if (!text || text.length === 0)
            return ""
        var flat = text.replace(/\s+/g, " ").trim()
        if (flat.length <= descriptionLimit)
            return flat
        return flat.slice(0, descriptionLimit - 1).replace(/\s+\S*$/, "") + "…"
    }

    function openMarkMenu(episodeId, played) {
        markedEpisode = episodeId
        markedPlayed = played
        markMenu.popup()
    }

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 64 * theme.textScale
        z: 1

        Rectangle {
            anchors.fill: parent
            color: theme.background
        }

        IconButton {
            id: back
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            icon.name: "go-previous-symbolic"
            tip: "Back"
            onClicked: backend.closeShow()
        }
        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            IconButton {
                icon.name: backend.episodeShowAll ? "view-reveal-symbolic" : "view-conceal-symbolic"
                tip: backend.episodeShowAll ? "Show unplayed only" : "Show all"
                onClicked: backend.setEpisodeShowAll(!backend.episodeShowAll)
            }
            IconButton {
                icon.name: "view-refresh-symbolic"
                tip: "Refresh  ·  Ctrl+click reloads artwork"
                property bool reloadArtwork: false
                onPressedChanged: {
                    if (pressed)
                        reloadArtwork = backend.controlHeld()
                }
                onClicked: backend.refreshOpenShow(reloadArtwork)
            }
            Item {
                width: 16
                height: 1
            }
            IconButton {
                icon.name: "check-plain-symbolic"
                tip: "Mark all as played"
                onClicked: page.markAllRequested()
            }
            IconButton {
                icon.name: "list-remove-symbolic"
                glyph: theme.dim
                tip: "Remove"
                onClicked: page.removeRequested()
            }
        }
        Column {
            anchors.left: back.right
            anchors.right: actions.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            Text {
                width: parent.width
                text: backend.openShowTitle
                color: theme.foreground
                font.pixelSize: theme.heading
                font.weight: Font.Normal
                elide: Text.ElideRight
            }
            Text {
                text: backend.openShowUnheard === 0
                      ? "Caught up"
                      : backend.openShowUnheard + " unplayed"
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

    ListView {
        id: list
        anchors.top: statusLine.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        FastWheel { }
        model: page.episodeModel !== null ? page.episodeModel : backend.episodes
        spacing: 2
        // Keep a screenful of delegates so scrolling back up does not rebuild art.
        cacheBuffer: height

        delegate: Rectangle {
            id: row
            width: list.width
            height: Math.max(page.listRowHeight, contentColumn.implicitHeight + 20 * theme.textScale)
            color: hover.containsMouse ? theme.selection : "transparent"

            Rectangle {
                width: 3
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: theme.accent
                visible: model.episodeId === backend.playerEpisodeId && backend.playerStatus !== "stopped"
            }

            Item {
                id: playWell
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                width: page.listCover
                height: page.listCover
                z: 2

                // Episode art when the feed has it; otherwise the show cover.
                readonly property string art: model.cover && model.cover !== ""
                                             ? model.cover
                                             : backend.openShowCover
                Image {
                    anchors.fill: parent
                    source: playWell.art
                    fillMode: Image.PreserveAspectCrop
                    // Synchronous so a cache hit paints with the delegate.
                    // asynchronous:true completes one row at a time and reads as a
                    // top-to-bottom cascade even when the file is already on disk.
                    asynchronous: false
                    cache: true
                    sourceSize.width: page.listCover * 2
                    sourceSize.height: page.listCover * 2
                    visible: playWell.art !== ""
                    z: 0
                }
                Rectangle {
                    anchors.fill: parent
                    color: theme.selection
                    visible: playWell.art === ""
                    z: 0
                }
                Rectangle {
                    anchors.fill: parent
                    color: Qt.rgba(0, 0, 0, playButton.hovered || playButton.down ? 0.45 : 0.28)
                    z: 1
                }
                IconButton {
                    id: playButton
                    anchors.fill: parent
                    z: 2
                    icon.name: model.episodeId === backend.playerEpisodeId
                               && backend.playerStatus === "playing"
                               ? "media-playback-pause-symbolic"
                               : "media-playback-start-symbolic"
                    // Scale the glyph with the cover so art stays readable under it.
                    icon.width: Math.max(16 * theme.textScale, page.listCover * 0.375)
                    icon.height: icon.width
                    glyph: theme.foreground
                    tip: model.episodeId === backend.playerEpisodeId
                         && backend.playerStatus === "playing" ? "Pause" : "Play"
                    // Transparent chrome so episode art shows through under the glyph.
                    background: Item {}
                    onClicked: backend.playEpisode(model.episodeId)
                }
            }

            Column {
                id: contentColumn
                anchors.left: playWell.right
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 14
                anchors.rightMargin: 20
                spacing: 4

                Text {
                    width: parent.width
                    text: model.title
                    color: model.played ? theme.dim : theme.foreground
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
                    text: page.day(model.published)
                          + (model.duration > 0 ? "  ·  " + page.clock(model.duration) : "")
                          + (model.played ? "  ·  Played" : "")
                          + (!model.played && model.positionMs > 5000 ? "  ·  In progress" : "")
                }
                Text {
                    width: parent.width
                    visible: model.description && model.description.length > 0
                    text: page.snippet(model.description)
                    color: theme.dim
                    font.pixelSize: theme.caption
                    font.weight: Font.Normal
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                Rectangle {
                    visible: !model.played && model.duration > 0 && model.positionMs > 0
                    width: parent.width
                    height: 3
                    radius: 1
                    color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.15)
                    Rectangle {
                        width: parent.width * Math.min(1, model.positionMs / (model.duration * 1000))
                        height: parent.height
                        color: theme.accent
                    }
                }
            }

            MouseArea {
                id: hover
                anchors.fill: parent
                anchors.leftMargin: playWell.width + 18
                z: 0
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton)
                        page.openMarkMenu(model.episodeId, model.played)
                    else
                        backend.openEpisode(model.episodeId)
                }
                onWheel: (wheel) => page.takeWheel(wheel)
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
        id: markMenu
        AppMenuItem {
            text: "Mark as played"
            enabled: !page.markedPlayed
            onTriggered: backend.markPlayed(page.markedEpisode, true)
        }
        AppMenuItem {
            text: "Mark as unplayed"
            enabled: page.markedPlayed
            onTriggered: backend.markPlayed(page.markedEpisode, false)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Mark older as played"
            onTriggered: backend.markOlderPlayed(page.markedEpisode)
        }
        AppMenuItem {
            text: "Mark newer as played"
            onTriggered: backend.markNewerPlayed(page.markedEpisode)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Mark all as played"
            onTriggered: page.markAllRequested()
        }
    }
}
