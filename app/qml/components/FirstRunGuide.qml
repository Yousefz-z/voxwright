import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// The setup guide shown on first start: microphone, virtual microphone
// (with a check that it works), headphones, and where to go next.
Popup {
    id: root

    required property AppContext app
    property int step: 0
    readonly property int lastStep: 4

    objectName: "firstRunGuide"
    modal: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    width: Math.min(620, parent ? parent.width - 40 : 620)
    padding: 28

    function finish() {
        root.app.micCheck.cancel()
        root.app.system.finishFirstRun()
        root.close()
    }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: "#B3000000" }

    contentItem: ColumnLayout {
        spacing: 18

        // Progress dots.
        Row {
            spacing: 6
            Repeater {
                model: root.lastStep + 1
                Rectangle {
                    required property int index
                    width: index === root.step ? 22 : 8
                    height: 8
                    radius: 4
                    color: index <= root.step ? Theme.accent : Theme.surfaceHover
                    Behavior on width { NumberAnimation { duration: 120 } }
                }
            }
        }

        Text {
            objectName: "guideTitle"
            Layout.fillWidth: true
            text: [qsTr("Welcome to Voxwright"), qsTr("Your microphone"), qsTr("The virtual microphone"),
                   qsTr("Hearing yourself"), qsTr("You are set")][root.step]
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Bold
            color: Theme.textPrimary
        }
        Text {
            Layout.fillWidth: true
            text: [
                qsTr("Voxwright changes your voice as you speak and plays it into a virtual microphone that Discord, Zoom, Teams, OBS, and games can use. This takes a minute."),
                qsTr("Pick the microphone you speak into, then say something: the bar should move."),
                root.app.audio.virtualCableFound
                    ? qsTr("Voxwright plays your changed voice into \"%1\". In your chat app or game, choose \"%2\" as the microphone.").arg(root.app.audio.virtualMicName).arg(root.app.audio.chatAppMicrophoneName)
                    : qsTr("Other apps hear Voxwright through a virtual audio cable. Install %1 (free), then press Check again. You may need to restart the computer after installing it.").arg(root.app.audio.virtualCableProduct),
                qsTr("Choose headphones to hear your changed voice and sounds. Use headphones rather than speakers so your microphone does not pick yourself up."),
                qsTr("Pick a voice on the Voices page and set keys for push-to-talk and your favorite sounds on the Hotkeys page. You can run this guide again from Settings.")
            ][root.step]
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.textSecondary
            lineHeight: 1.2
        }

        // Step 1: microphone.
        ColumnLayout {
            visible: root.step === 1
            Layout.fillWidth: true
            spacing: 10
            DevicePicker {
                label: qsTr("Microphone")
                iconName: "mic"
                model: root.app.audio.inputDevices
                currentId: root.app.audio.inputDeviceId
                onPicked: id => root.app.audio.inputDeviceId = id
            }
            LevelMeter {
                Layout.fillWidth: true
                level: root.app.audio.inputLevel
            }
        }

        // Step 2: virtual microphone.
        VirtualMicTest {
            visible: root.step === 2
            Layout.fillWidth: true
            app: root.app
        }

        // Step 3: headphones.
        ColumnLayout {
            visible: root.step === 3
            Layout.fillWidth: true
            spacing: 10
            DevicePicker {
                label: qsTr("Headphones")
                iconName: "headphones"
                model: root.app.audio.monitorDevices
                currentId: root.app.audio.monitorDeviceId
                onPicked: id => root.app.audio.monitorDeviceId = id
            }
            SwitchRow {
                objectName: "guideHearMyself"
                label: qsTr("Hear myself")
                detail: qsTr("You can turn this on and off from the bottom bar at any time.")
                checked: root.app.audio.hearMyself
                onToggled: on => root.app.audio.hearMyself = on
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 10
            PageButton {
                objectName: "guideSkip"
                visible: root.step < root.lastStep
                text: qsTr("Skip setup")
                onClicked: root.finish()
            }
            Item { Layout.fillWidth: true }
            PageButton {
                objectName: "guideBack"
                visible: root.step > 0
                text: qsTr("Back")
                onClicked: root.step -= 1
            }
            Button {
                id: next
                objectName: "guideNext"
                text: root.step === 0 ? qsTr("Start") : root.step === root.lastStep ? qsTr("Done") : qsTr("Next")
                onClicked: {
                    if (root.step === root.lastStep)
                        root.finish()
                    else
                        root.step += 1
                }
                contentItem: Text {
                    text: next.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    font.weight: Font.DemiBold
                    color: "#FFFFFF"
                }
                background: Rectangle {
                    implicitWidth: 110
                    implicitHeight: 36
                    radius: Theme.radiusSmall
                    color: next.pressed ? Theme.accentPressed : Theme.accent
                }
            }
        }
    }
}
