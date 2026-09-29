import QtQuick
import QtQuick.Controls

Item {
    id: page
    signal markAllRequested()
    signal removeRequested()

    property var markedEpisode: 0
    property bool markedPlayed: false

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

        Text {
            id: back
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            text: "Back"
            color: theme.accent
            font.pixelSize: 14 * theme.textScale
            MouseArea {
                anchors.fill: parent
                anchors.margins: -8
                cursorShape: Qt.PointingHandCursor
                onClicked: backend.closeShow()
            }
        }
        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 18
            Text {
                text: "Mark all as played"
                color: theme.accent
                font.pixelSize: 14 * theme.textScale
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.markAllRequested()
                }
            }
            Text {
                text: "Refresh"
                color: theme.foreground
                font.pixelSize: 14 * theme.textScale
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.refreshOpenShow()
                }
            }
            Text {
                text: "Remove"
                color: theme.muted
                font.pixelSize: 14 * theme.textScale
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: page.removeRequested()
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
                text: backend.openShowTitle
                color: theme.foreground
                font.pixelSize: 18 * theme.textScale
                elide: Text.ElideRight
            }
            Text {
                text: backend.openShowUnheard === 0
                      ? "Caught up"
                      : backend.openShowUnheard + " unplayed"
                color: theme.muted
                font.pixelSize: 12 * theme.textScale
            }
        }
    }

    Text {
        id: statusLine
        visible: backend.status.length > 0
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        height: visible ? 22 * theme.textScale : 0
        text: backend.status
        color: theme.muted
        font.pixelSize: 12 * theme.textScale
        elide: Text.ElideRight
    }

    ListView {
        id: list
        anchors.top: statusLine.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: backend.episodes
            spacing: 2

            delegate: Rectangle {
                width: list.width
                height: 76 * theme.textScale
                color: hover.containsMouse ? theme.selection : "transparent"

                Rectangle {
                    width: 3
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    color: theme.accent
                    visible: model.episodeId === backend.playerEpisodeId && backend.playerStatus !== "stopped"
                }

                Column {
                    anchors.left: parent.left
                    anchors.right: markButton.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 24
                    anchors.rightMargin: 16
                    spacing: 4

                    Text {
                        width: parent.width
                        text: model.title
                        color: theme.foreground
                        opacity: model.played ? 0.45 : 1
                        font.pixelSize: 15 * theme.textScale
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        color: theme.muted
                        font.pixelSize: 12 * theme.textScale
                        elide: Text.ElideRight
                        text: page.day(model.published)
                              + (model.duration > 0 ? "  ·  " + page.clock(model.duration) : "")
                              + (model.played ? "  ·  Played" : "")
                              + (!model.played && model.positionMs > 5000 ? "  ·  In progress" : "")
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

                Text {
                    id: markButton
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Mark"
                    color: theme.accent
                    font.pixelSize: 14 * theme.textScale
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -10
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.openMarkMenu(model.episodeId, model.played)
                    }
                }

                MouseArea {
                    id: hover
                    anchors.left: parent.left
                    anchors.right: markButton.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: function(mouse) {
                        if (mouse.button === Qt.RightButton)
                            page.openMarkMenu(model.episodeId, model.played)
                        else
                            backend.playEpisode(model.episodeId)
                    }
                }
            }
        }

    Menu {
        id: markMenu
        MenuItem {
            text: "Mark as played"
            enabled: !page.markedPlayed
            onTriggered: backend.markPlayed(page.markedEpisode, true)
        }
        MenuItem {
            text: "Mark as unplayed"
            enabled: page.markedPlayed
            onTriggered: backend.markPlayed(page.markedEpisode, false)
        }
        MenuSeparator {}
        MenuItem {
            text: "Mark older as played"
            onTriggered: backend.markOlderPlayed(page.markedEpisode)
        }
        MenuItem {
            text: "Mark newer as played"
            onTriggered: backend.markNewerPlayed(page.markedEpisode)
        }
        MenuSeparator {}
        MenuItem {
            text: "Mark all as played"
            onTriggered: page.markAllRequested()
        }
    }
}
