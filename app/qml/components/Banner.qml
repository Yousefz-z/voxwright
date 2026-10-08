import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// One notification: what happened and what to do about it.
Rectangle {
    id: root

    required property string key
    required property int level
    required property string title
    required property string message
    required property string actionLabel
    required property string action
    signal actionTriggered(string action)
    signal dismissed(string key)

    readonly property color tone: Theme.levelColor(level)

    implicitHeight: content.implicitHeight + 20
    radius: Theme.radiusSmall
    color: Qt.rgba(tone.r, tone.g, tone.b, 0.12)
    border.width: 1
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.5)

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 10
        spacing: 12

        Icon {
            Layout.alignment: Qt.AlignTop
            name: root.level === 2 ? "error" : root.level === 1 ? "warning" : "info"
            color: root.tone
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: root.title
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            Text {
                Layout.fillWidth: true
                text: root.message
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                color: Theme.textSecondary
            }
        }
        Button {
            id: actionButton
            visible: root.actionLabel.length > 0
            text: root.actionLabel
            onClicked: root.actionTriggered(root.action)
            contentItem: Text {
                text: actionButton.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                color: Theme.textPrimary
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                implicitWidth: 96
                implicitHeight: 30
                radius: Theme.radiusSmall
                color: actionButton.hovered ? Theme.surfaceHover : Theme.surfaceRaised
                border.width: 1
                border.color: Theme.border
            }
        }
        AbstractButton {
            id: closeButton
            implicitWidth: 24
            implicitHeight: 24
            Accessible.name: qsTr("Dismiss")
            onClicked: root.dismissed(root.key)
            contentItem: Icon { name: "close"; width: 16; height: 16; color: Theme.textMuted }
        }
    }
}
