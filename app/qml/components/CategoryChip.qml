import QtQuick
import QtQuick.Controls.Basic
import Voxwright

// Filter chip above the voice grid.
AbstractButton {
    id: root

    property bool selected: false

    hoverEnabled: true
    implicitHeight: 30
    implicitWidth: label.implicitWidth + 24
    Accessible.name: text

    background: Rectangle {
        radius: height / 2
        color: root.selected ? Theme.accent : (root.hovered ? Theme.surfaceHover : Theme.surfaceRaised)
        border.width: root.selected ? 0 : 1
        border.color: Theme.border
    }
    contentItem: Text {
        id: label
        text: root.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
        font.weight: Font.DemiBold
        color: root.selected ? "#1A0E0A" : Theme.textSecondary
    }
}
