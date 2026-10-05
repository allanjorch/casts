import QtQuick

Item {
    id: page
    signal dismissRequested()

    // Very short pane: horizontal art + titles, keep transport/scrubber, hide blurb.
    // Full stacked cover layout when page.height is at or above this.
    readonly property real compactThreshold: 320 * theme.textScale
    readonly property bool compact: height < compactThreshold

    function rateLabel(value) {
        var rounded = Math.round(value * 10) / 10
        return (rounded % 1 === 0 ? rounded.toFixed(0) : rounded.toFixed(1)) + "×"
    }

    function nudge(seconds) {
        var next = backend.playerPosition + seconds
        if (next < 0)
            next = 0
        if (backend.playerDuration > 0 && next > backend.playerDuration)
            next = backend.playerDuration
        backend.seekTo(next)
    }

    Rectangle {
        anchors.fill: parent
        color: theme.background
    }

    IconButton {
        id: back
        anchors.left: parent.left
        anchors.leftMargin: page.compact ? 8 : 16
        anchors.top: parent.top
        anchors.topMargin: page.compact ? 8 : 16
        z: 2
        icon.name: "go-previous-symbolic"
        tip: "Back"
        onClicked: page.dismissRequested()
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        // Include top/bottom pads so scrolling reaches scrubber fully above PlayerBar.
        contentHeight: Math.max(height, body.topPad + body.implicitHeight + body.bottomPad)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        FastWheel { }

        Column {
            id: body
            readonly property real sidePad: (page.compact ? 16 : 40) * theme.textScale
            readonly property real topPad: (page.compact ? 48 : 56) * theme.textScale
            // Breathing room above the always-visible PlayerBar (outside this page).
            readonly property real bottomPad: (page.compact ? 12 : 28) * theme.textScale
            width: Math.max(120 * theme.textScale, parent.width - sidePad * 2)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: topPad
            spacing: (page.compact ? 10 : 20) * theme.textScale

            // Height claimed by title, transport, scrubber, optional blurb, and gaps.
            // PlayerBar lives in Main below this page, so page.height already excludes it.
            readonly property real chromeBelow: {
                var gaps = 3
                var h = titles.implicitHeight
                        + transport.implicitHeight
                        + scrubberCol.implicitHeight
                if (synopsis.visible) {
                    h += synopsis.implicitHeight
                    gaps += 1
                }
                return h + spacing * gaps + bottomPad
            }
            // Square cover: fit leftover space; shrink aggressively in short windows
            // so title/transport/scrubber stay reachable (prefer shrink over hide).
            readonly property real coverSide: {
                var byWidth = width
                var byHeight = page.height - topPad - chromeBelow
                var ideal = Math.min(byWidth, byHeight)
                if (ideal >= 160 * theme.textScale)
                    return ideal
                // Short tiled window: keep a small cover rather than forcing overflow.
                return Math.max(72 * theme.textScale, ideal)
            }

            // Compact: small art beside title/show. Full: large centered cover above titles.
            Item {
                width: parent.width
                height: coverFrame.height
                visible: !page.compact

                Rectangle {
                    id: coverFrame
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: body.coverSide
                    height: width
                    radius: 12
                    color: theme.selection
                    clip: true

                    // backend.playerArt already prefers episode art over the show cover.
                    Image {
                        anchors.fill: parent
                        source: backend.playerArt
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize.width: Math.max(320, coverFrame.width * 2)
                        sourceSize.height: Math.max(320, coverFrame.height * 2)
                        visible: backend.playerArt !== ""
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: backend.playerArt === ""
                        text: "No art"
                        color: theme.dim
                        font.pixelSize: theme.body
                        font.weight: Font.Normal
                        font.family: "monospace"
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.togglePlayback()
                    }
                }
            }

            Row {
                id: compactHeader
                width: parent.width
                spacing: 12 * theme.textScale
                visible: page.compact

                Rectangle {
                    id: compactArt
                    width: 56 * theme.textScale
                    height: width
                    radius: 8
                    color: theme.selection
                    clip: true
                    anchors.verticalCenter: parent.verticalCenter

                    Image {
                        anchors.fill: parent
                        source: backend.playerArt
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize.width: Math.max(112, compactArt.width * 2)
                        sourceSize.height: Math.max(112, compactArt.height * 2)
                        visible: backend.playerArt !== ""
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: backend.playerArt === ""
                        text: "—"
                        color: theme.dim
                        font.pixelSize: theme.caption
                        font.weight: Font.Normal
                        font.family: "monospace"
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.togglePlayback()
                    }
                }

                Column {
                    width: parent.width - compactArt.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    SelectableText {
                        width: parent.width
                        text: backend.playerTitle
                        color: theme.foreground
                        font.pixelSize: theme.titleSize
                        font.family: "monospace"
                        wrapMode: TextEdit.Wrap
                    }
                    SelectableText {
                        width: parent.width
                        text: backend.playerShowTitle
                        color: theme.dim
                        font.pixelSize: theme.caption
                        font.family: "monospace"
                        wrapMode: TextEdit.NoWrap
                        clip: true
                    }
                }
            }

            Column {
                id: titles
                width: Math.min(parent.width, 560 * theme.textScale)
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 6
                visible: !page.compact

                SelectableText {
                    width: parent.width
                    horizontalAlignment: TextEdit.AlignHCenter
                    text: backend.playerTitle
                    color: theme.foreground
                    font.pixelSize: theme.heading
                    font.family: "monospace"
                    wrapMode: TextEdit.Wrap
                }
                SelectableText {
                    width: parent.width
                    horizontalAlignment: TextEdit.AlignHCenter
                    text: backend.playerShowTitle
                    color: theme.dim
                    font.pixelSize: theme.body
                    font.family: "monospace"
                    wrapMode: TextEdit.NoWrap
                    clip: true
                }
            }

            Row {
                id: transport
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: page.compact ? 6 : 10
                IconButton {
                    icon.name: "media-seek-backward-symbolic"
                    tip: "Back 15 seconds"
                    onClicked: page.nudge(-15)
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
                    onClicked: page.nudge(30)
                }
            }

            Column {
                id: scrubberCol
                width: Math.min(parent.width, 560 * theme.textScale)
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: page.compact ? 4 : 8

                Row {
                    width: parent.width
                    spacing: page.compact ? 6 : 10
                    Text {
                        id: elapsedLabel
                        text: scrubber.elapsedText
                        color: theme.dim
                        font.pixelSize: theme.caption
                        font.weight: Font.Normal
                        font.family: "monospace"
                        width: 52 * theme.textScale
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Scrubber {
                        id: scrubber
                        width: parent.width
                               - elapsedLabel.width
                               - remainLabel.implicitWidth
                               - rateChip.width
                               - parent.spacing * 3
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: remainLabel
                        text: scrubber.durationText
                        color: theme.dim
                        font.pixelSize: theme.caption
                        font.weight: Font.Normal
                        font.family: "monospace"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    // Quiet caption chip: playback speed beside remaining time.
                    Item {
                        id: rateChip
                        width: rateText.implicitWidth + 10
                        height: Math.max(18 * theme.textScale, rateText.implicitHeight + 4)
                        anchors.verticalCenter: parent.verticalCenter

                        Rectangle {
                            anchors.fill: parent
                            radius: 4
                            color: rateArea.containsMouse || rateArea.pressed
                                   ? Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.10)
                                   : "transparent"
                        }
                        Text {
                            id: rateText
                            anchors.centerIn: parent
                            text: page.rateLabel(backend.playerRate)
                            color: theme.dim
                            font.pixelSize: theme.caption
                            font.weight: Font.Normal
                            font.family: "monospace"
                            opacity: 0.85
                        }
                        MouseArea {
                            id: rateArea
                            anchors.fill: parent
                            anchors.margins: -2
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: backend.cycleRate()
                        }
                    }
                }
            }

            SelectableText {
                id: synopsis
                width: Math.min(parent.width, 560 * theme.textScale)
                anchors.horizontalCenter: parent.horizontalCenter
                // Compact panes hide the long description; full layout keeps a short blurb.
                visible: !page.compact && backend.playerDescription.length > 0
                horizontalAlignment: TextEdit.AlignHCenter
                text: backend.playerDescription
                color: theme.dim
                font.pixelSize: theme.bodySmall
                font.family: "monospace"
                wrapMode: TextEdit.Wrap
                // Cap height roughly to four lines (TextEdit has no maximumLineCount).
                height: Math.min(implicitHeight, font.pixelSize * 1.35 * 4)
                clip: true
                opacity: 0.9
            }
        }
    }

    QuietScroll {
        view: scroller
        anchors.top: scroller.top
        anchors.bottom: scroller.bottom
        anchors.right: parent.right
        anchors.rightMargin: 2
    }
}
