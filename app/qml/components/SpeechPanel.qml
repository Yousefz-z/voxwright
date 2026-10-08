import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// Type text and say it into the virtual microphone with a system voice.
Rectangle {
    id: root

    required property SpeechController speech
    property string error

    objectName: "speechPanel"
    implicitHeight: column.implicitHeight + 28
    radius: Theme.radius
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    function say() {
        root.error = root.speech.speak(input.text)
        if (root.error.length === 0)
            input.selectAll()
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Icon { name: "megaphone"; width: 18; height: 18; color: Theme.textSecondary }
            Text {
                Layout.fillWidth: true
                text: qsTr("Text to speech")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            ChoiceBox {
                objectName: "speechVoice"
                Layout.preferredWidth: 220
                visible: root.speech.available
                model: root.speech.voices
                currentIndex: root.speech.voiceIndex
                Accessible.name: qsTr("Speech voice")
                onActivated: index => root.speech.voiceIndex = index
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.speech.available
            spacing: 10
            TextField {
                id: input
                objectName: "speechText"
                Layout.fillWidth: true
                placeholderText: qsTr("Type something and press Enter")
                maximumLength: 1000
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                Accessible.name: qsTr("Text to say")
                onAccepted: root.say()
                background: Rectangle {
                    implicitHeight: 36
                    radius: Theme.radiusSmall
                    color: Theme.surfaceRaised
                    border.width: 1
                    border.color: input.activeFocus ? Theme.accent : Theme.border
                }
            }
            PageButton {
                objectName: "speakButton"
                text: root.speech.busy ? qsTr("Stop") : qsTr("Say it")
                iconName: root.speech.busy ? "stop" : "megaphone"
                onClicked: root.speech.busy ? root.speech.stop() : root.say()
            }
        }
        SwitchRow {
            visible: root.speech.available
            label: qsTr("Through my voice effect")
            detail: qsTr("Off: the system voice is sent as it is. On: it goes through the current voice first.")
            checked: root.speech.throughVoice
            onToggled: on => root.speech.throughVoice = on
        }
        Text {
            visible: !root.speech.available || root.error.length > 0
            Layout.fillWidth: true
            text: root.error.length > 0 ? root.error : root.speech.problem
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: root.error.length > 0 ? Theme.danger : Theme.textMuted
        }
    }
}
