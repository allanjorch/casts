import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Button {
    id: control
    property string tip: ""
    property string caption: ""
    property color glyph: enabled ? theme.foreground : theme.muted

    flat: true
    hoverEnabled: true
    text: caption
    display: caption.length > 0 && icon.name.length > 0 ? AbstractButton.TextBesideIcon
           : caption.length > 0 ? AbstractButton.TextOnly
           : AbstractButton.IconOnly
    font.family: "monospace"
    font.pixelSize: theme.body
    font.weight: Font.Normal
    font.capitalization: Font.MixedCase
    icon.color: glyph
    Material.foreground: glyph
    icon.width: 18 * theme.textScale
    icon.height: icon.width
    spacing: 4
    leftPadding: caption.length > 0 ? 8 : 6
    rightPadding: leftPadding
    topPadding: 6
    bottomPadding: 6
    implicitWidth: Math.max(36 * theme.textScale, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: 36 * theme.textScale

    background: Rectangle {
        radius: 6
        color: !control.enabled ? "transparent"
             : control.down ? Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.16)
             : control.hovered ? Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.10)
             : "transparent"
    }

    HoverHandler {
        enabled: control.enabled
        cursorShape: Qt.PointingHandCursor
    }

    AppTip {
        visible: control.hovered && control.tip.length > 0
        text: control.tip
    }
}
