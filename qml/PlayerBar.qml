import QtQuick
import QtQuick.Controls

Rectangle {
    id: bar
    signal markAllRequested()
    signal nowPlayingRequested()
    color: theme.darkMode ? Qt.darker(theme.background, 1.25) : Qt.lighter(theme.background, 1.08)
    implicitHeight: 96 * theme.textScale
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

    function volumeIcon(value) {
        if (value <= 0.001)
            return "audio-volume-muted-symbolic"
        if (value < 0.34)
            return "audio-volume-low-symbolic"
        if (value < 0.67)
            return "audio-volume-medium-symbolic"
        return "audio-volume-high-symbolic"
    }

    function nudge(seconds) {
        var next = backend.playerPosition + seconds
        if (next < 0)
            next = 0
        if (backend.playerDuration > 0 && next > backend.playerDuration)
            next = backend.playerDuration
        backend.seekTo(next)
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 14

        Item {
            width: 52 * theme.textScale
            height: width
            anchors.verticalCenter: parent.verticalCenter

            Image {
                anchors.fill: parent
                source: backend.playerArt
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: backend.playerArt !== ""
            }
            Rectangle {
                anchors.fill: parent
                visible: backend.playerArt === ""
                color: theme.selection
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.nowPlayingRequested()
            }
        }

        Column {
            width: parent.width - 70 * theme.textScale
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Item {
                width: parent.width
                height: transport.implicitHeight

                Row {
                    id: transport
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    IconButton {
                        icon.name: "media-seek-backward-symbolic"
                        tip: "Back 15 seconds"
                        onClicked: bar.nudge(-15)
                    }
                    IconButton {
                        icon.name: backend.playerStatus === "playing"
                                   ? "media-playback-pause-symbolic"
                                   : "media-playback-start-symbolic"
                        tip: backend.playerStatus === "playing" ? "Pause" : "Play"
                        onClicked: backend.togglePlayback()
                    }
                    IconButton {
                        icon.name: "media-seek-forward-symbolic"
                        tip: "Forward 30 seconds"
                        onClicked: bar.nudge(30)
                    }
                }
                Row {
                    id: extras
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    IconButton {
                        id: volumeButton
                        icon.name: bar.volumeIcon(backend.playerVolume)
                        tip: "Volume"
                        onClicked: {
                            volumePopup.x = volumeButton.mapToItem(bar, 0, 0).x
                                          + volumeButton.width / 2 - volumePopup.width / 2
                            volumePopup.y = -volumePopup.height - 8
                            volumePopup.open()
                        }
                    }
                    IconButton {
                        caption: rateLabel(backend.playerRate)
                        tip: "Playback speed"
                        onClicked: backend.cycleRate()
                    }
                    IconButton {
                        icon.name: "view-fullscreen-symbolic"
                        tip: "Now playing"
                        onClicked: bar.nowPlayingRequested()
                    }
                    IconButton {
                        icon.name: "check-plain-symbolic"
                        tip: "Mark"
                        onClicked: {
                            bar.markedPlayed = backend.playerPlayed
                            markMenu.popup()
                        }
                    }
                }
                Text {
                    id: titleText
                    anchors.left: transport.right
                    anchors.right: extras.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    text: backend.playerTitle
                    color: theme.foreground
                    font.pixelSize: theme.titleSize
                    font.weight: Font.Normal
                    elide: Text.ElideRight

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.openPlayingEpisode()
                    }
                }
            }

            Text {
                width: parent.width
                text: backend.playerError.length > 0 ? backend.playerError : backend.playerShowTitle
                color: backend.playerError.length > 0 ? theme.accent : theme.dim
                font.pixelSize: theme.caption
                font.weight: Font.Normal
                elide: Text.ElideRight
            }

            Row {
                width: parent.width
                spacing: 8
                Text {
                    text: clock(dragging ? dragFraction * backend.playerDuration : backend.playerPosition)
                    color: theme.dim
                    font.pixelSize: theme.caption
                    font.weight: Font.Normal
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
                    color: theme.dim
                    font.pixelSize: theme.caption
                    font.weight: Font.Normal
                }
            }
        }
    }

    Popup {
        id: volumePopup
        parent: bar
        width: 44 * theme.textScale
        height: 148 * theme.textScale
        padding: 10
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: theme.background
            border.color: theme.selection
            border.width: 1
            radius: 8
        }

        contentItem: Column {
            spacing: 8
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: Math.round(backend.playerVolume * 100) + "%"
                color: theme.dim
                font.pixelSize: theme.caption
                font.weight: Font.Normal
                font.family: "monospace"
            }
            Slider {
                id: volumeSlider
                width: parent.width
                height: 100 * theme.textScale
                from: 0
                to: 1
                stepSize: 0.01
                value: backend.playerVolume
                orientation: Qt.Vertical
                onMoved: backend.setVolume(value)

                background: Item {
                    x: volumeSlider.leftPadding + volumeSlider.availableWidth / 2 - width / 2
                    y: volumeSlider.topPadding
                    implicitWidth: 6
                    implicitHeight: volumeSlider.availableHeight
                    width: implicitWidth
                    height: volumeSlider.availableHeight
                    Rectangle {
                        width: parent.width
                        height: parent.height
                        radius: 3
                        color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.18)
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: parent.height * volumeSlider.position
                        radius: 3
                        color: theme.accent
                    }
                }
                handle: Rectangle {
                    x: volumeSlider.leftPadding + volumeSlider.availableWidth / 2 - width / 2
                    y: volumeSlider.topPadding + volumeSlider.visualPosition * (volumeSlider.availableHeight - height)
                    implicitWidth: 14 * theme.textScale
                    implicitHeight: implicitWidth
                    radius: width / 2
                    color: theme.foreground
                }
            }
        }
    }

    AppMenu {
        id: markMenu
        AppMenuItem {
            text: "Mark as played"
            enabled: !bar.markedPlayed
            onTriggered: backend.markPlayed(backend.playerEpisodeId, true)
        }
        AppMenuItem {
            text: "Mark as unplayed"
            enabled: bar.markedPlayed
            onTriggered: backend.markPlayed(backend.playerEpisodeId, false)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Mark older as played"
            onTriggered: backend.markOlderPlayed(backend.playerEpisodeId)
        }
        AppMenuItem {
            text: "Mark newer as played"
            onTriggered: backend.markNewerPlayed(backend.playerEpisodeId)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Mark all as played"
            onTriggered: bar.markAllRequested()
        }
    }
}
