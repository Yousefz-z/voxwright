import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// When the voice is sent, and keys that work in any app.
Item {
    id: page

    required property AppContext app
    readonly property AudioController audio: app.audio
    readonly property HotkeyController hotkeys: app.hotkeys

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroller.availableWidth
            spacing: 16

            Item { height: 4 }
            Text {
                Layout.leftMargin: 20
                text: qsTr("Hotkeys")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
                color: Theme.textPrimary
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("When your voice is sent")
                subtitle: qsTr("Sounds and text-to-speech are always sent; these modes only affect your microphone.")

                RowLayout {
                    spacing: 8
                    Repeater {
                        model: [qsTr("Always"), qsTr("Push-to-talk"), qsTr("Push-to-mute")]
                        CategoryChip {
                            required property string modelData
                            required property int index
                            objectName: "transmitMode" + index
                            text: modelData
                            selected: page.audio.transmitMode === index
                            onClicked: page.audio.transmitMode = index
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                    text: page.audio.transmitMode === 1 ? qsTr("Your voice is sent only while you hold the talk key.")
                        : page.audio.transmitMode === 2 ? qsTr("Your voice is sent except while you hold the talk key.")
                        : qsTr("Your voice is always sent unless you mute the microphone.")
                }
                RowLayout {
                    visible: page.audio.transmitMode !== 0
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Talk key")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        color: Theme.textPrimary
                    }
                    HotkeyField {
                        objectName: "talkKey"
                        hotkeys: page.hotkeys
                        sequence: page.hotkeys.actions.find(a => a.action === "talk").sequence
                        assign: seq => page.hotkeys.assignAction("talk", seq)
                    }
                }
                LabeledSlider {
                    visible: page.audio.transmitMode === 1
                    label: qsTr("Keep sending after release")
                    from: 0
                    to: 500
                    stepSize: 10
                    value: page.audio.releaseDelayMs
                    unit: "ms"
                    onMoved: v => page.audio.releaseDelayMs = v
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 20
                title: qsTr("Global hotkeys")
                subtitle: page.hotkeys.supported
                          ? qsTr("These keys work in any app, including games. The app you are using still receives them.")
                          : qsTr("Global hotkeys are not available on this system. Use the buttons in the window instead.")

                Repeater {
                    model: page.hotkeys.actions.filter(a => a.action !== "talk")
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: modelData.label
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            color: Theme.textPrimary
                        }
                        HotkeyField {
                            objectName: "hotkey_" + modelData.action
                            hotkeys: page.hotkeys
                            sequence: modelData.sequence
                            enabled: page.hotkeys.supported
                            assign: seq => page.hotkeys.assignAction(modelData.action, seq)
                        }
                    }
                }
            }
        }
    }
}
