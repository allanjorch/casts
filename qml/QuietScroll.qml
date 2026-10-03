import QtQuick

// Thin thumb for a Flickable, GridView, or ListView. Hidden unless the
// loaded content is taller than the viewport, so short pages stay bare.
// Thumb length is the visible fraction of contentHeight. Count is part of
// that binding: a later append (more shows or episodes) must shrink and
// shift the thumb, not leave the first-paint ratio in place.
// The bar does not take input, so FastWheel on the view still sees the wheel.
Item {
    id: bar
    property var view: null
    // Bind to the view's count where it has one. Plain Flickables stay at 0.
    property int loadedCount: 0

    width: 6
    enabled: false
    z: 2

    // Bumped from contentHeight and loadedCount so a var-typed view cannot
    // freeze the geometry on the first height it happened to paint.
    property int layoutRev: 0
    property real scrollY: 0
    property bool scrolling: false

    function noteLayout() {
        layoutRev = layoutRev + 1
        if (view)
            scrollY = view.contentY
    }

    function noteScroll() {
        if (!view)
            return
        scrollY = view.contentY
        scrolling = true
        idle.restart()
    }

    onLoadedCountChanged: noteLayout()
    onViewChanged: noteLayout()
    Component.onCompleted: noteLayout()

    Connections {
        target: bar.view
        ignoreUnknownSignals: true
        function onContentHeightChanged() { bar.noteLayout() }
        function onContentYChanged() { bar.noteScroll() }
        function onCountChanged() { bar.noteLayout() }
        function onVisibleChanged() { bar.noteLayout() }
    }

    Timer {
        id: idle
        interval: 280
        onTriggered: bar.scrolling = false
    }

    readonly property real inset: 3
    readonly property real track: Math.max(0, height - inset * 2)

    readonly property real contentSpan: {
        var rev = layoutRev
        if (!view)
            return 0
        var span = view.contentHeight + view.topMargin + view.bottomMargin
        // loadedCount is a real dependency even when span is unchanged in
        // the same frame the model grows.
        return span + loadedCount * 0
    }

    readonly property bool overflows: {
        var rev = layoutRev
        if (!view || !view.visible || view.height <= 0)
            return false
        // A few pixels of margin is not a scrolling page.
        return contentSpan > view.height + 8
    }

    readonly property real thumbLen: {
        var rev = layoutRev
        if (!overflows || contentSpan <= 0 || track <= 0)
            return 0
        var natural = track * (view.height / contentSpan)
        var floor = Math.min(track, 10)
        if (natural < floor)
            return floor
        if (natural > track)
            return track
        return natural
    }

    readonly property real thumbY: {
        var rev = layoutRev
        var y = scrollY
        if (!view || !overflows || thumbLen <= 0)
            return inset
        var minPos = view.originY - view.topMargin
        var span = contentSpan - view.height
        if (span < 1)
            return inset
        var offset = y - minPos
        if (offset < 0)
            offset = 0
        if (offset > span)
            offset = span
        return inset + (offset / span) * (track - thumbLen)
    }

    visible: overflows

    Rectangle {
        x: parent.width - width - 1
        y: bar.thumbY
        width: 2
        height: Math.max(0, bar.thumbLen)
        radius: 1
        color: bar.scrolling ? theme.foreground : theme.muted
        opacity: bar.scrolling ? 0.9 : 0.75
    }
}
