import QtQuick

// Read-only selectable label. Drag with LMB to mark; on release, a non-empty
// selection is copied to the clipboard then cleared. Looks like Text; selection
// uses theme.selection with theme.foreground ink.
TextEdit {
    id: root

    // Short left-click with no selection (and not on a link).
    signal activated()
    // linkActivated comes from TextEdit; callers use onLinkActivated as usual.
    signal linkCopyRequested(string link)

    property bool autoCopy: true

    readOnly: true
    selectByMouse: true
    persistentSelection: true
    activeFocusOnPress: true
    cursorVisible: false
    wrapMode: TextEdit.Wrap
    textFormat: TextEdit.PlainText
    color: theme.foreground
    selectionColor: theme.selection
    selectedTextColor: theme.foreground
    font.weight: Font.Normal

    HoverHandler {
        cursorShape: (root.textFormat === TextEdit.RichText && root.hoveredLink.length > 0)
                     ? Qt.PointingHandCursor
                     : Qt.IBeamCursor
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: function (eventPoint) {
            if (root.textFormat !== TextEdit.RichText)
                return
            var link = root.linkAt(eventPoint.position.x, eventPoint.position.y)
            if (link && link.length > 0)
                root.linkCopyRequested(link)
        }
    }

    // PointHandler keeps a passive grab so TextEdit still owns select-by-mouse.
    // A MouseArea that sets mouse.accepted=false never sees Move/Release (Qt only
    // delivers those after an accepted press), which broke auto-copy on LMB-up.
    PointHandler {
        id: gate
        acceptedButtons: Qt.LeftButton
        property real startX: 0
        property real startY: 0
        property bool moved: false
        property string pressLink: ""

        onActiveChanged: {
            if (active) {
                finishDelay.stop()
                startX = point.position.x
                startY = point.position.y
                moved = false
                pressLink = (root.textFormat === TextEdit.RichText)
                            ? root.linkAt(point.position.x, point.position.y) : ""
            } else {
                // Defer past TextEdit finalizing the selection on this release.
                finishDelay.moved = gate.moved
                finishDelay.pressLink = gate.pressLink
                finishDelay.restart()
            }
        }
        onPointChanged: {
            if (!active)
                return
            if (Math.abs(point.position.x - startX) + Math.abs(point.position.y - startY) > 4)
                moved = true
        }
    }

    Timer {
        id: finishDelay
        interval: 1
        property bool moved: false
        property string pressLink: ""
        onTriggered: root.finishPointer(moved, pressLink)
    }

    function finishPointer(moved, pressLink) {
        if (selectedText.length > 0) {
            if (autoCopy) {
                // copy() needs an active selection; focus helps on some platforms.
                forceActiveFocus()
                copy()
            }
            deselect()
            return
        }
        if (pressLink && pressLink.length > 0)
            return
        if (!moved)
            activated()
    }
}
