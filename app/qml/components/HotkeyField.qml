import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// Shows a hotkey and records a new one: click, then press the keys.
// Escape cancels, Backspace clears.
ColumnLayout {
    id: root

    required property HotkeyController hotkeys
    property string sequence
    property string display: hotkeys.displayText(sequence)
    property string error
    property bool recording: false
    /// Return "" to accept the new sequence, or a message to show.
    property var assign: function(sequence) { return "" }

    spacing: 4

    function finish(newSequence) {
        const problem = root.assign(newSequence)
        root.error = problem
        root.recording = false
        if (problem.length === 0)
            root.sequence = newSequence
    }

    Rectangle {
        id: box
        objectName: "hotkeyBox"
        Layout.preferredWidth: 180
        implicitHeight: 34
        radius: Theme.radiusSmall
        color: root.recording ? Theme.accentSoft : Theme.surfaceRaised
        border.width: 1
        border.color: root.recording ? Theme.accent : (root.error.length > 0 ? Theme.danger : Theme.border)
        focus: root.recording
        Accessible.role: Accessible.Button
        Accessible.name: root.recording ? qsTr("Press the new hotkey") : qsTr("Hotkey: %1").arg(label.text)

        Text {
            id: label
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: root.recording ? qsTr("Press keys...")
                : (root.sequence.length > 0 ? root.display : qsTr("Not set"))
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: root.recording ? Theme.accent : (root.sequence.length > 0 ? Theme.textPrimary : Theme.textMuted)
        }
        MouseArea {
            anchors.fill: parent
            onClicked: {
                root.error = ""
                root.recording = true
                box.forceActiveFocus()
            }
        }
        Keys.onPressed: event => {
            if (!root.recording)
                return
            event.accepted = true
            if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) {
                root.recording = false
                return
            }
            if ((event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) && event.modifiers === Qt.NoModifier) {
                root.finish("")
                return
            }
            const text = root.hotkeys.sequenceFromKey(event.key, event.modifiers)
            if (text.length > 0)
                root.finish(text)
        }
        onActiveFocusChanged: if (!activeFocus) root.recording = false
    }
    Text {
        visible: root.error.length > 0
        Layout.preferredWidth: 260
        text: root.error
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSmall
        color: Theme.danger
    }
}
