import QtQuick
import QtQuick.Controls

Rectangle {
    id: bar
    // Refresh busy used to disable transport / menus; leave them usable while
    // refresh runs. Spinner / StatusNote still use backend.busy elsewhere.
    signal markAllRequested()
    signal nowPlayingRequested()
    color: theme.darkMode ? Qt.darker(theme.background, 1.25) : Qt.lighter(theme.background, 1.08)
    implicitHeight: 96 * theme.textScale
    height: implicitHeight

    property var markedPlayed: false

    // Ascending picker order; values match backend kRates.
    readonly property var rateChoices: [0.5, 0.8, 1.0, 1.2, 1.5, 1.8, 2.0]

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

    function openVolumePopup() {
        speedCloseTimer.stop()
        speedPopup.close()
        volumeCloseTimer.stop()
        volumePopup.x = volumeButton.mapToItem(bar, 0, 0).x
                      + volumeButton.width / 2 - volumePopup.width / 2
        volumePopup.y = -volumePopup.height - 8
        volumePopup.open()
    }

    function openSpeedPopup() {
        volumeCloseTimer.stop()
        volumePopup.close()
        speedCloseTimer.stop()
        speedPopup.x = speedButton.mapToItem(bar, 0, 0).x
                     + speedButton.width / 2 - speedPopup.width / 2
        speedPopup.y = -speedPopup.height - 8
        speedPopup.open()
    }

    function volumePointerInside() {
        return volumeButton.hovered
               || volumePopupBgHover.hovered || volumePopupContentHover.hovered
               || (volumeSlider && volumeSlider.pressed)
    }

    function speedPointerInside() {
        return speedButton.hovered
               || speedPopupBgHover.hovered || speedPopupContentHover.hovered
    }

    function considerCloseVolume() {
        if (volumePointerInside())
            volumeCloseTimer.stop()
        else
            volumeCloseTimer.restart()
    }

    function considerCloseSpeed() {
        if (speedPointerInside())
            speedCloseTimer.stop()
        else
            speedCloseTimer.restart()
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
                // enabled: !backend.busy
                enabled: true
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
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
                        // enabled: !backend.busy
                        enabled: true
                        onClicked: bar.nudge(-15)
                    }
                    IconButton {
                        icon.name: backend.playerStatus === "playing"
                                   ? "media-playback-pause-symbolic"
                                   : "media-playback-start-symbolic"
                        tip: backend.playerStatus === "playing" ? "Pause" : "Play"
                        // enabled: !backend.busy
                        enabled: true
                        onClicked: backend.togglePlayback()
                    }
                    IconButton {
                        icon.name: "media-seek-forward-symbolic"
                        tip: "Forward 30 seconds"
                        // enabled: !backend.busy
                        enabled: true
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
                        tip: ""
                        // enabled: !backend.busy
                        enabled: true
                        onHoveredChanged: {
                            if (hovered)
                                bar.openVolumePopup()
                            else
                                bar.considerCloseVolume()
                        }
                        onClicked: bar.openVolumePopup()
                    }
                    IconButton {
                        id: speedButton
                        caption: rateLabel(backend.playerRate)
                        tip: ""
                        // enabled: !backend.busy
                        enabled: true
                        onHoveredChanged: {
                            if (hovered)
                                bar.openSpeedPopup()
                            else
                                bar.considerCloseSpeed()
                        }
                        onClicked: backend.cycleRate()
                    }
                    IconButton {
                        icon.name: "view-fullscreen-symbolic"
                        tip: "Now playing"
                        // enabled: !backend.busy
                        enabled: true
                        onClicked: bar.nowPlayingRequested()
                    }
                    IconButton {
                        icon.name: "check-plain-symbolic"
                        tip: "Mark"
                        // enabled: !backend.busy
                        enabled: true
                        onClicked: {
                            bar.markedPlayed = backend.playerPlayed
                            markMenu.popup()
                        }
                    }
                }
                SelectableText {
                    id: titleText
                    anchors.left: transport.right
                    anchors.right: extras.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    text: backend.playerTitle
                    color: theme.foreground
                    font.pixelSize: theme.titleSize
                    wrapMode: TextEdit.NoWrap
                    clip: true
                    onActivated: backend.openPlayingEpisode()
                }
            }

            SelectableText {
                width: parent.width
                text: backend.playerError.length > 0 ? backend.playerError : backend.playerShowTitle
                color: backend.playerError.length > 0 ? theme.accent : theme.dim
                font.pixelSize: theme.caption
                wrapMode: TextEdit.NoWrap
                clip: true
            }

            Row {
                width: parent.width
                spacing: 8
                Text {
                    text: scrubber.elapsedText
                    color: theme.dim
                    font.pixelSize: theme.caption
                    font.weight: Font.Normal
                    width: 48 * theme.textScale
                }
                Scrubber {
                    id: scrubber
                    width: parent.width - 110 * theme.textScale
                    anchors.verticalCenter: parent.verticalCenter
                    // interactive: !backend.busy
                    interactive: true
                }
                Text {
                    text: scrubber.durationText
                    color: theme.dim
                    font.pixelSize: theme.caption
                    font.weight: Font.Normal
                }
            }
        }
    }

    Timer {
        id: volumeCloseTimer
        interval: 280
        onTriggered: {
            if (!bar.volumePointerInside())
                volumePopup.close()
        }
    }

    Timer {
        id: speedCloseTimer
        interval: 280
        onTriggered: {
            if (!bar.speedPointerInside())
                speedPopup.close()
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

            HoverHandler {
                id: volumePopupBgHover
                onHoveredChanged: bar.considerCloseVolume()
            }
        }

        contentItem: Item {
            HoverHandler {
                id: volumePopupContentHover
                onHoveredChanged: bar.considerCloseVolume()
            }

            Column {
                anchors.fill: parent
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
                    // enabled: !backend.busy
                    enabled: true
                    onMoved: backend.setVolume(value)
                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }
                    onPressedChanged: {
                        if (pressed)
                            volumeCloseTimer.stop()
                        else
                            bar.considerCloseVolume()
                    }

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
    }

    Popup {
        id: speedPopup
        parent: bar
        width: 64 * theme.textScale
        height: speedColumn.implicitHeight + topPadding + bottomPadding
        padding: 6
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: theme.background
            border.color: theme.selection
            border.width: 1
            radius: 8

            HoverHandler {
                id: speedPopupBgHover
                onHoveredChanged: bar.considerCloseSpeed()
            }
        }

        contentItem: Item {
            HoverHandler {
                id: speedPopupContentHover
                onHoveredChanged: bar.considerCloseSpeed()
            }

            Column {
                id: speedColumn
                width: parent.width
                spacing: 2

                Repeater {
                    model: bar.rateChoices
                    delegate: Item {
                        id: rateRow
                        width: speedColumn.width
                        height: 28 * theme.textScale

                        readonly property real rateValue: modelData
                        readonly property bool current: Math.abs(rateValue - backend.playerRate) < 0.05

                        Rectangle {
                            anchors.fill: parent
                            radius: 6
                            color: rateRow.current ? theme.selection
                                 : rateMouse.containsMouse
                                   ? Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.10)
                                   : "transparent"
                        }

                        Text {
                            anchors.centerIn: parent
                            text: bar.rateLabel(rateRow.rateValue)
                            color: rateRow.current ? theme.accent : theme.foreground
                            font.pixelSize: theme.body
                            font.weight: Font.Normal
                            font.family: "monospace"
                        }

                        MouseArea {
                            id: rateMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: backend.setRate(rateRow.rateValue)
                        }
                    }
                }
            }
        }
    }

    AppMenu {
        id: markMenu
        AppMenuItem {
            text: "Mark as played"
            // enabled: !backend.busy && !bar.markedPlayed
            enabled: !bar.markedPlayed
            onTriggered: backend.markPlayed(backend.playerEpisodeId, true)
        }
        AppMenuItem {
            text: "Mark as unplayed"
            // enabled: !backend.busy && bar.markedPlayed
            enabled: bar.markedPlayed
            onTriggered: backend.markPlayed(backend.playerEpisodeId, false)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Mark older as played"
            // enabled: !backend.busy
            enabled: true
            onTriggered: backend.markOlderPlayed(backend.playerEpisodeId)
        }
        AppMenuItem {
            text: "Mark newer as played"
            // enabled: !backend.busy
            enabled: true
            onTriggered: backend.markNewerPlayed(backend.playerEpisodeId)
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: "Mark all as played"
            // enabled: !backend.busy
            enabled: true
            onTriggered: bar.markAllRequested()
        }
    }
}
