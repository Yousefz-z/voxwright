import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// Browse voices and switch live; adjust the active voice on the right.
Item {
    id: page

    required property AppContext app
    readonly property VoiceController voices: app.voices

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                ColumnLayout {
                    spacing: 2
                    Text {
                        text: qsTr("Voices")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTitle
                        font.weight: Font.Bold
                        color: Theme.textPrimary
                    }
                    Text {
                        text: qsTr("%1 of %2 voices").arg(page.voices.voices.count).arg(page.voices.voiceCount)
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                }
                Item { Layout.fillWidth: true }
                SearchField {
                    objectName: "voiceSearch"
                    Layout.preferredWidth: 260
                    placeholderText: qsTr("Search voices")
                    onTextChanged: page.voices.voices.searchText = text
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 8
                CategoryChip {
                    text: qsTr("All")
                    selected: page.voices.voices.category === ""
                    onClicked: page.voices.voices.category = ""
                }
                CategoryChip {
                    text: qsTr("Favorites")
                    selected: page.voices.voices.category === "favorites"
                    onClicked: page.voices.voices.category = "favorites"
                }
                Repeater {
                    model: page.voices.categories
                    CategoryChip {
                        required property string modelData
                        text: modelData
                        selected: page.voices.voices.category === modelData
                        onClicked: page.voices.voices.category = modelData
                    }
                }
            }

            GridView {
                id: grid
                objectName: "voiceGrid"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: Math.max(168, width / Math.max(1, Math.floor(width / 176)))
                cellHeight: 134
                model: page.voices.voices
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                // Keep the active voice in view when it changes from anywhere.
                function showCurrent() {
                    const row = page.voices.voices.rowOf(page.voices.currentVoiceId)
                    if (row >= 0)
                        positionViewAtIndex(row, GridView.Contain)
                }
                Component.onCompleted: Qt.callLater(showCurrent)
                Connections {
                    target: page.voices
                    function onCurrentVoiceChanged() { Qt.callLater(grid.showCurrent) }
                }
                Connections {
                    target: page.voices.voices
                    function onFilterChanged() {
                        if (page.voices.voices.searchText.length === 0 && page.voices.voices.category === "")
                            Qt.callLater(grid.showCurrent)
                    }
                }

                delegate: Item {
                    id: cell
                    required property string voiceId
                    required property string name
                    required property string category
                    required property string iconName
                    required property string accentColor
                    required property bool favorite
                    required property bool active
                    width: grid.cellWidth
                    height: grid.cellHeight

                    VoiceTile {
                        anchors.fill: parent
                        anchors.margins: 6
                        voiceId: cell.voiceId
                        name: cell.name
                        category: cell.category
                        iconName: cell.iconName
                        accentColor: cell.accentColor
                        favorite: cell.favorite
                        active: cell.active
                        onClicked: page.voices.selectVoice(cell.voiceId)
                        onFavoriteToggled: id => page.voices.toggleFavorite(id)
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: grid.count === 0
                    text: page.voices.voices.category === "favorites"
                          ? qsTr("No favorites yet. Star a voice to keep it here.")
                          : qsTr("No voice matches \"%1\".").arg(page.voices.voices.searchText)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    color: Theme.textMuted
                }
            }
        }

        Card {
            objectName: "voiceDetail"
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignTop

            RowLayout {
                spacing: 14
                Rectangle {
                    readonly property color tint: page.voices.currentColor.length > 0 ? page.voices.currentColor : Theme.accent
                    width: 56
                    height: 56
                    radius: 28
                    color: Qt.rgba(tint.r, tint.g, tint.b, 0.22)
                    Icon {
                        anchors.centerIn: parent
                        width: 30
                        height: 30
                        name: page.voices.currentIcon
                        color: parent.tint
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        objectName: "currentVoiceName"
                        Layout.fillWidth: true
                        text: page.voices.currentName
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: Theme.textPrimary
                    }
                    Text {
                        text: page.voices.currentCategory
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        color: Theme.textMuted
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: page.voices.currentDescription
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: Theme.textSecondary
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

            Repeater {
                model: page.voices.macros
                LabeledSlider {
                    required property var modelData
                    required property int index
                    label: modelData.name
                    from: 0
                    to: 1
                    value: modelData.value
                    formatValue: v => Math.round(v * 100) + " %"
                    onMoved: v => page.voices.setMacro(index, v)
                }
            }
            LabeledSlider {
                label: qsTr("Bass")
                from: -12
                to: 12
                stepSize: 0.5
                value: page.voices.bassDb
                unit: "dB"
                decimals: 1
                onMoved: v => page.voices.setTone(v, page.voices.trebleDb)
            }
            LabeledSlider {
                label: qsTr("Treble")
                from: -12
                to: 12
                stepSize: 0.5
                value: page.voices.trebleDb
                unit: "dB"
                decimals: 1
                onMoved: v => page.voices.setTone(page.voices.bassDb, v)
            }

            Item { Layout.fillHeight: true }

            Button {
                id: resetButton
                Layout.fillWidth: true
                text: qsTr("Reset voice settings")
                onClicked: page.voices.resetCurrentVoice()
                contentItem: Text {
                    text: resetButton.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    color: Theme.textSecondary
                }
                background: Rectangle {
                    implicitHeight: 36
                    radius: Theme.radiusSmall
                    color: resetButton.hovered ? Theme.surfaceHover : Theme.surfaceRaised
                    border.width: 1
                    border.color: Theme.border
                }
            }
        }
    }
}
