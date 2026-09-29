import QtQuick
import QtQuick.Controls

MenuItem {
    id: item
    font.family: "monospace"
    font.pixelSize: theme.titleSize
    font.weight: Font.Normal
    leftPadding: 14
    rightPadding: 14
    topPadding: 6
    bottomPadding: 6
    spacing: 0

    contentItem: Text {
        text: item.text
        font: item.font
        color: item.enabled ? theme.foreground : theme.dim
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        color: item.highlighted && item.enabled ? theme.selection : "transparent"
    }
}
