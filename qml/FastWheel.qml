import QtQuick

// Flickable's own wheel coasts (flick / OutExpo), which feels sluggish.
// One notch is Qt's step, wheelScrollLines*24 px per 120 angle units.
// Phased devices (trackpads) already report a pixelDelta; Qt uses that
// instead of the notch. Either way, move twice as far and do it immediately.
//
// Flickable reparents PointerHandlers onto its contentItem (which has no
// contentY). WheelHandler.blocking is true, so the event never reaches the
// view. Writing contentY on the content item, then calling cancelFlick(),
// throws and the wheel is swallowed. Resolve the Flickable at event time.
WheelHandler {
    id: handler
    acceptedDevices: PointerDevice.AllDevices
    // Keep the view from also coasting this same event.
    blocking: true
    readonly property real multiplier: 2

    onWheel: (wheel) => handler.apply(wheel)

    function flickable() {
        var item = parent
        while (item) {
            if (typeof item.cancelFlick === "function" && typeof item.contentY === "number")
                return item
            item = item.parent
        }
        return null
    }

    function apply(wheel) {
        var view = handler.flickable()
        if (!view)
            return
        var dy = handler.axisDelta(wheel.pixelDelta.y, wheel.angleDelta.y, wheel.phase)
        var dx = handler.axisDelta(wheel.pixelDelta.x, wheel.angleDelta.x, wheel.phase)
        var moved = false
        if (dy !== 0)
            moved = handler.nudge(view, true, dy) || moved
        if (dx !== 0)
            moved = handler.nudge(view, false, dx) || moved
        if (moved)
            view.cancelFlick()
    }

    function axisDelta(pixel, angle, phase) {
        var lines = Qt.styleHints.wheelScrollLines
        if (!(lines >= 1))
            lines = 3
        var notch = lines * 24
        // Match QQuickFlickable: a real notch uses angleDelta. A phased device
        // (trackpad) reports pixelDelta as the distance. Some Wayland mice
        // also fill a tiny pixelDelta next to a 120-unit angle; don't let
        // that replace the notch.
        var phased = phase !== Qt.NoScrollPhase && pixel !== 0
        if (phased && Math.abs(pixel) >= Math.abs(angle) / 120 * notch * 0.5)
            return pixel * multiplier
        if (angle !== 0)
            return angle / 120 * notch * multiplier
        return pixel * multiplier
    }

    // Positive delta follows the wheel (up/right). contentY grows downward.
    function nudge(view, vertical, delta) {
        var content = vertical ? view.contentY : view.contentX
        var origin = vertical ? view.originY : view.originX
        var marginStart = vertical ? view.topMargin : view.leftMargin
        var marginEnd = vertical ? view.bottomMargin : view.rightMargin
        var contentSize = vertical ? view.contentHeight : view.contentWidth
        var viewSize = vertical ? view.height : view.width
        var minPos = origin - marginStart
        var span = contentSize + marginStart + marginEnd - viewSize
        var maxPos = minPos + Math.max(0, span)
        var next = content - delta
        if (next < minPos)
            next = minPos
        if (next > maxPos)
            next = maxPos
        if (Math.abs(next - content) < 0.01)
            return false
        if (vertical)
            view.contentY = next
        else
            view.contentX = next
        return true
    }
}
