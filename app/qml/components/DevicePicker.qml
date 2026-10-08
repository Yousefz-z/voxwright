import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// Chooses one device from a DeviceListModel.
ColumnLayout {
    id: root

    property string label
    property string iconName
    property var model
    property string currentId
    signal picked(string deviceId)

    Layout.fillWidth: true
    spacing: 6

    RowLayout {
        spacing: 8
        Icon { name: root.iconName; color: Theme.textSecondary; width: 18; height: 18 }
        Text {
            text: root.label
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.textSecondary
        }
    }

    ComboBox {
        id: combo
        Layout.fillWidth: true
        model: root.model
        textRole: "name"
        valueRole: "deviceId"
        currentIndex: root.model ? root.model.indexOf(root.currentId) : -1
        Accessible.name: root.label
        onActivated: index => root.picked(root.model.idAt(index))

        Connections {
            target: root.model
            function onCountChanged() { combo.currentIndex = root.model.indexOf(root.currentId) }
        }

        background: Rectangle {
            implicitHeight: 38
            radius: Theme.radiusSmall
            color: combo.hovered ? Theme.surfaceHover : Theme.surfaceRaised
            border.width: 1
            border.color: combo.visualFocus ? Theme.accent : Theme.border
        }
        contentItem: Text {
            leftPadding: 12
            rightPadding: combo.indicator.width + 8
            text: combo.displayText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.textPrimary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: combo.width - width - 12
            y: (combo.height - height) / 2
            text: "▾"
            color: Theme.textSecondary
            font.pixelSize: 12
        }
        delegate: ItemDelegate {
            id: option
            required property int index
            required property string name
            required property bool isVirtualCable
            width: combo.width
            highlighted: combo.highlightedIndex === index
            contentItem: RowLayout {
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: option.name
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
                Text {
                    visible: option.isVirtualCable
                    text: qsTr("Virtual cable")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: Theme.accent
                }
            }
            background: Rectangle {
                color: option.highlighted ? Theme.surfaceHover : Theme.surfaceRaised
            }
        }
        popup.background: Rectangle {
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.border
            radius: Theme.radiusSmall
        }
    }
}
