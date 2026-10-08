import QtQuick
import QtQuick.Controls.Basic
import Voxwright

// Large on/off toggle for the bottom bar.
AbstractButton {
    id: root

    property string iconName
    property color onColor: Theme.accent

    checkable: true
    hoverEnabled: true
    implicitHeight: 48
    implicitWidth: Math.max(128, row.implicitWidth + 28)
    Accessible.name: text
    Accessible.checkable: true
    Accessible.checked: checked

    background: Rectangle {
        radius: Theme.radius
        color: root.checked ? Qt.rgba(root.onColor.r, root.onColor.g, root.onColor.b, 0.16)
                            : (root.hovered ? Theme.surfaceHover : Theme.surfaceRaised)
        border.width: 1
        border.color: root.checked ? root.onColor : Theme.border
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 10
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: root.iconName
                color: root.checked ? root.onColor : Theme.textSecondary
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: root.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                }
                Text {
                    text: root.checked ? qsTr("On") : qsTr("Off")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: root.checked ? root.onColor : Theme.textMuted
                }
            }
        }
    }
}
