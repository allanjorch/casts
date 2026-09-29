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
    title: backend.openShowId === 0 ? "Podcasts" : backend.openShowTitle
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
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (addOpen)
                addOpen = false
            else if (confirmAll)
                confirmAll = false
            else if (confirmRemove)
                confirmRemove = false
            else if (backend.openShowId !== 0)
                backend.closeShow()
        }
    }
    function zoomFromWheel(delta) {
        if (backend.openShowId !== 0 || addOpen || confirmAll || confirmRemove)
            return false
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
        enabled: backend.openShowId === 0 && !addOpen && !confirmAll && !confirmRemove
        onActivated: shelfView.zoomIn()
    }
    Shortcut {
        sequence: StandardKey.ZoomOut
        context: Qt.ApplicationShortcut
        enabled: backend.openShowId === 0 && !addOpen && !confirmAll && !confirmRemove
        onActivated: shelfView.zoomOut()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ShelfView {
            id: shelfView
            Layout.fillWidth: true
            Layout.fillHeight: true
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
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: backend.openShowId !== 0
            onMarkAllRequested: win.confirmAll = true
            onRemoveRequested: win.confirmRemove = true
        }

        PlayerBar {
            Layout.fillWidth: true
            visible: backend.playerEpisodeId !== 0
            onMarkAllRequested: win.confirmAll = true
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
