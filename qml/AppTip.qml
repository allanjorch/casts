import QtQuick
import QtQuick.Controls

ToolTip {
    id: tip
    delay: 300
    font.family: "monospace"
    font.pixelSize: theme.titleSize
    font.weight: Font.Normal
    leftPadding: 10
    rightPadding: 10
    topPadding: 5
    bottomPadding: 5
    x: parent ? Math.round((parent.width - implicitWidth) / 2) : 0
    y: -implicitHeight - 8

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 80; easing.type: Easing.OutQuad }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 60; easing.type: Easing.InQuad }
    }

    contentItem: Text {
        text: tip.text
        font: tip.font
        color: theme.foreground
    }

    background: Rectangle {
        color: theme.background
        border.color: theme.foreground
        border.width: 1
    }
}
