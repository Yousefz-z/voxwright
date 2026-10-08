import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// A setting name, an optional explanation, and a switch.
RowLayout {
    id: root

    property string label
    property string detail
    property alias checked: toggle.checked
    signal toggled(bool checked)

    Layout.fillWidth: true
    spacing: 12

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Text {
            text: root.label
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.textPrimary
        }
        Text {
            visible: root.detail.length > 0
            Layout.fillWidth: true
            text: root.detail
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
        }
    }

    Switch {
        id: toggle
        Layout.alignment: Qt.AlignVCenter
        padding: 0
        implicitWidth: 40
        implicitHeight: 22
        Accessible.name: root.label
        onToggled: root.toggled(checked)
        indicator: Rectangle {
            implicitWidth: 40
            implicitHeight: 22
            x: 0
            y: (toggle.height - height) / 2
            radius: 11
            color: toggle.checked ? Theme.accent : Theme.surfaceHover
            border.width: 1
            border.color: toggle.checked ? Theme.accent : Theme.border
            Rectangle {
                x: toggle.checked ? parent.width - width - 3 : 3
                y: 3
                width: 16
                height: 16
                radius: 8
                color: Theme.textPrimary
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        contentItem: Item {}
    }
}
