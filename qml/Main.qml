import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: win
    width: 1100
    height: 740
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: backend.openEpisodeId !== 0 ? backend.openEpisodeTitle
         : backend.openShowId === 0 ? "Podcasts" : backend.openShowTitle
    color: theme.background
    font.family: "monospace"
    font.weight: Font.Normal

    Material.theme: theme.darkMode ? Material.Dark : Material.Light
    Material.accent: theme.accent

    function inkOn(swatch) {
        var accentLuma = 0.299 * swatch.r + 0.587 * swatch.g + 0.114 * swatch.b
        var backgroundLuma = 0.299 * theme.background.r + 0.587 * theme.background.g + 0.114 * theme.background.b
        var foregroundLuma = 0.299 * theme.foreground.r + 0.587 * theme.foreground.g + 0.114 * theme.foreground.b
        return Math.abs(accentLuma - backgroundLuma) > Math.abs(accentLuma - foregroundLuma)
                ? theme.background : theme.foreground
    }

    property bool addOpen: false
    property bool confirmAll: false
    property bool confirmRemove: false
    property bool nowPlayingOpen: false
    property rect normalGeometry: Qt.rect(x, y, width, height)
    property bool wasMaximized: false

    function trackNormalGeometry() {
        if (visibility === Window.Windowed)
            normalGeometry = Qt.rect(x, y, width, height)
    }

    onXChanged: trackNormalGeometry()
    onYChanged: trackNormalGeometry()
    onWidthChanged: trackNormalGeometry()
    onHeightChanged: trackNormalGeometry()
    onVisibilityChanged: {
        if (visibility === Window.Maximized || visibility === Window.FullScreen)
            wasMaximized = true
        else if (visibility === Window.Windowed)
            wasMaximized = false
    }

    Component.onCompleted: {
        var geometry = backend.windowGeometry()
        if (geometry.valid) {
            x = geometry.x
            y = geometry.y
            width = geometry.width
            height = geometry.height
            if (geometry.maximized)
                showMaximized()
        }
    }
    Component.onDestruction: backend.saveWindowGeometry(
        normalGeometry.x, normalGeometry.y,
        normalGeometry.width, normalGeometry.height, wasMaximized)

    Connections {
        target: backend
        function onRaised() {
            win.show()
            win.raise()
            win.requestActivate()
        }
    }

    Shortcut {
        sequence: "Ctrl+Q"
        context: Qt.ApplicationShortcut
        onActivated: win.close()
    }
    // Short forward stack for mouse Forward after Back (browser-style).
    property var forwardStack: []
    property bool navGuard: false

    function clearForward() {
        if (forwardStack.length > 0)
            forwardStack = []
    }

    function pushForward(entry) {
        forwardStack = forwardStack.concat([entry]).slice(-8)
    }

    function navigateBack() {
        navGuard = true
        if (nowPlayingOpen) {
            pushForward({ kind: "nowPlaying" })
            nowPlayingOpen = false
        } else if (addOpen) {
            addOpen = false
        } else if (confirmAll) {
            confirmAll = false
        } else if (confirmRemove) {
            confirmRemove = false
        } else if (backend.openEpisodeId !== 0) {
            var epId = backend.openEpisodeId
            var showWas = backend.openShowId
            backend.closeEpisode()
            // openPlayingEpisode closes through to the shelf; remember that path.
            if (backend.openShowId === 0 && showWas !== 0)
                pushForward({ kind: "playingEpisode", id: epId })
            else
                pushForward({ kind: "episode", id: epId })
        } else if (backend.openShowId !== 0) {
            pushForward({ kind: "show", id: backend.openShowId })
            backend.closeShow()
        }
        navGuard = false
    }

    function navigateForward() {
        if (forwardStack.length === 0)
            return
        if (addOpen || confirmAll || confirmRemove)
            return
        var stack = forwardStack.slice()
        var entry = stack.pop()
        forwardStack = stack
        navGuard = true
        if (entry.kind === "nowPlaying") {
            if (backend.playerEpisodeId !== 0)
                nowPlayingOpen = true
        } else if (entry.kind === "playingEpisode") {
            if (backend.playerEpisodeId === entry.id)
                backend.openPlayingEpisode()
            else
                backend.openEpisode(entry.id)
        } else if (entry.kind === "episode") {
            backend.openEpisode(entry.id)
        } else if (entry.kind === "show") {
            backend.openShow(entry.id)
        }
        navGuard = false
    }

    Connections {
        target: backend
        function onOpenShowChanged() {
            if (!navGuard && backend.openShowId !== 0)
                clearForward()
        }
        function onOpenEpisodeChanged() {
            if (!navGuard && backend.openEpisodeId !== 0)
                clearForward()
        }
    }
    onNowPlayingOpenChanged: {
        if (!navGuard && nowPlayingOpen)
            clearForward()
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: navigateBack()
    }
    Shortcut {
        sequence: "Alt+Left"
        context: Qt.ApplicationShortcut
        onActivated: navigateBack()
    }
    Shortcut {
        sequence: "Alt+Right"
        context: Qt.ApplicationShortcut
        onActivated: navigateForward()
    }
    Shortcut {
        sequence: "Backspace"
        context: Qt.ApplicationShortcut
        // Leave Backspace to TextField / TextInput / TextEdit / TextArea.
        enabled: {
            var item = win.activeFocusItem
            return !(item instanceof TextInput || item instanceof TextEdit)
        }
        onActivated: navigateBack()
    }
    function zoomFromWheel(delta) {
        if (addOpen || confirmAll || confirmRemove || nowPlayingOpen || backend.openEpisodeId !== 0)
            return false
        if (backend.openShowId !== 0)
            showView.applyZoomDelta(delta)
        else
            shelfView.applyZoomDelta(delta)
        return true
    }

    Shortcut {
        sequence: "Space"
        enabled: !addOpen
        context: Qt.ApplicationShortcut
        onActivated: backend.togglePlayback()
    }
    Shortcut {
        sequences: [StandardKey.ZoomIn, "Ctrl+="]
        context: Qt.ApplicationShortcut
        enabled: backend.openEpisodeId === 0 && !addOpen && !confirmAll && !confirmRemove && !nowPlayingOpen
        onActivated: backend.openShowId !== 0 ? showView.zoomIn() : shelfView.zoomIn()
    }
    Shortcut {
        sequence: StandardKey.ZoomOut
        context: Qt.ApplicationShortcut
        enabled: backend.openEpisodeId === 0 && !addOpen && !confirmAll && !confirmRemove && !nowPlayingOpen
        onActivated: backend.openShowId !== 0 ? showView.zoomOut() : shelfView.zoomOut()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Content stack sits above the always-visible PlayerBar so Now Playing
        // never paints under / behind the chrome.
        Item {
            id: contentStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ShelfView {
                id: shelfView
                anchors.fill: parent
                visible: backend.openShowId === 0
                onAddRequested: {
                    feedField.text = ""
                    win.addOpen = true
                    feedField.forceActiveFocus()
                }
                onImportRequested: opmlDialog.open()
                onMarkAllRequested: win.confirmAll = true
            }

            ShowView {
                id: showView
                anchors.fill: parent
                visible: backend.openShowId !== 0 && backend.openEpisodeId === 0
                onMarkAllRequested: win.confirmAll = true
                onRemoveRequested: win.confirmRemove = true
            }

            EpisodeView {
                anchors.fill: parent
                visible: backend.openEpisodeId !== 0
                onMarkAllRequested: win.confirmAll = true
            }

            NowPlaying {
                anchors.fill: parent
                visible: win.nowPlayingOpen && backend.playerEpisodeId !== 0
                onDismissRequested: win.nowPlayingOpen = false
            }
        }

        PlayerBar {
            Layout.fillWidth: true
            visible: backend.playerEpisodeId !== 0
            onMarkAllRequested: win.confirmAll = true
            onNowPlayingRequested: win.nowPlayingOpen = true
        }
    }

    Connections {
        target: backend
        function onPlayerStateChanged() {
            if (backend.playerEpisodeId === 0)
                win.nowPlayingOpen = false
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: addOpen || confirmAll || confirmRemove
        color: Qt.rgba(0, 0, 0, 0.45)

        MouseArea {
            anchors.fill: parent
            onClicked: {
                addOpen = false
                confirmAll = false
                confirmRemove = false
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 460)
            height: addOpen ? column.implicitHeight + 36 : confirmColumn.implicitHeight + 36
            color: theme.background
            border.color: theme.selection
            border.width: 1

            Column {
                id: column
                visible: addOpen
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 18
                spacing: 12

                Text {
                    text: "Add a feed"
                    color: theme.foreground
                    font.pixelSize: theme.heading
                    font.weight: Font.Normal
                }
                TextField {
                    id: feedField
                    width: parent.width
                    placeholderText: "https://example.com/feed.xml"
                    color: theme.foreground
                    font.pixelSize: theme.body
                    font.weight: Font.Normal
                    placeholderTextColor: theme.dim
                    Material.accent: theme.accent
                    onAccepted: submitFeed()
                    background: Rectangle {
                        color: theme.selection
                        opacity: 0.55
                    }
                }
                Row {
                    spacing: 8
                    TextButton {
                        label: "Add"
                        onClicked: submitFeed()
                    }
                    TextButton {
                        label: "Cancel"
                        onClicked: addOpen = false
                    }
                }
            }

            Column {
                id: confirmColumn
                visible: !addOpen
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 18
                spacing: 14

                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: theme.foreground
                    font.pixelSize: theme.body
                    font.weight: Font.Normal
                    text: confirmRemove
                          ? "Remove " + backend.openShowTitle + "? Playback history for this show goes with it."
                          : "Mark every episode of " + (backend.playerShowId !== 0 && backend.openShowId === 0
                                                        ? backend.playerShowTitle : backend.openShowTitle)
                            + " as played?"
                }
                Row {
                    spacing: 8
                    TextButton {
                        label: confirmRemove ? "Remove" : "Mark all as played"
                        onClicked: {
                            if (confirmRemove)
                                backend.removeOpenShow()
                            else
                                backend.markAllPlayed(backend.openShowId !== 0 ? backend.openShowId : backend.playerShowId)
                            confirmAll = false
                            confirmRemove = false
                        }
                    }
                    TextButton {
                        label: "Cancel"
                        onClicked: {
                            confirmAll = false
                            confirmRemove = false
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: opmlDialog
        title: "Import OPML"
        nameFilters: ["OPML files (*.opml *.xml)", "All files (*)"]
        onAccepted: backend.importOpml(selectedFile)
    }

    function submitFeed() {
        var url = feedField.text.trim()
        if (url.length === 0)
            return
        backend.addFeed(url)
        addOpen = false
    }

    component TextButton: Text {
        id: button
        property string label: ""
        signal clicked()
        text: label
        color: theme.accent
        font.pixelSize: theme.body
        font.weight: Font.Normal
        padding: 6

        MouseArea {
            anchors.fill: parent
            onClicked: button.clicked()
            cursorShape: Qt.PointingHandCursor
        }
    }
}
