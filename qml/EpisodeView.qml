import QtQuick

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

    function escapeHtml(value) {
        return value.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    function escapeAttr(value) {
        return page.escapeHtml(value).replace(/"/g, "&quot;")
    }

    function countChar(value, ch) {
        var n = 0
        for (var i = 0; i < value.length; ++i) {
            if (value.charAt(i) === ch)
                n++
        }
        return n
    }

    function peelUrl(url) {
        var trail = ""
        while (url.length > 0) {
            var ch = url.charAt(url.length - 1)
            if (".,;:!?\"'".indexOf(ch) !== -1) {
                trail = ch + trail
                url = url.slice(0, -1)
                continue
            }
            if ((ch === ")" && page.countChar(url, "(") < page.countChar(url, ")"))
                    || (ch === "]" && page.countChar(url, "[") < page.countChar(url, "]"))) {
                trail = ch + trail
                url = url.slice(0, -1)
                continue
            }
            break
        }
        return { url: url, trail: trail }
    }

    function stampSeconds(stamp) {
        var parts = stamp.split(":")
        if (parts.length === 3)
            return Number(parts[0]) * 3600 + Number(parts[1]) * 60 + Number(parts[2])
        if (parts.length === 2)
            return Number(parts[0]) * 60 + Number(parts[1])
        return -1
    }

    // #RRGGBB. Qt RichText ignores Text.linkColor (that property is StyledText
    // only) and paints anchors palette(link) blue unless the <a> carries a color.
    function colorHex(color) {
        function byte(channel) {
            var n = Math.round(channel * 255)
            if (n < 0)
                n = 0
            else if (n > 255)
                n = 255
            var hex = n.toString(16)
            return hex.length < 2 ? "0" + hex : hex
        }
        return "#" + byte(color.r) + byte(color.g) + byte(color.b)
    }

    function anchorHtml(href, label, color) {
        return "<a href=\"" + page.escapeAttr(href)
                + "\" style=\"color:" + page.colorHex(color)
                + "; text-decoration: underline\">"
                + page.escapeHtml(label) + "</a>"
    }

    // Plain show notes -> quiet underlined links. seek: offsets stay on this page.
    // `color` is part of the HTML so the notes binding rebuilds when the theme changes.
    function linkify(plain, color) {
        if (!plain || plain.length === 0)
            return ""
        var re = /https?:\/\/[^\s<]+|www\.[^\s<]+|\d{1,2}:[0-5]\d:[0-5]\d|\d{1,4}:[0-5]\d/gi
        var out = ""
        var last = 0
        var match
        while ((match = re.exec(plain)) !== null) {
            var start = match.index
            var token = match[0]
            var prev = start > 0 ? plain.charAt(start - 1) : ""
            var end = start + token.length
            var next = end < plain.length ? plain.charAt(end) : ""
            var head = token.charAt(0)
            var isUrl = head === "h" || head === "H" || head === "w" || head === "W"
            if (isUrl) {
                if (prev && /[A-Za-z0-9]/.test(prev))
                    continue
                var peeled = page.peelUrl(token)
                if (peeled.url.length === 0)
                    continue
                var href = /^www\./i.test(peeled.url) ? "https://" + peeled.url : peeled.url
                out += page.escapeHtml(plain.slice(last, start)).replace(/\n/g, "<br>")
                out += page.anchorHtml(href, peeled.url, color)
                out += page.escapeHtml(peeled.trail)
                last = end
                continue
            }
            if ((prev && /[0-9:A-Za-z]/.test(prev)) || (next && /[0-9:A-Za-z]/.test(next)))
                continue
            if (next === "." && end + 1 < plain.length && /[0-9]/.test(plain.charAt(end + 1)))
                continue
            var seconds = page.stampSeconds(token)
            if (!(seconds >= 0))
                continue
            out += page.escapeHtml(plain.slice(last, start)).replace(/\n/g, "<br>")
            out += page.anchorHtml("seek:" + seconds, token, color)
            last = end
        }
        out += page.escapeHtml(plain.slice(last)).replace(/\n/g, "<br>")
        return out
    }

    function playFrom(seconds) {
        var id = backend.openEpisodeId
        if (!id)
            return
        // Already loaded: seek in place. playEpisode() would pause if it is playing.
        var loaded = backend.playerEpisodeId === id
        if (!loaded)
            backend.playEpisode(id)
        backend.seekTo(seconds)
        if (loaded && backend.playerStatus !== "playing")
            backend.togglePlayback()
    }

    function openDescriptionLink(link) {
        if (link.indexOf("seek:") === 0) {
            var seconds = Number(link.slice(5))
            if (!(seconds >= 0))
                return
            page.playFrom(seconds)
            return
        }
        Qt.openUrlExternally(link)
    }

    // Right-click copies the web URL. Timestamps are seek: links and stay on the page.
    function copyWebLink(link) {
        if (!link || link.length === 0)
            return
        if (link.indexOf("http://") !== 0 && link.indexOf("https://") !== 0)
            return
        linkClip.text = link
        linkClip.selectAll()
        linkClip.copy()
        linkClip.deselect()
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
        FastWheel { }

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
                id: notes
                width: parent.width
                visible: backend.openEpisodeDescription.length > 0
                text: page.linkify(backend.openEpisodeDescription, theme.foreground)
                textFormat: Text.RichText
                color: theme.foreground
                // RichText ignores linkColor. Each <a> has style color = theme.foreground.
                font.family: "monospace"
                font.pixelSize: theme.body
                font.weight: Font.Normal
                wrapMode: Text.Wrap
                lineHeight: 1.35
                onLinkActivated: function (link) { page.openDescriptionLink(link) }
                HoverHandler {
                    cursorShape: notes.hoveredLink.length > 0
                                 ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function (point) {
                        page.copyWebLink(notes.linkAt(point.position.x, point.position.y))
                    }
                }
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

    QuietScroll {
        view: scroller
        anchors.top: scroller.top
        anchors.bottom: scroller.bottom
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

    TextEdit {
        id: linkClip
        visible: false
        width: 0
        height: 0
    }
}
