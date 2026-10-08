import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Voxwright

// Boards of sounds: play them, add your own, and change their settings.
Item {
    id: page

    required property AppContext app
    readonly property SoundboardController board: app.soundboard

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ColumnLayout {
                spacing: 2
                Text {
                    text: qsTr("Soundboard")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.Bold
                    color: Theme.textPrimary
                }
                Text {
                    text: page.board.loadingCount > 0 ? qsTr("Loading %1 sounds").arg(page.board.loadingCount)
                                                     : qsTr("%1 sounds on this board").arg(page.board.sounds.count)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
            }
            Item { Layout.fillWidth: true }
            PageButton {
                objectName: "addSounds"
                text: qsTr("Add sounds")
                iconName: "import"
                onClicked: fileDialog.open()
            }
            PageButton {
                objectName: "stopAllSounds"
                text: qsTr("Stop all")
                iconName: "stop"
                enabled: page.board.playingCount > 0
                onClicked: page.board.stopAll()
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: page.board.boards
                CategoryChip {
                    required property string modelData
                    required property int index
                    text: modelData
                    selected: index === page.board.currentBoard
                    onClicked: page.board.currentBoard = index
                    onDoubleClicked: renamePopup.openFor(index, modelData)
                }
            }
            CategoryChip {
                objectName: "addBoard"
                text: "+"
                Accessible.name: qsTr("Add a board")
                onClicked: page.board.addBoard(qsTr("Board %1").arg(page.board.boards.length + 1))
            }
        }

        GridView {
            id: grid
            objectName: "soundGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.max(160, width / Math.max(1, Math.floor(width / 168)))
            cellHeight: 118
            model: page.board.sounds
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: cell
                required property int slot
                required property string name
                required property string accentColor
                required property string iconName
                required property string hotkey
                required property bool playing
                required property int loadState
                required property string mode
                required property bool loop
                width: grid.cellWidth
                height: grid.cellHeight
                SoundTile {
                    anchors.fill: parent
                    anchors.margins: 6
                    slot: cell.slot
                    name: cell.name
                    accentColor: cell.accentColor
                    iconName: cell.iconName
                    hotkey: cell.hotkey
                    playing: cell.playing
                    loadState: cell.loadState
                    mode: cell.mode
                    loop: cell.loop
                    onPressed: s => page.board.press(s)
                    onReleased: s => page.board.release(s)
                    onEditRequested: s => editor.openFor(s)
                }
            }

            Text {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: qsTr("Drop audio files here, or use Add sounds.")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: Theme.textMuted
            }

            DropArea {
                id: drop
                anchors.fill: parent
                keys: ["text/uri-list"]
                onDropped: event => {
                    page.board.importFiles(event.urls)
                    event.acceptProposedAction()
                }
                Rectangle {
                    anchors.fill: parent
                    visible: drop.containsDrag
                    radius: Theme.radius
                    color: Qt.rgba(1, 0.48, 0.35, 0.08)
                    border.width: 2
                    border.color: Theme.accent
                }
            }
        }

        SpeechPanel {
            Layout.fillWidth: true
            speech: page.app.speech
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Add sounds")
        fileMode: FileDialog.OpenFiles
        nameFilters: page.board.fileFilters
        onAccepted: page.board.importFiles(selectedFiles)
    }

    SoundEditor {
        id: editor
        soundboard: page.board
        hotkeys: page.app.hotkeys
    }

    Popup {
        id: renamePopup
        property int index: -1
        function openFor(i, name) {
            index = i
            renameField.text = name
            open()
            renameField.forceActiveFocus()
        }
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 320
        padding: 16
        background: Rectangle { radius: Theme.radius; color: Theme.surface; border.width: 1; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: qsTr("Board name")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            TextField {
                id: renameField
                Layout.fillWidth: true
                color: Theme.textPrimary
                onAccepted: {
                    page.board.renameBoard(renamePopup.index, text)
                    renamePopup.close()
                }
                background: Rectangle { implicitHeight: 34; radius: Theme.radiusSmall; color: Theme.surfaceRaised; border.width: 1; border.color: Theme.border }
            }
            RowLayout {
                PageButton {
                    text: qsTr("Delete board")
                    iconName: "trash"
                    enabled: page.board.boards.length > 1
                    onClicked: {
                        page.board.removeBoard(renamePopup.index)
                        renamePopup.close()
                    }
                }
                Item { Layout.fillWidth: true }
                PageButton {
                    text: qsTr("Save")
                    iconName: "check"
                    onClicked: {
                        page.board.renameBoard(renamePopup.index, renameField.text)
                        renamePopup.close()
                    }
                }
            }
        }
    }
}
