import QtQuick
import QtQuick.Controls.Basic
import Voxwright

// A secondary action button with an icon.
AbstractButton {
    id: root

    property string iconName

    hoverEnabled: true
    implicitHeight: 36
    implicitWidth: row.implicitWidth + 24
    opacity: enabled ? 1.0 : 0.45
    Accessible.name: text

    background: Rectangle {
        radius: Theme.radiusSmall
        color: root.pressed ? Theme.surfaceHover : (root.hovered ? Theme.surfaceHover : Theme.surfaceRaised)
        border.width: 1
        border.color: Theme.border
    }
    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.iconName.length > 0
                name: root.iconName
                width: 16
                height: 16
                color: Theme.textSecondary
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
            }
        }
    }
}
