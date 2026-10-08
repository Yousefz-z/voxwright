import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// One sound on a board: press to play (hold for "hold" sounds), the edit
// button opens its settings.
Rectangle {
    id: root

    required property int slot
    required property string name
    required property string accentColor
    required property string iconName
    required property string hotkey
    required property bool playing
    required property int loadState // 0 loading, 1 ready, 2 failed
    required property string mode
    required property bool loop
    signal pressed(int slot)
    signal released(int slot)
    signal editRequested(int slot)

    readonly property color tint: accentColor
    readonly property bool ready: loadState === 1

    objectName: "soundTile_" + slot
    radius: Theme.radius
    color: area.containsMouse && ready ? Theme.surfaceHover : Theme.surface
    border.width: playing ? 2 : 1
    border.color: playing ? tint : Theme.border
    opacity: loadState === 2 ? 0.5 : 1.0
    Accessible.role: Accessible.Button
    Accessible.name: name

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.ready
        onPressed: root.pressed(root.slot)
        onReleased: root.released(root.slot)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                width: 40
                height: 40
                radius: 12
                color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, root.playing ? 0.45 : 0.2)
                Icon {
                    anchors.centerIn: parent
                    width: 22
                    height: 22
                    name: root.iconName
                    color: root.tint
                }
            }
            Item { Layout.fillWidth: true }
            // Animated bars while playing.
            Row {
                visible: root.playing
                height: 14
                spacing: 2
                Repeater {
                    model: 3
                    Rectangle {
                        required property int index
                        width: 3
                        height: 6
                        y: 14 - height
                        radius: 1.5
                        color: root.tint
                        SequentialAnimation on height {
                            running: root.playing
                            loops: Animation.Infinite
                            NumberAnimation { to: 14 - index * 3; duration: 180 + index * 70 }
                            NumberAnimation { to: 5; duration: 180 + index * 70 }
                        }
                    }
                }
            }
            AbstractButton {
                id: editButton
                objectName: "editSound_" + root.slot
                implicitWidth: 26
                implicitHeight: 26
                Accessible.name: qsTr("Edit %1").arg(root.name)
                onClicked: root.editRequested(root.slot)
                contentItem: Icon {
                    name: "edit"
                    width: 16
                    height: 16
                    color: editButton.hovered ? Theme.textPrimary : Theme.textMuted
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
            Layout.fillWidth: true
            text: root.loadState === 0 ? qsTr("Loading")
                : root.loadState === 2 ? qsTr("Could not load")
                : (root.hotkey.length > 0 ? root.hotkey : qsTr("No hotkey"))
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: root.loadState === 2 ? Theme.danger : Theme.textMuted
        }
    }
}
