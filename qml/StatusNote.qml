import QtQuick

Text {
    id: note
    property int holdMs: 3500

    text: backend.status
    color: theme.dim
    font.weight: Font.Normal
    elide: Text.ElideRight
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

    Component.onCompleted: note.reveal()
}
