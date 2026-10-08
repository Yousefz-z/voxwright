import QtQuick
import QtQuick.Controls.Basic
import Voxwright

// A small round button showing only an icon; its name is the tooltip and
// what screen readers announce.
AbstractButton {
    id: root

    property string iconName
    property color iconColor: Theme.textSecondary
    property int iconSize: 16

    implicitWidth: 28
    implicitHeight: 28
    hoverEnabled: true
    opacity: enabled ? 1.0 : 0.35
    Accessible.name: text
    ToolTip.visible: hovered && text.length > 0
    ToolTip.text: text
    ToolTip.delay: 400

    background: Rectangle {
        radius: width / 2
        color: root.pressed || root.hovered ? Theme.surfaceHover : "transparent"
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            width: root.iconSize
            height: root.iconSize
            name: root.iconName
            color: root.iconColor
        }
    }
}
