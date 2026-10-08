import QtQuick
import QtQuick.Controls.Basic
import Voxwright

// Large on/off toggle for the bottom bar.
AbstractButton {
    id: root

    property string iconName
    property color onColor: Theme.accent
    /// Icon only, for narrow windows (the name shows as a tooltip).
    property bool compact: false
    /// Width with the label shown, whether or not it is shown now.
    readonly property real fullWidth: Math.max(112, Math.ceil(20 + row.spacing + nameMetrics.advanceWidth) + 2 * padding)

    checkable: true
    hoverEnabled: true
    padding: 10
    implicitHeight: 48
    implicitWidth: compact ? 48 : fullWidth
    ToolTip.visible: compact && hovered
    ToolTip.text: text
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

    TextMetrics {
        id: nameMetrics
        text: root.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        font.weight: Font.DemiBold
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: root.iconName
                color: root.checked ? root.onColor : Theme.textSecondary
            }
            Column {
                visible: !root.compact
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
