import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Menu {
    id: menu
    font.family: "monospace"
    font.pixelSize: theme.titleSize
    font.weight: Font.Normal
    padding: 1
    topPadding: 4
    bottomPadding: 4
    Material.elevation: 0

    // List content reports no implicit width, so the panel would collapse to the border.
    implicitWidth: {
        var widest = 0
        for (var i = 0; i < count; ++i) {
            var entry = itemAt(i)
            if (entry)
                widest = Math.max(widest, entry.implicitWidth)
        }
        return Math.ceil(widest + leftPadding + rightPadding)
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 90; easing.type: Easing.OutQuad }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 70; easing.type: Easing.InQuad }
    }

    background: Rectangle {
        color: theme.background
        border.color: theme.foreground
        border.width: 1
    }

    Overlay.modal: Rectangle { color: "transparent" }
    Overlay.modeless: Rectangle { color: "transparent" }
}
