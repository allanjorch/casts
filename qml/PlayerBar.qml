import QtQuick
import QtQuick.Controls

Rectangle {
    id: bar
    signal markAllRequested()
    color: theme.darkMode ? Qt.darker(theme.background, 1.25) : Qt.lighter(theme.background, 1.08)
    implicitHeight: 78 * theme.textScale
    height: implicitHeight

    property bool dragging: false
    property real dragFraction: 0
    property var markedPlayed: false

    function shownFraction() {
        if (dragging)
            return dragFraction
        if (backend.playerDuration <= 0)
            return 0
        return Math.max(0, Math.min(1, backend.playerPosition / backend.playerDuration))
    }

    function clock(seconds) {
        if (!seconds || seconds < 0)
            return "0:00"
        var total = Math.floor(seconds)
        var hours = Math.floor(total / 3600)
        var minutes = Math.floor((total % 3600) / 60)
        var remain = total % 60
        function pad(value) { return value < 10 ? "0" + value : "" + value }
        if (hours > 0)
            return hours + ":" + pad(minutes) + ":" + pad(remain)
        return minutes + ":" + pad(remain)
    }

    function rateLabel(value) {
        var rounded = Math.round(value * 10) / 10
        return (rounded % 1 === 0 ? rounded.toFixed(0) : rounded.toFixed(1)) + "×"
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 14

        Image {
            width: 52 * theme.textScale
            height: width
            anchors.verticalCenter: parent.verticalCenter
            source: backend.playerArt
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            visible: backend.playerArt !== ""
        }
        Rectangle {
            visible: backend.playerArt === ""
            width: 52 * theme.textScale
            height: width
            anchors.verticalCenter: parent.verticalCenter
            color: theme.selection
        }

        Column {
            width: parent.width - 70 * theme.textScale
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Row {
                width: parent.width
                spacing: 14
                Text {
                    text: backend.playerStatus === "playing" ? "Pause" : "Play"
                    color: theme.accent
                    font.pixelSize: 14 * theme.textScale
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.togglePlayback()
                    }
                }
                Text {
                    text: backend.playerTitle
                    color: theme.foreground
                    font.pixelSize: 14 * theme.textScale
                    elide: Text.ElideRight
                    width: Math.max(40, parent.width - 280 * theme.textScale)
                }
                Text {
                    text: rateLabel(backend.playerRate)
                    color: theme.foreground
                    font.pixelSize: 14 * theme.textScale
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.cycleRate()
                    }
                }
                Text {
                    text: "Mark"
                    color: theme.accent
                    font.pixelSize: 14 * theme.textScale
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            bar.markedPlayed = backend.playerPlayed
                            markMenu.popup()
                        }
                    }
                }
            }

            Text {
                width: parent.width
                text: backend.playerError.length > 0 ? backend.playerError : backend.playerShowTitle
                color: backend.playerError.length > 0 ? theme.accent : theme.muted
                font.pixelSize: 12 * theme.textScale
                elide: Text.ElideRight
            }

            Row {
                width: parent.width
                spacing: 8
                Text {
                    text: clock(dragging ? dragFraction * backend.playerDuration : backend.playerPosition)
                    color: theme.muted
                    font.pixelSize: 11 * theme.textScale
                    width: 48 * theme.textScale
                }
                Rectangle {
                    id: track
                    width: parent.width - 110 * theme.textScale
                    height: 6
                    radius: 3
                    anchors.verticalCenter: parent.verticalCenter
                    color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.18)
                    Rectangle {
                        width: parent.width * bar.shownFraction()
                        height: parent.height
                        radius: 3
                        color: theme.accent
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        cursorShape: Qt.PointingHandCursor
                        onPressed: function(mouse) {
                            bar.dragging = true
                            bar.dragFraction = Math.max(0, Math.min(1, mouse.x / track.width))
                        }
                        onPositionChanged: function(mouse) {
                            if (bar.dragging)
                                bar.dragFraction = Math.max(0, Math.min(1, mouse.x / track.width))
                        }
                        onReleased: {
                            backend.seekTo(bar.dragFraction * backend.playerDuration)
                            bar.dragging = false
                        }
                    }
                }
                Text {
                    text: clock(backend.playerDuration)
                    color: theme.muted
                    font.pixelSize: 11 * theme.textScale
                }
            }
        }
    }

    Menu {
        id: markMenu
        MenuItem {
            text: "Mark as played"
            enabled: !bar.markedPlayed
            onTriggered: backend.markPlayed(backend.playerEpisodeId, true)
        }
        MenuItem {
            text: "Mark as unplayed"
            enabled: bar.markedPlayed
            onTriggered: backend.markPlayed(backend.playerEpisodeId, false)
        }
        MenuSeparator {}
        MenuItem {
            text: "Mark older as played"
            onTriggered: backend.markOlderPlayed(backend.playerEpisodeId)
        }
        MenuItem {
            text: "Mark newer as played"
            onTriggered: backend.markNewerPlayed(backend.playerEpisodeId)
        }
        MenuSeparator {}
        MenuItem {
            text: "Mark all as played"
            onTriggered: bar.markAllRequested()
        }
    }
}
