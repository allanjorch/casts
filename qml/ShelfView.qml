import QtQuick
import QtQuick.Controls

Item {
    id: shelf
    signal addRequested()
    signal importRequested()
    signal markLibraryRequested()
    signal queueRequested()
    signal exploreRequested()

    readonly property int columnFloor: 112
    readonly property real galleryGap: 12
    readonly property real galleryWidth: Math.max(1, width - 32)
    readonly property int columnsFit: Math.max(1, Math.floor((galleryWidth + galleryGap) / (columnFloor + galleryGap)))
    readonly property int columns: Math.max(1, Math.min(10, Math.min(backend.shelfColumns, columnsFit)))
    readonly property real cellWidth: galleryWidth / columns
    readonly property real coverSize: Math.max(columnFloor, cellWidth - galleryGap)
    readonly property real cellHeight: coverSize + theme.titleSize * 2.4 + 22
    readonly property real caughtUpVeil: 0.38
    // Refresh gray-out disabled (same spirit as enabled: !backend.busy comments).
    // Stronger than caught-up while refresh-all runs; cleared with backend.busy on success or failure.
    // readonly property real refreshVeil: 0.62
    // Five steps. 0 matches the original 44px list cover. Keep the count in step with shelfListSize.
    readonly property var listCoverSteps: [44, 64, 88, 120, 160]
    readonly property int listCoverBase: listCoverSteps[Math.max(0, Math.min(backend.shelfListSize, listCoverSteps.length - 1))]
    readonly property real listCover: listCoverBase * theme.textScale
    readonly property real listRowHeight: listCover + 20 * theme.textScale

    property real zoomPending: 0

    function zoomIn() {
        if (backend.shelfView === "list")
            backend.setShelfListSize(backend.shelfListSize + 1)
        else
            backend.setShelfColumns(Math.min(backend.shelfColumns, columns) - 1)
    }

    function zoomOut() {
        if (backend.shelfView === "list")
            backend.setShelfListSize(backend.shelfListSize - 1)
        else if (backend.shelfColumns < 10)
            backend.setShelfColumns(backend.shelfColumns + 1)
    }

    function takeWheel(wheel) {
        if (!(wheel.modifiers & Qt.ControlModifier)) {
            wheel.accepted = false
            return
        }
        var delta = wheel.angleDelta.y
        if (delta === 0 && wheel.pixelDelta.y !== 0)
            delta = wheel.pixelDelta.y > 0 ? 120 : -120
        applyZoomDelta(delta)
        wheel.accepted = true
    }

    function applyZoomDelta(delta) {
        zoomPending += delta
        while (zoomPending >= 120) {
            zoomPending -= 120
            zoomIn()
        }
        while (zoomPending <= -120) {
            zoomPending += 120
            zoomOut()
        }
    }

    function unplayedLine(count) {
        if (count <= 0)
            return "Caught up"
        if (count === 1)
            return "1 unplayed"
        return count + " unplayed"
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

        SelectableText {
            id: title
            anchors.left: parent.left
            anchors.leftMargin: 22
            anchors.verticalCenter: parent.verticalCenter
            text: "Podcasts"
            color: theme.foreground
            font.pixelSize: theme.display
            wrapMode: TextEdit.NoWrap
        }
        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            // Refresh busy used to disable these controls; leave them usable so
            // shows can open while refresh runs. Spinner / StatusNote still use busy.
            // No podcasts yet: only Add and Import. The eye stays available once
            // any show exists, even if the unplayed filter hides every cover.
            readonly property bool hasShows: backend.showCount > 0
            IconButton {
                visible: actions.hasShows
                icon.name: backend.shelfShowAll ? "view-reveal-symbolic" : "view-conceal-symbolic"
                // tip: backend.busy ? "Refreshing…" : …
                tip: backend.shelfShowAll ? "Show unplayed only" : "Show all"
                // enabled: !backend.busy
                enabled: true
                onClicked: backend.setShelfShowAll(!backend.shelfShowAll)
            }
            IconButton {
                visible: actions.hasShows
                icon.name: backend.shelfView === "list" ? "view-list-symbolic" : "view-grid-symbolic"
                // tip: backend.busy ? "Refreshing…" : …
                tip: backend.shelfView === "list" ? "Show as gallery" : "Show as list"
                // enabled: !backend.busy
                enabled: true
                onClicked: backend.setShelfView(backend.shelfView === "list" ? "gallery" : "list")
            }
            IconButton {
                id: refreshButton
                visible: actions.hasShows
                icon.name: "view-refresh-symbolic"
                // tip: backend.busy ? "Refreshing…" : …
                tip: backend.lastRefreshLabel.length
                     ? ("Refresh — " + backend.lastRefreshLabel + "  ·  Ctrl+click reloads artwork")
                     : "Refresh  ·  Ctrl+click reloads artwork"
                // enabled: !backend.busy
                enabled: true
                property bool reloadArtwork: false
                onPressedChanged: {
                    if (pressed)
                        reloadArtwork = backend.controlHeld()
                }
                onClicked: backend.refreshAll(reloadArtwork)

                // Spin only the glyph so the hover/press background stays still.
                RotationAnimator {
                    target: refreshButton.contentItem
                    from: 0
                    to: 360
                    duration: 900
                    loops: Animation.Infinite
                    running: backend.busy && refreshButton.visible
                    onRunningChanged: {
                        if (!running && refreshButton.contentItem)
                            refreshButton.contentItem.rotation = 0
                    }
                }
            }
            IconButton {
                visible: actions.hasShows
                icon.source: "qrc:/icons/queue-symbolic.svg"
                tip: backend.queueCount > 0 ? "Queue (" + backend.queueCount + ")" : "Queue"
                onClicked: shelf.queueRequested()
            }
            IconButton {
                visible: actions.hasShows
                icon.name: "check-plain-symbolic"
                // enabled: !backend.busy && grid.count > 0
                enabled: grid.count > 0
                // tip: backend.busy ? "Refreshing…" : …
                tip: grid.count > 0 ? "Mark all as played" : "Nothing to mark"
                onClicked: shelf.markLibraryRequested()
            }
            IconButton {
                icon.name: "system-search-symbolic"
                tip: "Explore podcasts"
                onClicked: shelf.exploreRequested()
            }
            IconButton {
                icon.name: "list-add-symbolic"
                tip: "Add feed"
                // enabled: !backend.busy
                enabled: true
                onClicked: shelf.addRequested()
            }
            IconButton {
                icon.name: "document-open-symbolic"
                tip: "Import OPML"
                // enabled: !backend.busy
                enabled: true
                onClicked: shelf.importRequested()
            }
        }
        StatusNote {
            anchors.left: title.right
            anchors.right: actions.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            font.pixelSize: theme.bodySmall
        }
    }

    SelectableText {
        visible: grid.count === 0
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 520)
        horizontalAlignment: TextEdit.AlignHCenter
        wrapMode: TextEdit.Wrap
        color: theme.dim
        font.pixelSize: theme.body
        text: backend.shelfShowAll
              ? "Add a feed to start. An OPML export from Podcast Addict brings the shows with it."
              : "No shows with unplayed episodes."
    }

    GridView {
        id: grid
        visible: backend.shelfView !== "list"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        leftMargin: 16
        rightMargin: 16
        topMargin: 4
        clip: true
        cellWidth: shelf.cellWidth
        cellHeight: shelf.cellHeight
        boundsBehavior: Flickable.StopAtBounds
        FastWheel { }
        model: backend.shows
        cacheBuffer: height

        delegate: Item {
            width: grid.cellWidth
            height: grid.cellHeight

            Column {
                width: shelf.coverSize
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8

                Item {
                    id: cover
                    width: parent.width
                    height: width

                    Image {
                        anchors.fill: parent
                        source: model.cover
                        fillMode: Image.PreserveAspectCrop
                        // Fixed decode size: resizing the window never reloads; the GPU scales.
                        // Sync only when prewarmed in memory (paints with the first frame).
                        asynchronous: !coverCache.ready(model.cover, coverCache.tileSide)
                        cache: true
                        smooth: true
                        mipmap: true
                        sourceSize.width: coverCache.tileSide / Screen.devicePixelRatio
                        sourceSize.height: coverCache.tileSide / Screen.devicePixelRatio
                        visible: model.cover !== ""
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: model.cover === ""
                        color: theme.selection
                        Text {
                            anchors.centerIn: parent
                            text: model.title.length ? model.title.charAt(0) : "?"
                            color: model.unheard > 0 ? theme.foreground : theme.dim
                            font.pixelSize: theme.display * 1.6
                            font.weight: Font.Normal
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: model.unheard === 0
                        color: theme.background
                        opacity: shelf.caughtUpVeil
                    }
                    Rectangle {
                        visible: model.unheard > 0
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 8
                        radius: 3
                        color: theme.accent
                        height: 22 * theme.textScale
                        width: Math.max(height, countLabel.implicitWidth + 12)
                        Text {
                            id: countLabel
                            anchors.centerIn: parent
                            text: model.unheard > 99 ? "99+" : model.unheard
                            color: inkOnAccent(theme.accent)
                            font.pixelSize: theme.caption
                            font.weight: Font.Normal
                        }
                    }
                    // Refresh veil: left inactive so covers stay undimmed while refresh runs.
                    // Rectangle {
                    //     anchors.fill: parent
                    //     visible: backend.busy
                    //     color: theme.background
                    //     opacity: shelf.refreshVeil
                    // }
                }

                Text {
                    width: parent.width
                    text: model.title
                    color: model.unheard === 0 ? theme.dim : theme.foreground
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: theme.titleSize
                    font.weight: Font.Normal
                }
            }

            MouseArea {
                anchors.fill: parent
                // enabled: !backend.busy
                enabled: true
                hoverEnabled: true
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton)
                        shelf.openCoverMenu(model.showId)
                    else
                        win.openShowPage(model.showId)
                }
                onWheel: (wheel) => shelf.takeWheel(wheel)
                Rectangle {
                    anchors.fill: cover
                    z: -1
                    color: theme.selection
                    opacity: parent.containsMouse ? 0.28 : 0
                }
            }
        }
    }

    ListView {
        id: showList
        visible: backend.shelfView === "list"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        leftMargin: 12
        rightMargin: 12
        topMargin: 4
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        FastWheel { }
        model: backend.shows
        cacheBuffer: height
        spacing: 2

        delegate: Item {
            width: showList.width
            height: shelf.listRowHeight

            Rectangle {
                anchors.fill: parent
                radius: 4
                color: theme.selection
                opacity: rowMouse.containsMouse ? 0.35 : 0
            }

            Item {
                id: rowArt
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: shelf.listCover
                height: width
                clip: true

                Image {
                    id: rowCover
                    anchors.fill: parent
                    source: model.cover
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: !coverCache.ready(model.cover, coverCache.rowSide)
                    cache: true
                    smooth: true
                    mipmap: true
                    sourceSize.width: coverCache.rowSide / Screen.devicePixelRatio
                    sourceSize.height: coverCache.rowSide / Screen.devicePixelRatio
                    visible: model.cover !== ""
                }
                Rectangle {
                    anchors.fill: parent
                    visible: model.cover === ""
                    color: theme.selection
                    Text {
                        anchors.centerIn: parent
                        text: model.title.length ? model.title.charAt(0) : "?"
                        color: model.unheard > 0 ? theme.foreground : theme.dim
                        font.pixelSize: theme.heading
                        font.weight: Font.Normal
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    visible: model.unheard === 0
                    color: theme.background
                    opacity: shelf.caughtUpVeil
                }
                Rectangle {
                    visible: model.unheard > 0
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: parent.width >= 96 ? 8 : 4
                    radius: 3
                    color: theme.accent
                    height: Math.min(22 * theme.textScale, parent.height * 0.5)
                    width: Math.max(height, rowBadgeText.implicitWidth + 12)
                    Text {
                        id: rowBadgeText
                        anchors.centerIn: parent
                        text: model.unheard > 99 ? "99+" : model.unheard
                        color: inkOnAccent(theme.accent)
                        font.pixelSize: theme.caption
                        font.weight: Font.Normal
                    }
                }
                // Refresh veil: left inactive so list covers stay undimmed while refresh runs.
                // Rectangle {
                //     anchors.fill: parent
                //     visible: backend.busy
                //     color: theme.background
                //     opacity: shelf.refreshVeil
                // }
            }
            Column {
                anchors.left: rowArt.right
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 14
                anchors.rightMargin: 16
                spacing: 2
                Text {
                    width: parent.width
                    text: model.title
                    color: model.unheard === 0 ? theme.dim : theme.foreground
                    elide: Text.ElideRight
                    font.pixelSize: theme.titleSize
                    font.weight: Font.Normal
                }
                Text {
                    width: parent.width
                    text: shelf.unplayedLine(model.unheard)
                    color: theme.dim
                    elide: Text.ElideRight
                    font.pixelSize: theme.caption
                    font.weight: Font.Normal
                }
            }
            MouseArea {
                id: rowMouse
                anchors.fill: parent
                // enabled: !backend.busy
                enabled: true
                hoverEnabled: true
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton)
                        shelf.openCoverMenu(model.showId)
                    else
                        win.openShowPage(model.showId)
                }
                onWheel: (wheel) => shelf.takeWheel(wheel)
            }
        }
    }

    // Right-click on a show cover (gallery tile or list row).
    property real coverMenuShow: 0
    function openCoverMenu(showId) {
        coverMenuShow = showId
        coverMenu.info = backend.artworkInfo(0, showId)
        coverMenu.popup()
    }
    AppMenu {
        id: coverMenu
        property var info: ({})
        AppMenuItem {
            text: "Copy"
            enabled: coverMenu.info.available === true
            onTriggered: backend.copyArtwork(0, shelf.coverMenuShow)
        }
        AppMenuItem {
            text: "Save artwork…"
            enabled: coverMenu.info.available === true
            onTriggered: win.saveArtworkAs(0, shelf.coverMenuShow)
        }
        AppMenuItem {
            text: "Copy image URL"
            enabled: coverMenu.info.hasUrl === true
            onTriggered: backend.copyArtworkUrl(0, shelf.coverMenuShow)
        }
    }

    QuietScroll {
        view: grid
        loadedCount: grid.count
        anchors.top: grid.top
        anchors.bottom: grid.bottom
        anchors.right: parent.right
        anchors.rightMargin: 2
    }
    QuietScroll {
        view: showList
        loadedCount: showList.count
        anchors.top: showList.top
        anchors.bottom: showList.bottom
        anchors.right: parent.right
        anchors.rightMargin: 2
    }

    function inkOnAccent(swatch) {
        var accentLuma = 0.299 * swatch.r + 0.587 * swatch.g + 0.114 * swatch.b
        var backgroundLuma = 0.299 * theme.background.r + 0.587 * theme.background.g + 0.114 * theme.background.b
        var foregroundLuma = 0.299 * theme.foreground.r + 0.587 * theme.foreground.g + 0.114 * theme.foreground.b
        return Math.abs(accentLuma - backgroundLuma) > Math.abs(accentLuma - foregroundLuma)
                ? theme.background : theme.foreground
    }

}
