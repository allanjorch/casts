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
    title: pageKind === "episode" ? backend.openEpisodeTitle
         : pageKind === "show" ? backend.openShowTitle
         : pageKind === "queue" ? "Queue"
         : pageKind === "explore" ? "Explore"
         : pageKind === "nowPlaying" ? backend.playerTitle
         : "OmaEar"
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
    property bool confirmLibrary: false
    property bool confirmRemove: false
    property bool confirmQueue: false
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
    // ---- Page navigation: one browser-style history -------------------------
    // Entries are {kind, showId, episodeId}; kind is shelf | show | episode | queue
    // | nowPlaying. The visible page is derived from history[historyIndex] only.
    // Modal dialogs are not entries; back() closes them first. history[0] is
    // always the shelf, so Back on any other page has somewhere to go.
    readonly property int historyLimit: 60
    property var history: [{ kind: "shelf", showId: 0, episodeId: 0 }]
    property int historyIndex: 0
    readonly property var currentPage: history[Math.max(0, Math.min(historyIndex, history.length - 1))]
    readonly property string pageKind: currentPage.kind
    readonly property bool canGoBack: modalOpen || explore.previewOpen || explorePage.regionOpen || historyIndex > 0
    readonly property bool canGoForward: !modalOpen && historyIndex < history.length - 1
    readonly property bool modalOpen: addOpen || confirmAll || confirmLibrary || confirmRemove || confirmQueue
    readonly property bool nowPlayingOpen: pageKind === "nowPlaying"
    readonly property bool queueOpen: pageKind === "queue"
    // True while we drive backend.open*/close*, so its change signals are not
    // mistaken for the backend dropping a page on its own.
    property bool navGuard: false

    function makeEntry(kind, showId, episodeId) {
        return { kind: kind, showId: showId || 0, episodeId: episodeId || 0 }
    }

    function sameEntry(a, b) {
        return a.kind === b.kind && a.showId === b.showId && a.episodeId === b.episodeId
    }

    function navigate(entry) {
        if (sameEntry(entry, currentPage)) {
            applyPage()
            return
        }
        var list = history.slice(0, historyIndex + 1)
        list.push(entry)
        // Cap the length but keep the shelf root at 0.
        while (list.length > historyLimit)
            list.splice(1, 1)
        history = list
        historyIndex = list.length - 1
        applyPage()
    }

    function closeModals() {
        if (!modalOpen)
            return false
        addOpen = false
        confirmAll = false
        confirmLibrary = false
        confirmRemove = false
        confirmQueue = false
        return true
    }

    function back() {
        if (closeModals())
            return
        // Explore's region picker and preview card are page-level overlays: close them first.
        if (pageKind === "explore" && explorePage.closeRegions())
            return
        if (pageKind === "explore" && explore.previewOpen) {
            explore.closePreview()
            return
        }
        if (historyIndex <= 0)
            return
        historyIndex = historyIndex - 1
        applyPage()
    }

    function forward() {
        if (modalOpen || historyIndex >= history.length - 1)
            return
        historyIndex = historyIndex + 1
        applyPage()
    }

    // Kept for the C++ input filter and older call sites.
    function navigateBack() { back() }
    function navigateForward() { forward() }

    function openShowPage(showId) { navigate(makeEntry("show", showId, 0)) }
    function openEpisodePage(episodeId) {
        navigate(makeEntry("episode", backend.episodeShowId(episodeId), episodeId))
    }
    function openQueuePage() { navigate(makeEntry("queue", 0, 0)) }
    function openExplorePage() { navigate(makeEntry("explore", 0, 0)) }
    function openNowPlayingPage() {
        if (backend.playerEpisodeId !== 0)
            navigate(makeEntry("nowPlaying", 0, 0))
    }

    // Is this entry still showable? Shows/episodes can vanish under us.
    function entryValid(entry) {
        if (entry.kind === "show")
            return backend.hasShow(entry.showId)
        if (entry.kind === "episode")
            return backend.hasEpisode(entry.episodeId)
        if (entry.kind === "nowPlaying")
            return backend.playerEpisodeId !== 0
        return true
    }

    // Drop entries matching `gone`, fold neighbours that became duplicates, and
    // keep the index on the nearest surviving page at or before the current one.
    function pruneHistory(gone) {
        var list = []
        var index = 0
        for (var i = 0; i < history.length; ++i) {
            var entry = history[i]
            var keep = i === 0 || !gone(entry)
            if (keep && list.length > 0 && sameEntry(list[list.length - 1], entry))
                keep = false
            if (keep)
                list.push(entry)
            if (i <= historyIndex)
                index = Math.max(0, list.length - 1)
        }
        if (list.length === 0 || list[0].kind !== "shelf")
            list.unshift(makeEntry("shelf", 0, 0))
        var changed = list.length !== history.length || index !== historyIndex
        if (!changed)
            return
        history = list
        historyIndex = Math.min(index, list.length - 1)
        applyPage()
    }

    // Make the backend match the current entry. Instant: no animation, and warm
    // ShowView pages are only hidden/shown.
    function applyPage() {
        var entry = currentPage
        if (!entryValid(entry)) {
            pruneHistory(function(e) { return !entryValid(e) })
            return
        }
        navGuard = true
        if (entry.kind === "shelf") {
            backend.closeShow()
        } else if (entry.kind === "show") {
            if (backend.openShowId === entry.showId && backend.openEpisodeId !== 0)
                backend.closeEpisode()
            else if (backend.openShowId !== entry.showId)
                backend.openShow(entry.showId)
        } else if (entry.kind === "episode") {
            if (backend.openEpisodeId !== entry.episodeId)
                backend.openEpisode(entry.episodeId)
        }
        // queue / nowPlaying cover the content and leave the backend as is.
        navGuard = false
    }

    // Does the backend still show what the current entry asks for?
    function backendMatchesPage() {
        var entry = currentPage
        if (entry.kind === "shelf")
            return backend.openShowId === 0
        if (entry.kind === "show")
            return backend.openShowId === entry.showId && backend.openEpisodeId === 0
        if (entry.kind === "episode")
            return backend.openEpisodeId === entry.episodeId
        return true
    }

    // The backend changed pages on its own (show removed, episode vanished in a
    // refresh, ...). Re-sync after the backend call finishes: applyPage drops
    // entries that are no longer valid and reopens the right page.
    function backendMoved() {
        if (!navGuard && !backendMatchesPage())
            Qt.callLater(function() { if (!backendMatchesPage()) applyPage() })
    }

    Connections {
        target: backend
        function onOpenShowChanged() {
            rememberShow(backend.openShowId)
            backendMoved()
        }
        function onOpenEpisodeChanged() {
            backendMoved()
        }
        function onEpisodeModelDiscarded(showId) {
            forgetShow(showId)
            Qt.callLater(function() { pruneHistory(function(e) { return !entryValid(e) }) })
        }
        function onPlayerStateChanged() {
            if (backend.playerEpisodeId === 0)
                pruneHistory(function(e) { return e.kind === "nowPlaying" })
        }
    }

    // Keyboard: Esc / Backspace / Alt+Left = back, Alt+Right = forward.
    // Qt::Key_Back / Key_Forward and the mouse side buttons are caught app-wide
    // in main.cpp (WindowInputFilter) so child MouseAreas and popups cannot eat them.
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: back()
    }
    Shortcut {
        sequence: "Alt+Left"
        context: Qt.ApplicationShortcut
        onActivated: back()
    }
    Shortcut {
        sequence: "Alt+Right"
        context: Qt.ApplicationShortcut
        onActivated: forward()
    }
    Shortcut {
        sequence: "Backspace"
        context: Qt.ApplicationShortcut
        // Leave Backspace to TextField / TextInput / TextEdit / TextArea.
        enabled: {
            var item = win.activeFocusItem
            return !(item instanceof TextInput || item instanceof TextEdit)
        }
        onActivated: back()
    }
    function rememberShow(showId) {
        if (!showId)
            return
        for (var i = 0; i < warmShows.count; ++i) {
            if (warmShows.get(i).showId === showId)
                return
        }
        warmShows.append({ showId: showId })
    }

    function forgetShow(showId) {
        for (var i = warmShows.count - 1; i >= 0; --i) {
            if (warmShows.get(i).showId === showId)
                warmShows.remove(i)
        }
    }

    function activeShow() {
        for (var i = 0; i < showRepeater.count; ++i) {
            var page = showRepeater.itemAt(i)
            if (page && page.showId === backend.openShowId)
                return page
        }
        return null
    }

    function zoomFromWheel(delta) {
        if (modalOpen)
            return false
        // Now Playing: Ctrl+wheel sizes the art (and never reaches the shelf zoom).
        if (pageKind === "nowPlaying") {
            nowPlayingPage.applyArtZoom(delta)
            return true
        }
        if (pageKind !== "shelf" && pageKind !== "show")
            return false
        if (pageKind === "show") {
            var page = activeShow()
            if (page)
                page.applyZoomDelta(delta)
        } else {
            shelfView.applyZoomDelta(delta)
        }
        return true
    }

    Shortcut {
        sequence: "Space"
        // Never steal a typed space from a text field (Explore search, add feed).
        enabled: !addOpen && !(win.activeFocusItem instanceof TextInput)
        context: Qt.ApplicationShortcut
        onActivated: backend.togglePlayback()
    }
    Shortcut {
        sequences: [StandardKey.ZoomIn, "Ctrl+="]
        context: Qt.ApplicationShortcut
        enabled: !modalOpen && (pageKind === "shelf" || pageKind === "show")
        onActivated: {
            if (pageKind === "show") {
                var page = win.activeShow()
                if (page)
                    page.zoomIn()
            } else {
                shelfView.zoomIn()
            }
        }
    }
    Shortcut {
        sequence: StandardKey.ZoomOut
        context: Qt.ApplicationShortcut
        enabled: !modalOpen && (pageKind === "shelf" || pageKind === "show")
        onActivated: {
            if (pageKind === "show") {
                var page = win.activeShow()
                if (page)
                    page.zoomOut()
            } else {
                shelfView.zoomOut()
            }
        }
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
                z: 0
                visible: win.pageKind === "shelf"
                onAddRequested: {
                    feedField.text = ""
                    win.addOpen = true
                    feedField.forceActiveFocus()
                }
                onImportRequested: opmlDialog.open()
                onMarkLibraryRequested: win.confirmLibrary = true
                onQueueRequested: win.openQueuePage()
                onExploreRequested: win.openExplorePage()
            }

            // One episode page per visited show. Back only hides it, so the
            // list and its decoded covers stay warm for the next open.
            ListModel { id: warmShows }
            Repeater {
                id: showRepeater
                model: warmShows
                ShowView {
                    showId: model.showId
                    episodeModel: backend.episodesFor(model.showId)
                    anchors.fill: parent
                    z: 1
                    visible: win.pageKind === "show" && win.currentPage.showId === showId
                    onMarkAllRequested: win.confirmAll = true
                    onRemoveRequested: win.confirmRemove = true
                    onQueueRequested: win.openQueuePage()
                }
            }

            EpisodeView {
                anchors.fill: parent
                z: 2
                visible: win.pageKind === "episode"
                onMarkAllRequested: win.confirmAll = true
            }

            ExploreView {
                id: explorePage
                anchors.fill: parent
                z: 1.5
                visible: win.pageKind === "explore"
                onDismissRequested: win.back()
            }

            QueueView {
                anchors.fill: parent
                z: 1.5
                visible: win.pageKind === "queue"
                onDismissRequested: win.back()
                onClearRequested: win.confirmQueue = true
            }

            NowPlaying {
                id: nowPlayingPage
                anchors.fill: parent
                z: 3
                visible: win.pageKind === "nowPlaying" && backend.playerEpisodeId !== 0
                onDismissRequested: win.back()
            }
        }

        PlayerBar {
            Layout.fillWidth: true
            visible: backend.playerEpisodeId !== 0
            onMarkAllRequested: win.confirmAll = true
            onNowPlayingRequested: win.openNowPlayingPage()
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: addOpen || confirmAll || confirmLibrary || confirmRemove || confirmQueue
        color: Qt.rgba(0, 0, 0, 0.45)

        MouseArea {
            anchors.fill: parent
            onClicked: {
                addOpen = false
                confirmAll = false
                confirmLibrary = false
                confirmRemove = false
                confirmQueue = false
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
                    // Plain in-box hint instead of Material's floating label:
                    // shown only while the field is empty, no animation.
                    color: theme.foreground
                    font.pixelSize: theme.body
                    font.weight: Font.Normal
                    topPadding: 10
                    bottomPadding: 10
                    leftPadding: 10
                    rightPadding: 10
                    verticalAlignment: TextInput.AlignVCenter
                    Text {
                        x: feedField.leftPadding
                        anchors.verticalCenter: parent.verticalCenter
                        visible: feedField.length === 0 && feedField.preeditText.length === 0
                        text: "https://example.com/feed.xml"
                        color: theme.dim
                        font: feedField.font
                    }
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
                    text: confirmQueue
                          ? "Clear the queue? " + backend.queueCount + (backend.queueCount === 1 ? " episode" : " episodes") + " will be removed from it."
                          : confirmRemove
                          ? "Remove " + backend.openShowTitle + "? Playback history for this show goes with it."
                          : confirmLibrary
                            ? "Mark every episode of every podcast as played?"
                            : "Mark every episode of " + (backend.playerShowId !== 0 && backend.openShowId === 0
                                                          ? backend.playerShowTitle : backend.openShowTitle)
                              + " as played?"
                }
                Row {
                    spacing: 8
                    TextButton {
                        label: confirmQueue ? "Clear queue" : confirmRemove ? "Remove" : "Mark all as played"
                        onClicked: {
                            if (confirmQueue)
                                backend.clearQueue()
                            else if (confirmRemove)
                                backend.removeOpenShow()
                            else if (confirmLibrary)
                                backend.markLibraryPlayed()
                            else
                                backend.markAllPlayed(backend.openShowId !== 0 ? backend.openShowId : backend.playerShowId)
                            confirmAll = false
                            confirmLibrary = false
                            confirmRemove = false
                            confirmQueue = false
                        }
                    }
                    TextButton {
                        label: "Cancel"
                        onClicked: {
                            confirmAll = false
                            confirmLibrary = false
                            confirmRemove = false
                            confirmQueue = false
                        }
                    }
                }
            }
        }
    }

    // Shared "Save artwork…" dialog (Now Playing art, shelf covers).
    function saveArtworkAs(episodeId, showId) {
        var info = backend.artworkInfo(episodeId, showId)
        if (!info.available)
            return
        artSaveDialog.episodeId = episodeId
        artSaveDialog.showId = showId
        artSaveDialog.currentFolder = info.folderUrl
        artSaveDialog.selectedFile = info.fileUrl
        artSaveDialog.open()
    }
    FileDialog {
        id: artSaveDialog
        property real episodeId: 0
        property real showId: 0
        title: "Save artwork"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Images (*.jpg *.jpeg *.png *.webp *.gif)", "All files (*)"]
        onAccepted: backend.saveArtwork(episodeId, showId, selectedFile)
    }
    Shortcut {
        sequence: "Ctrl+0"
        context: Qt.ApplicationShortcut
        enabled: pageKind === "nowPlaying" && !modalOpen
        onActivated: backend.setNowPlayingArtScale(1.0)
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
