import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// Runs the virtual microphone check and shows what it found.
ColumnLayout {
    id: root

    required property AppContext app
    readonly property VirtualMicCheck check: app.micCheck

    spacing: 8

    RowLayout {
        spacing: 10
        PageButton {
            objectName: "testVirtualMic"
            visible: root.app.audio.virtualCableFound
            enabled: root.check.state !== VirtualMicCheck.Running
            text: root.check.state === VirtualMicCheck.Running ? qsTr("Listening...") : qsTr("Test the virtual microphone")
            iconName: "cable"
            onClicked: root.check.start(root.app.audio.chatAppMicrophoneName)
        }
        PageButton {
            visible: !root.app.audio.virtualCableFound
            text: qsTr("Get %1").arg(root.app.audio.virtualCableProduct)
            iconName: "external"
            onClicked: root.app.runAction("get-virtual-cable")
        }
        PageButton {
            visible: !root.app.audio.virtualCableFound
            text: qsTr("Check again")
            iconName: "reset"
            onClicked: root.app.audio.refreshDevices()
        }
    }
    RowLayout {
        visible: root.check.message.length > 0
        Layout.fillWidth: true
        spacing: 8
        Icon {
            Layout.alignment: Qt.AlignTop
            name: root.check.state === VirtualMicCheck.Passed ? "check"
                  : root.check.state === VirtualMicCheck.Running ? "clock" : "warning"
            color: root.check.state === VirtualMicCheck.Passed ? Theme.positive
                   : root.check.state === VirtualMicCheck.Running ? Theme.textSecondary : Theme.warning
            width: 18
            height: 18
        }
        Text {
            objectName: "virtualMicResult"
            Layout.fillWidth: true
            text: root.check.message
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.textSecondary
        }
    }
}
