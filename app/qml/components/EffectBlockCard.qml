import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// One effect in the designer's chain: its settings, on or off, its place in
// the order, and removal. Settings come from the effect's description, so a
// new effect needs no UI code.
Rectangle {
    id: root

    required property DesignerController designer
    required property var block
    required property int count
    readonly property int blockIndex: block.index
    // Follows clicks right away; the chain is not rebuilt for an on/off change.
    property bool active: !block.bypassed

    objectName: "block" + blockIndex
    Layout.fillWidth: true
    implicitHeight: column.implicitHeight + 28
    radius: Theme.radius
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

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
            Rectangle {
                width: 26
                height: 26
                radius: 13
                color: root.active ? Theme.accentSoft : Theme.surfaceRaised
                Text {
                    anchors.centerIn: parent
                    text: root.blockIndex + 1
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                    color: root.active ? Theme.accent : Theme.textMuted
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: root.block.name
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold
                    color: root.active ? Theme.textPrimary : Theme.textMuted
                }
                Text {
                    Layout.fillWidth: true
                    text: root.block.description
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
            }
            IconButton {
                objectName: "bypass"
                text: root.active ? qsTr("Turn %1 off").arg(root.block.name) : qsTr("Turn %1 on").arg(root.block.name)
                iconName: "power"
                iconColor: root.active ? Theme.accent : Theme.textMuted
                onClicked: {
                    root.active = !root.active
                    root.designer.setBypassed(root.blockIndex, !root.active)
                }
            }
            IconButton {
                objectName: "moveUp"
                text: qsTr("Move up")
                iconName: "up"
                enabled: root.blockIndex > 0
                onClicked: root.designer.moveBlock(root.blockIndex, root.blockIndex - 1)
            }
            IconButton {
                objectName: "moveDown"
                text: qsTr("Move down")
                iconName: "down"
                enabled: root.blockIndex < root.count - 1
                onClicked: root.designer.moveBlock(root.blockIndex, root.blockIndex + 1)
            }
            IconButton {
                objectName: "removeBlock"
                text: qsTr("Remove %1").arg(root.block.name)
                iconName: "trash"
                onClicked: root.designer.removeBlock(root.blockIndex)
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 20
            rowSpacing: 8
            opacity: root.active ? 1.0 : 0.5

            Repeater {
                model: root.block.params
                ColumnLayout {
                    id: param
                    required property var modelData
                    readonly property bool underMacro: modelData.macro.length > 0
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 2

                    RowLayout {
                        visible: param.modelData.kind === "continuous"
                        Layout.fillWidth: true
                        spacing: 4
                        LabeledSlider {
                            objectName: "param_" + param.modelData.id
                            label: param.modelData.name
                            from: 0
                            to: 1
                            value: param.modelData.position
                            enabled: !param.underMacro
                            formatValue: p => root.designer.formatValue(root.blockIndex, param.modelData.index,
                                                                        root.designer.valueAt(root.blockIndex, param.modelData.index, p))
                            onMoved: p => root.designer.setParameter(root.blockIndex, param.modelData.index,
                                                                     root.designer.valueAt(root.blockIndex, param.modelData.index, p))
                        }
                        IconButton {
                            objectName: "expose_" + param.modelData.id
                            Layout.alignment: Qt.AlignBottom
                            visible: !param.underMacro
                            enabled: root.designer.canAddMacro
                            text: qsTr("Add a quick slider for %1").arg(param.modelData.name)
                            iconName: "sliders"
                            onClicked: root.designer.exposeParameter(root.blockIndex, param.modelData.index)
                        }
                    }
                    Text {
                        visible: param.underMacro
                        Layout.fillWidth: true
                        text: qsTr("Set by the quick slider \"%1\"").arg(param.modelData.macro)
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        color: Theme.info
                    }

                    SwitchRow {
                        visible: param.modelData.kind === "toggle"
                        label: param.modelData.name
                        checked: param.modelData.value >= 0.5
                        onToggled: on => root.designer.setParameter(root.blockIndex, param.modelData.index, on ? 1 : 0)
                    }

                    RowLayout {
                        visible: param.modelData.kind === "choice"
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: param.modelData.name
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            color: Theme.textSecondary
                        }
                        ChoiceBox {
                            Layout.preferredWidth: 140
                            model: param.modelData.choices
                            currentIndex: Math.round(param.modelData.value)
                            Accessible.name: param.modelData.name
                            onActivated: index => root.designer.setParameter(root.blockIndex, param.modelData.index, index)
                        }
                    }
                }
            }
        }
    }
}
