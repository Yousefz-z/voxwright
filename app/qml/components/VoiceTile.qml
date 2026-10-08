import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// One voice in the grid.
AbstractButton {
    id: root

    required property string voiceId
    required property string name
    required property string category
    required property string iconName
    required property string accentColor
    required property bool favorite
    required property bool active
    signal favoriteToggled(string voiceId)

    readonly property color tint: accentColor

    objectName: "voiceTile_" + voiceId
    hoverEnabled: true
    Accessible.name: name
    Accessible.description: category

    background: Rectangle {
        radius: Theme.radius
        color: root.hovered ? Theme.surfaceHover : Theme.surface
        border.width: root.active ? 2 : 1
        border.color: root.active ? Theme.accent : Theme.border
    }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                width: 44
                height: 44
                radius: 22
                color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.22)
                Icon {
                    anchors.centerIn: parent
                    width: 24
                    height: 24
                    name: root.iconName
                    color: root.tint
                }
            }
            Item { Layout.fillWidth: true }
            AbstractButton {
                id: star
                implicitWidth: 28
                implicitHeight: 28
                Accessible.name: root.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                onClicked: root.favoriteToggled(root.voiceId)
                contentItem: Icon {
                    name: "star"
                    filled: root.favorite
                    color: root.favorite ? Theme.warning : (star.hovered ? Theme.textSecondary : Theme.textMuted)
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: root.name
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
            color: Theme.textPrimary
        }
        Text {
            text: root.category
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
        }
    }
    padding: 14
}
