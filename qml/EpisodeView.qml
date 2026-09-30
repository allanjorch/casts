import QtQuick
import QtQuick.Controls

Item {
    id: page
    signal markAllRequested()

    property var markedEpisode: 0
    property bool markedPlayed: false
    // Episode art when present; otherwise the parent show's cover.
    readonly property string art: backend.openEpisodeCover !== ""
                                  ? backend.openEpisodeCover
                                  : backend.openShowCover

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

    function metaLine() {
        var parts = []
        var published = page.day(backend.openEpisodePublished)
        if (published.length > 0)
            parts.push(published)
        if (backend.openEpisodeDuration > 0)
            parts.push(page.clock(backend.openEpisodeDuration))
        if (backend.openEpisodePlayed)
            parts.push("Played")
        else if (backend.openEpisodePositionMs > 5000)
            parts.push("In progress")
        return parts.join("  ·  ")
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
            onClicked: backend.closeEpisode()
        }
        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            IconButton {
                icon.name: backend.playerEpisodeId === backend.openEpisodeId
                           && backend.playerStatus === "playing"
                           ? "media-playback-pause-symbolic"
                           : "media-playback-start-symbolic"
                tip: backend.playerEpisodeId === backend.openEpisodeId
                     && backend.playerStatus === "playing" ? "Pause" : "Play"
                onClicked: backend.playEpisode(backend.openEpisodeId)
            }
            IconButton {
                icon.name: "check-plain-symbolic"
                tip: "Mark"
                onClicked: {
                    page.markedEpisode = backend.openEpisodeId
                    page.markedPlayed = backend.openEpisodePlayed
                    markMenu.popup()
                }
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
                text: backend.openEpisodeTitle
                color: theme.foreground
                font.pixelSize: theme.heading
                font.weight: Font.Normal
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: backend.openShowTitle
                color: theme.dim
                font.pixelSize: theme.caption
                font.weight: Font.Normal
                elide: Text.ElideRight
            }
        }
    }

    Flickable {
        id: scroller
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: body.implicitHeight + 48 * theme.textScale
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: body
            width: Math.min(parent.width - 48, 720)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 12
            spacing: 16

            Item {
                width: parent.width
                height: Math.max(artFrame.height, metaColumn.implicitHeight)

                Rectangle {
                    id: artFrame
                    width: 160 * theme.textScale
                    height: width
                    color: theme.selection
                    visible: page.art !== ""
                    Image {
                        anchors.fill: parent
                        source: page.art
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
                Rectangle {
                    width: 160 * theme.textScale
                    height: width
                    color: theme.selection
                    visible: page.art === ""
                    Text {
                        anchors.centerIn: parent
                        text: "No art"
                        color: theme.dim
                        font.pixelSize: theme.caption
                        font.weight: Font.Normal
                    }
                }

                Column {
                    id: metaColumn
                    anchors.left: artFrame.right
                    anchors.leftMargin: 18
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    Text {
                        width: parent.width
                        text: page.metaLine()
                        color: theme.dim
                        font.pixelSize: theme.body
                        font.weight: Font.Normal
                        wrapMode: Text.Wrap
                    }
                    Rectangle {
                        visible: !backend.openEpisodePlayed
                                 && backend.openEpisodeDuration > 0
                                 && backend.openEpisodePositionMs > 0
                        width: Math.min(parent.width, 280 * theme.textScale)
                        height: 3
                        radius: 1
                        color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.15)
                        Rectangle {
                            width: parent.width * Math.min(1, backend.openEpisodePositionMs
                                                           / (backend.openEpisodeDuration * 1000))
                            height: parent.height
                            color: theme.accent
                        }
                    }
                }
            }

            Text {
                width: parent.width
                visible: backend.openEpisodeDescription.length > 0
                text: backend.openEpisodeDescription
                color: theme.foreground
                font.pixelSize: theme.body
                font.weight: Font.Normal
                wrapMode: Text.Wrap
                lineHeight: 1.35
            }
            Text {
                width: parent.width
                visible: backend.openEpisodeDescription.length === 0
                text: "No description in the feed for this episode."
                color: theme.dim
                font.pixelSize: theme.body
                font.weight: Font.Normal
                wrapMode: Text.Wrap
            }
        }
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
