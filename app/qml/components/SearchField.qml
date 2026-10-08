import QtQuick
import QtQuick.Controls.Basic
import Voxwright

// Text field with a search glyph.
TextField {
    id: root

    implicitHeight: 38
    leftPadding: 38
    color: Theme.textPrimary
    placeholderTextColor: Theme.textMuted
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    selectByMouse: true

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surfaceRaised
        border.width: 1
        border.color: root.activeFocus ? Theme.accent : Theme.border
        Icon {
            x: 11
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            name: "search"
            color: Theme.textMuted
        }
    }
}
