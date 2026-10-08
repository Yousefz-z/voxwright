import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Voxwright

// Build a voice from effect blocks and hear it while editing. Without a
// draft open, lists the user's voices to edit, export, or delete.
Item {
    id: page

    required property AppContext app
    readonly property DesignerController designer: app.designer
    property string error

    function run(problem) {
        page.error = problem
    }

    // ---------------------------------------------------------- landing
    ColumnLayout {
        objectName: "designerLanding"
        visible: !page.designer.editing
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        ColumnLayout {
            spacing: 2
            Text {
                text: qsTr("Voice designer")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
                color: Theme.textPrimary
            }
            Text {
                text: qsTr("Stack effects into a voice of your own. You hear every change while you make it.")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: Theme.textMuted
            }
        }

        RowLayout {
            spacing: 10
            PageButton {
                objectName: "designerNewVoice"
                text: qsTr("New voice")
                iconName: "plus"
                onClicked: page.designer.newVoice()
            }
            PageButton {
                objectName: "designerCustomize"
                text: qsTr("Start from %1").arg(page.app.voices.currentName)
                iconName: "edit"
                visible: page.app.voices.currentName.length > 0
                onClicked: page.designer.editVoice(page.app.voices.currentVoiceId)
            }
            PageButton {
                objectName: "designerImport"
                text: qsTr("Import voices")
                iconName: "import"
                onClicked: importDialog.open()
            }
        }

        Card {
            Layout.fillWidth: true
            title: qsTr("My voices")
            subtitle: page.designer.customVoices.length === 0
                      ? qsTr("Voices you save or import appear here and on the Voices page.")
                      : page.designer.customVoices.length === 1 ? qsTr("1 voice")
                      : qsTr("%1 voices").arg(page.designer.customVoices.length)

            Repeater {
                model: page.designer.customVoices
                RowLayout {
                    id: mine
                    required property var modelData
                    objectName: "myVoice_" + modelData.voiceId
                    Layout.fillWidth: true
                    spacing: 12
                    Rectangle {
                        readonly property color tint: mine.modelData.accentColor
                        width: 36
                        height: 36
                        radius: 18
                        color: Qt.rgba(tint.r, tint.g, tint.b, 0.22)
                        Icon {
                            anchors.centerIn: parent
                            width: 20
                            height: 20
                            name: mine.modelData.iconName
                            color: parent.tint
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: mine.modelData.name
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                        }
                        Text {
                            text: mine.modelData.effects === 1
                                  ? qsTr("%1, 1 effect").arg(mine.modelData.category)
                                  : qsTr("%1, %2 effects").arg(mine.modelData.category).arg(mine.modelData.effects)
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textMuted
                        }
                    }
                    IconButton {
                        objectName: "editVoice"
                        text: qsTr("Edit %1").arg(mine.modelData.name)
                        iconName: "edit"
                        onClicked: page.designer.editVoice(mine.modelData.voiceId)
                    }
                    IconButton {
                        text: qsTr("Export %1").arg(mine.modelData.name)
                        iconName: "export"
                        onClicked: {
                            exportDialog.voiceId = mine.modelData.voiceId
                            exportDialog.selectedFile = mine.modelData.name + ".voxvoice"
                            exportDialog.open()
                        }
                    }
                    IconButton {
                        objectName: "deleteVoice"
                        text: qsTr("Delete %1").arg(mine.modelData.name)
                        iconName: "trash"
                        onClicked: confirmDelete.openFor(mine.modelData.voiceId, mine.modelData.name)
                    }
                }
            }
        }

        Text {
            visible: page.error.length > 0 && !page.designer.editing
            Layout.fillWidth: true
            text: page.error
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.danger
        }

        Item { Layout.fillHeight: true }
    }

    // ----------------------------------------------------------- editor
    RowLayout {
        objectName: "designerEditor"
        visible: page.designer.editing
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // Effect palette.
        ColumnLayout {
            Layout.preferredWidth: 190
            Layout.fillHeight: true
            spacing: 8
            Text {
                text: qsTr("Add an effect")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: 2
                    Repeater {
                        model: page.designer.palette
                        ColumnLayout {
                            id: entry
                            required property var modelData
                            required property int index
                            readonly property bool firstOfCategory: index === 0
                                || page.designer.palette[index - 1].category !== modelData.category
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                visible: entry.firstOfCategory
                                Layout.topMargin: entry.index === 0 ? 0 : 8
                                text: entry.modelData.category
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                font.weight: Font.DemiBold
                                color: Theme.textMuted
                            }
                            AbstractButton {
                                id: addButton
                                objectName: "addEffect_" + entry.modelData.id
                                Layout.fillWidth: true
                                implicitHeight: 32
                                hoverEnabled: true
                                enabled: page.designer.canAddBlock
                                opacity: enabled ? 1.0 : 0.4
                                Accessible.name: qsTr("Add %1").arg(entry.modelData.name)
                                ToolTip.visible: hovered
                                ToolTip.delay: 500
                                ToolTip.text: entry.modelData.description
                                onClicked: page.run(page.designer.addBlock(entry.modelData.id))
                                background: Rectangle {
                                    radius: Theme.radiusSmall
                                    color: addButton.hovered ? Theme.surfaceHover : "transparent"
                                }
                                contentItem: RowLayout {
                                    spacing: 8
                                    Icon { name: "plus"; width: 14; height: 14; color: Theme.accent }
                                    Text {
                                        Layout.fillWidth: true
                                        text: entry.modelData.name
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontBody
                                        color: Theme.textPrimary
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Header and chain.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                TextField {
                    id: nameField
                    objectName: "voiceNameField"
                    Layout.fillWidth: true
                    text: page.designer.name
                    placeholderText: qsTr("Voice name")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold
                    Accessible.name: qsTr("Voice name")
                    onTextEdited: page.designer.name = text
                    background: Rectangle {
                        implicitHeight: 38
                        radius: Theme.radiusSmall
                        color: Theme.surfaceRaised
                        border.width: 1
                        border.color: nameField.activeFocus ? Theme.accent : Theme.border
                    }
                }
                PageButton {
                    objectName: "designerCancel"
                    text: page.designer.isNew ? qsTr("Discard") : qsTr("Cancel")
                    onClicked: {
                        page.error = ""
                        page.designer.discard()
                    }
                }
                Button {
                    id: saveButton
                    objectName: "designerSave"
                    text: qsTr("Save voice")
                    enabled: page.designer.dirty
                    onClicked: page.run(page.designer.save())
                    contentItem: Text {
                        text: saveButton.text
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
                        color: saveButton.pressed ? Theme.accentPressed : Theme.accent
                        opacity: saveButton.enabled ? 1.0 : 0.45
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text {
                    objectName: "designerLatency"
                    text: qsTr("Effect delay %1 ms").arg(page.designer.latencyMs.toFixed(1))
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: page.designer.latencyMs > 40 ? Theme.warning : Theme.textMuted
                }
                Text {
                    objectName: "designerLoad"
                    visible: page.app.audio.running
                    text: qsTr("Processing load %1 %").arg(Math.round(page.app.audio.processingLoad * 100))
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: page.app.audio.processingLoad > 0.5 ? Theme.warning : Theme.textMuted
                }
                Text {
                    text: page.designer.previewing ? qsTr("Playing live") : qsTr("Not playing: another voice was chosen")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: page.designer.previewing ? Theme.positive : Theme.warning
                }
                PageButton {
                    objectName: "designerListen"
                    visible: !page.designer.previewing
                    text: qsTr("Listen again")
                    iconName: "voices"
                    onClicked: page.designer.preview()
                }
                Item { Layout.fillWidth: true }
                Text {
                    visible: page.error.length > 0
                    Layout.maximumWidth: 360
                    text: page.error
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: Theme.danger
                }
            }

            ScrollView {
                id: chainScroll
                objectName: "chainScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: chainScroll.availableWidth
                    spacing: 10
                    Repeater {
                        model: page.designer.blocks
                        EffectBlockCard {
                            required property var modelData
                            designer: page.designer
                            block: modelData
                            count: page.designer.blocks.length
                        }
                    }
                    Text {
                        visible: page.designer.blocks.length === 0
                        Layout.fillWidth: true
                        Layout.topMargin: 20
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Add an effect from the list on the left.")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        color: Theme.textMuted
                    }
                }
            }
        }

        // Details and quick sliders.
        ScrollView {
            id: sideScroll
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: sideScroll.availableWidth
                spacing: 12

                Text {
                    text: qsTr("Look")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                }
                ChoiceBox {
                    objectName: "voiceCategory"
                    Layout.fillWidth: true
                    model: page.designer.categories
                    currentIndex: page.designer.categories.indexOf(page.designer.category)
                    Accessible.name: qsTr("Category")
                    onActivated: index => page.designer.category = page.designer.categories[index]
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: page.designer.icons
                        AbstractButton {
                            id: iconChoice
                            required property string modelData
                            width: 32
                            height: 32
                            checkable: true
                            checked: page.designer.icon === modelData
                            Accessible.name: qsTr("Icon %1").arg(modelData)
                            onClicked: page.designer.icon = modelData
                            background: Rectangle {
                                radius: 16
                                color: iconChoice.checked ? Theme.accentSoft : Theme.surfaceRaised
                                border.width: iconChoice.checked ? 1 : 0
                                border.color: Theme.accent
                            }
                            contentItem: Item {
                                Icon {
                                    anchors.centerIn: parent
                                    width: 18
                                    height: 18
                                    name: iconChoice.modelData
                                    color: iconChoice.checked ? Theme.accent : Theme.textSecondary
                                }
                            }
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: page.designer.colors
                        AbstractButton {
                            id: swatch
                            required property string modelData
                            width: 24
                            height: 24
                            Accessible.name: qsTr("Color %1").arg(modelData)
                            onClicked: page.designer.color = modelData
                            background: Rectangle {
                                radius: 12
                                color: swatch.modelData
                                border.width: page.designer.color.toUpperCase() === swatch.modelData ? 2 : 0
                                border.color: Theme.textPrimary
                            }
                        }
                    }
                }
                TextArea {
                    id: descriptionField
                    objectName: "voiceDescription"
                    Layout.fillWidth: true
                    text: page.designer.description
                    placeholderText: qsTr("Description (optional)")
                    wrapMode: TextEdit.Wrap
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    Accessible.name: qsTr("Description")
                    onTextChanged: if (activeFocus) page.designer.description = text
                    background: Rectangle {
                        implicitHeight: 64
                        radius: Theme.radiusSmall
                        color: Theme.surfaceRaised
                        border.width: 1
                        border.color: descriptionField.activeFocus ? Theme.accent : Theme.border
                    }
                }

                Text {
                    Layout.topMargin: 8
                    text: qsTr("Quick sliders")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Shown on the Voices page so you can adjust the voice quickly. Add one with the slider button next to a setting (up to 4).")
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
                Repeater {
                    model: page.designer.macros
                    ColumnLayout {
                        id: macro
                        required property var modelData
                        objectName: "macro" + modelData.index
                        Layout.fillWidth: true
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: macroName
                                Layout.fillWidth: true
                                text: macro.modelData.name
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                Accessible.name: qsTr("Quick slider name")
                                onEditingFinished: page.designer.renameMacro(macro.modelData.index, text)
                                background: Rectangle {
                                    implicitHeight: 30
                                    radius: Theme.radiusSmall
                                    color: macroName.activeFocus ? Theme.surfaceRaised : "transparent"
                                    border.width: macroName.activeFocus ? 1 : 0
                                    border.color: Theme.accent
                                }
                            }
                            IconButton {
                                objectName: "removeMacro"
                                text: qsTr("Remove the quick slider %1").arg(macro.modelData.name)
                                iconName: "close"
                                onClicked: page.designer.removeMacro(macro.modelData.index)
                            }
                        }
                        LabeledSlider {
                            label: macro.modelData.targets
                            from: 0
                            to: 1
                            value: macro.modelData.position
                            formatValue: v => Math.round(v * 100) + " %"
                            onMoved: v => page.designer.setMacroPosition(macro.modelData.index, v)
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import voices")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Voxwright voices (*.voxvoice *.json)")]
        onAccepted: page.designer.importVoices(selectedFiles)
    }

    FileDialog {
        id: exportDialog
        property string voiceId
        title: qsTr("Export voice")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "voxvoice"
        nameFilters: [qsTr("Voxwright voices (*.voxvoice)")]
        onAccepted: page.run(page.designer.exportVoice(voiceId, selectedFile))
    }

    Popup {
        id: confirmDelete
        objectName: "confirmDelete"
        property string voiceId
        property string voiceName
        function openFor(id, name) {
            voiceId = id
            voiceName = name
            open()
        }
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 380
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
            Text {
                Layout.fillWidth: true
                text: qsTr("Delete \"%1\"?").arg(confirmDelete.voiceName)
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("The voice and its hotkey are removed. Export it first if you may want it back.")
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: Theme.textSecondary
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 10
                PageButton {
                    text: qsTr("Keep it")
                    onClicked: confirmDelete.close()
                }
                PageButton {
                    objectName: "confirmDeleteButton"
                    text: qsTr("Delete")
                    iconName: "trash"
                    onClicked: {
                        page.run(page.designer.deleteVoice(confirmDelete.voiceId))
                        confirmDelete.close()
                    }
                }
            }
        }
    }
}
