import QtQuick

// Shared progress track for PlayerBar and Now Playing.
// Hover previews the time at the pointer; a drag previews the scrub
// position and seeks once, on release (same SeekTo path as before).
Item {
    id: scrubber
    implicitWidth: 80
    implicitHeight: 6

    // interactive defaults on. PlayerBar used to set interactive: !backend.busy
    // during refresh; that busy gate is commented out so scrubbing stays available.
    property bool interactive: true
    property bool dragging: false
    property real dragFraction: 0

    readonly property real duration: backend.playerDuration
    readonly property real shownFraction: {
        if (dragging)
            return dragFraction
        if (duration <= 0)
            return 0
        return Math.max(0, Math.min(1, backend.playerPosition / duration))
    }
    readonly property string elapsedText: clock(dragging ? dragFraction * duration : backend.playerPosition)
    readonly property string durationText: clock(duration)

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

    function fractionAt(localX, originX) {
        if (width <= 0)
            return 0
        return Math.max(0, Math.min(1, (localX + originX) / width))
    }

    property real hoverFraction: 0
    readonly property real tipFraction: dragging ? dragFraction : hoverFraction
    readonly property bool tipOpen: interactive && duration > 0 && (dragging || hover.hovered)

    Rectangle {
        anchors.fill: parent
        radius: 3
        color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.18)
        Rectangle {
            width: scrubber.width * scrubber.shownFraction
            height: parent.height
            radius: 3
            color: theme.accent
        }
    }

    // Same quiet metrics as the Now Playing speed chip: radius 4, monospace caption.
    // No delay and no position animation — the chip tracks the pointer on this frame.
    Item {
        id: tip
        z: 2
        enabled: false
        visible: scrubber.tipOpen
        width: tipText.implicitWidth + 10
        height: Math.max(18 * theme.textScale, tipText.implicitHeight + 4)
        y: -height - 4
        x: {
            var left = scrubber.tipFraction * scrubber.width - width / 2
            if (left < 0)
                return 0
            var limit = scrubber.width - width
            return left > limit ? Math.max(0, limit) : left
        }

        Rectangle {
            anchors.fill: parent
            radius: 4
            color: theme.background
            border.width: 1
            border.color: Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.18)
        }
        Text {
            id: tipText
            anchors.centerIn: parent
            text: scrubber.clock(scrubber.tipFraction * scrubber.duration)
            color: theme.dim
            font.pixelSize: theme.caption
            font.weight: Font.Normal
            font.family: "monospace"
            opacity: 0.85
        }
    }

    Item {
        id: hit
        anchors.fill: parent
        anchors.margins: -10

        HoverHandler {
            id: hover
            enabled: scrubber.interactive
            // position has no change signal; sample it at the moment hover begins.
            onHoveredChanged: {
                if (hovered)
                    scrubber.hoverFraction = scrubber.fractionAt(point.position.x, hit.x)
            }
        }

        MouseArea {
            id: area
            anchors.fill: parent
            enabled: scrubber.interactive
            hoverEnabled: true
            preventStealing: true
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onPressed: function(mouse) {
                var fraction = scrubber.fractionAt(mouse.x, hit.x)
                scrubber.hoverFraction = fraction
                scrubber.dragging = true
                scrubber.dragFraction = fraction
            }
            onPositionChanged: function(mouse) {
                var fraction = scrubber.fractionAt(mouse.x, hit.x)
                scrubber.hoverFraction = fraction
                if (scrubber.dragging)
                    scrubber.dragFraction = fraction
            }
            onReleased: function(mouse) {
                if (!scrubber.dragging)
                    return
                var fraction = scrubber.fractionAt(mouse.x, hit.x)
                scrubber.dragFraction = fraction
                backend.seekTo(fraction * scrubber.duration)
                scrubber.dragging = false
            }
            onCanceled: scrubber.dragging = false
        }
    }
}
