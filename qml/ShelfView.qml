import QtQuick
import QtQuick.Controls

Item {
    id: shelf
    signal addRequested()
    signal importRequested()

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 64 * theme.textScale

        Text {
            id: title
            anchors.left: parent.left
            anchors.leftMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            text: "Podcasts"
            color: theme.foreground
            font.pixelSize: 22 * theme.textScale
        }
        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            spacing: 18
            HeaderAction { label: "Add feed"; onClicked: shelf.addRequested() }
            HeaderAction { label: "Import OPML"; onClicked: shelf.importRequested() }
            HeaderAction { label: "Refresh"; onClicked: backend.refreshAll() }
        }
        Text {
            anchors.left: title.right
            anchors.right: actions.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            text: backend.status
            color: theme.muted
            font.pixelSize: 13 * theme.textScale
            elide: Text.ElideRight
            visible: backend.status.length > 0
        }
    }

    Text {
        visible: grid.count === 0
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 520)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: theme.muted
        font.pixelSize: 16 * theme.textScale
        text: "Add a feed to start. An OPML export from Podcast Addict brings the shows with it."
    }

    GridView {
        id: grid
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        leftMargin: 16
        rightMargin: 16
        topMargin: 4
            cellWidth: 196 * theme.textScale
            cellHeight: 250 * theme.textScale
            boundsBehavior: Flickable.StopAtBounds
            model: backend.shows
            cacheBuffer: height

            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight

                Column {
                    width: 168 * theme.textScale
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8 * theme.textScale

                    Item {
                        id: cover
                        width: parent.width
                        height: width

                        Image {
                            anchors.fill: parent
                            source: model.cover
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 336
                            sourceSize.height: 336
                            visible: model.cover !== ""
                        }
                        Rectangle {
                            anchors.fill: parent
                            visible: model.cover === ""
                            color: theme.selection
                            Text {
                                anchors.centerIn: parent
                                text: model.title.length ? model.title.charAt(0) : "?"
                                color: theme.foreground
                                font.pixelSize: 40 * theme.textScale
                            }
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
                                font.pixelSize: 12 * theme.textScale
                                font.bold: true
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: model.title
                        color: theme.foreground
                        opacity: model.unheard > 0 ? 1 : 0.45
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pixelSize: 14 * theme.textScale
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.openShow(model.showId)
                    Rectangle {
                        anchors.fill: cover
                        anchors.margins: 0
                        z: -1
                        color: theme.selection
                        opacity: parent.containsMouse ? 0.28 : 0
                    }
                }
            }
        }

    function inkOnAccent(swatch) {
        var accentLuma = 0.299 * swatch.r + 0.587 * swatch.g + 0.114 * swatch.b
        var backgroundLuma = 0.299 * theme.background.r + 0.587 * theme.background.g + 0.114 * theme.background.b
        var foregroundLuma = 0.299 * theme.foreground.r + 0.587 * theme.foreground.g + 0.114 * theme.foreground.b
        return Math.abs(accentLuma - backgroundLuma) > Math.abs(accentLuma - foregroundLuma)
                ? theme.background : theme.foreground
    }

    component HeaderAction: Text {
        id: action
        property string label: ""
        signal clicked()
        text: label
        color: theme.foreground
        font.pixelSize: 14 * theme.textScale
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: action.clicked()
        }
    }
}
