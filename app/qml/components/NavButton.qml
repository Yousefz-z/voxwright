import QtQuick
import QtQuick.Controls.Basic
import Voxwright

// Sidebar navigation entry.
AbstractButton {
    id: root

    property string iconName
    property bool current: false

    implicitHeight: 40
    implicitWidth: 180
    hoverEnabled: true
    Accessible.name: text

    background: Rectangle {
        radius: Theme.radiusSmall
        color: root.current ? Theme.accentSoft : (root.hovered ? Theme.surfaceHover : "transparent")
    }

    contentItem: Row {
        leftPadding: 12
        spacing: 12
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            color: root.current ? Theme.accent : Theme.textSecondary
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: root.current ? Font.DemiBold : Font.Normal
            color: root.current ? Theme.textPrimary : Theme.textSecondary
        }
    }
}
