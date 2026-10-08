import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// Settings of one sound: name, play mode, volume, routing, and hotkey.
Popup {
    id: root

    required property SoundboardController soundboard
    required property HotkeyController hotkeys
    property int slot: -1
    property var details: ({})

    function openFor(newSlot) {
        root.slot = newSlot
        root.details = root.soundboard.soundDetails(newSlot)
        root.open()
    }
    function change(key, value) {
        const changes = {}
        changes[key] = value
        root.soundboard.updateSound(root.slot, changes)
        root.details = root.soundboard.soundDetails(root.slot)
    }

    objectName: "soundEditor"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 420
    padding: 20

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: "#99000000" }

    contentItem: ColumnLayout {
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Sound settings")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            AbstractButton {
                implicitWidth: 24
                implicitHeight: 24
                Accessible.name: qsTr("Close")
                onClicked: root.close()
                contentItem: Icon { name: "close"; width: 16; height: 16; color: Theme.textMuted }
            }
        }

        TextField {
            id: nameField
            objectName: "soundName"
            Layout.fillWidth: true
            text: root.details.name !== undefined ? root.details.name : ""
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            onEditingFinished: root.change("name", text)
            background: Rectangle {
                implicitHeight: 36
                radius: Theme.radiusSmall
                color: Theme.surfaceRaised
                border.width: 1
                border.color: nameField.activeFocus ? Theme.accent : Theme.border
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("When pressed")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: Theme.textSecondary
            }
            ComboBox {
                id: modeBox
                Layout.preferredWidth: 200
                textRole: "label"
                valueRole: "mode"
                model: [
                    { mode: "restart", label: qsTr("Play, restart") },
                    { mode: "toggle", label: qsTr("Play, stop") },
                    { mode: "pause", label: qsTr("Play, pause") },
                    { mode: "overlap", label: qsTr("Play over itself") },
                    { mode: "hold", label: qsTr("Play while held") }
                ]
                currentIndex: Math.max(0, indexOfValue(root.details.mode))
                onActivated: root.change("mode", currentValue)
                Accessible.name: qsTr("Play mode")
            }
        }

        SwitchRow {
            label: qsTr("Loop")
            checked: root.details.loop === true
            onToggled: on => root.change("loop", on)
        }
        LabeledSlider {
            label: qsTr("Volume")
            from: -30
            to: 6
            stepSize: 0.5
            value: root.details.gainDb !== undefined ? root.details.gainDb : 0
            unit: "dB"
            decimals: 1
            onMoved: v => root.change("gainDb", v)
        }
        SwitchRow {
            label: qsTr("Mute my voice while it plays")
            checked: root.details.muteVoice === true
            onToggled: on => root.change("muteVoice", on)
        }
        SwitchRow {
            label: qsTr("Silence other sounds while it plays")
            checked: root.details.muteOthers === true
            onToggled: on => root.change("muteOthers", on)
        }
        SwitchRow {
            label: qsTr("Stop other sounds when it starts")
            checked: root.details.stopOthers === true
            onToggled: on => root.change("stopOthers", on)
        }
        SwitchRow {
            label: qsTr("Others only")
            detail: qsTr("Play it into the virtual microphone but not in your headphones.")
            checked: root.details.muteForMe === true
            onToggled: on => root.change("muteForMe", on)
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 8
                text: qsTr("Hotkey")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: Theme.textSecondary
            }
            HotkeyField {
                hotkeys: root.hotkeys
                sequence: root.details.hotkey !== undefined ? root.details.hotkey : ""
                assign: seq => root.soundboard.setSoundHotkey(root.slot, seq)
            }
        }

        Button {
            id: removeButton
            Layout.fillWidth: true
            text: qsTr("Remove sound")
            onClicked: {
                root.soundboard.removeSound(root.slot)
                root.close()
            }
            contentItem: RowLayout {
                spacing: 8
                Item { Layout.fillWidth: true }
                Icon { name: "trash"; width: 16; height: 16; color: Theme.danger }
                Text {
                    text: removeButton.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    color: Theme.danger
                }
                Item { Layout.fillWidth: true }
            }
            background: Rectangle {
                implicitHeight: 36
                radius: Theme.radiusSmall
                color: removeButton.hovered ? Theme.surfaceHover : Theme.surfaceRaised
                border.width: 1
                border.color: Theme.border
            }
        }
    }
}
