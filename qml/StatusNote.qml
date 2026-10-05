import QtQuick

SelectableText {
    id: note
    property int holdMs: 3500

    text: backend.status
    color: theme.dim
    wrapMode: TextEdit.NoWrap
    clip: true
    opacity: 0

    Behavior on opacity {
        NumberAnimation { duration: 500 }
    }

    function reveal() {
        if (backend.status.length === 0) {
            opacity = 0
            return
        }
        opacity = 1
        hold.restart()
    }

    Timer {
        id: hold
        interval: note.holdMs
        onTriggered: note.opacity = 0
    }

    Connections {
        target: backend
        function onStatusChanged() { note.reveal() }
    }

    // Do not reveal onCompleted. Cold show pages are created after a shelf
    // refresh has already set backend.status; resurrecting that string made
    // every first open look like "Refresh finished with errors." Warm pages
    // already existed (and had faded), so they never showed it. Live updates
    // still arrive through statusChanged while the note exists.
}
