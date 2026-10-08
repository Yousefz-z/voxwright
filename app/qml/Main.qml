import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

ApplicationWindow {
    id: window

    required property AppContext app
    /// Started at sign-in with --minimized: stay in the tray.
    property bool startHidden: false
    property bool trayNoticeShown: false
    property int page: 0
    // Page indexes.
    readonly property int voicesPage: 0
    readonly property int soundboardPage: 1
    readonly property int audioPage: 2
    readonly property int hotkeysPage: 3
    readonly property int designerPage: 4
    readonly property int settingsPage: 5

    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 640
    visible: !startHidden
    title: "Voxwright"
    color: Theme.background
    font.family: Theme.fontFamily

    Connections {
        target: window.app
        function onUiActionRequested(action) {
            if (action === "open-audio")
                window.page = window.audioPage
            else if (action === "open-hotkeys")
                window.page = window.hotkeysPage
            else if (action === "open-designer")
                window.page = window.designerPage
        }
    }

    function showFromTray() {
        window.show()
        window.raise()
        window.requestActivate()
    }

    // Closing keeps Voxwright running in the tray when the user wants that.
    // Otherwise it quits: with a tray, closing the last window does not.
    onClosing: close => {
        if (!window.app.system.closeToTray) {
            Qt.quit()
            return
        }
        close.accepted = false
        window.hide()
        if (!window.trayNoticeShown) {
            window.trayNoticeShown = true
            window.app.tray.notify(qsTr("Voxwright is still running"),
                                   qsTr("Your voice keeps changing. Open Voxwright again from its icon here."))
        }
    }

    Connections {
        target: window.app.tray
        function onShowWindowRequested() { window.showFromTray() }
        function onQuitRequested() { Qt.quit() }
    }

    FirstRunGuide {
        id: guide
        app: window.app
    }
    Connections {
        target: window.app.system
        function onChanged() {
            if (!window.app.system.firstRunDone && !guide.opened) {
                guide.step = 0
                guide.open()
            }
        }
    }
    Component.onCompleted: {
        if (!window.app.system.firstRunDone && !window.startHidden)
            guide.open()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar.
        Rectangle {
            Layout.preferredWidth: Theme.sidebarWidth
            Layout.fillHeight: true
            color: Theme.sidebar

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 6

                RowLayout {
                    Layout.bottomMargin: 18
                    Layout.topMargin: 6
                    spacing: 10
                    AppMark { width: 32; height: 32 }
                    Text {
                        text: "Voxwright"
                        font.family: Theme.fontFamily
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: Theme.textPrimary
                    }
                }
                NavButton {
                    objectName: "navVoices"
                    Layout.fillWidth: true
                    text: qsTr("Voices")
                    iconName: "voices"
                    current: window.page === window.voicesPage
                    onClicked: window.page = window.voicesPage
                }
                NavButton {
                    objectName: "navDesigner"
                    Layout.fillWidth: true
                    text: qsTr("Voice designer")
                    iconName: "designer"
                    current: window.page === window.designerPage
                    onClicked: window.page = window.designerPage
                }
                NavButton {
                    objectName: "navSoundboard"
                    Layout.fillWidth: true
                    text: qsTr("Soundboard")
                    iconName: "soundboard"
                    current: window.page === window.soundboardPage
                    onClicked: window.page = window.soundboardPage
                }
                NavButton {
                    objectName: "navAudio"
                    Layout.fillWidth: true
                    text: qsTr("Audio")
                    iconName: "audio"
                    current: window.page === window.audioPage
                    onClicked: window.page = window.audioPage
                }
                NavButton {
                    objectName: "navHotkeys"
                    Layout.fillWidth: true
                    text: qsTr("Hotkeys")
                    iconName: "keyboard"
                    current: window.page === window.hotkeysPage
                    onClicked: window.page = window.hotkeysPage
                }
                NavButton {
                    objectName: "navSettings"
                    Layout.fillWidth: true
                    text: qsTr("Settings")
                    iconName: "settings"
                    current: window.page === window.settingsPage
                    onClicked: window.page = window.settingsPage
                }
                Item { Layout.fillHeight: true }
                Text {
                    text: qsTr("Version %1").arg(window.app.version)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textMuted
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Notifications.
            ColumnLayout {
                objectName: "banners"
                Layout.fillWidth: true
                // An empty nested layout still takes space: it fills by
                // default and keeps the size of its last banner. Hidden, it
                // takes none.
                visible: window.app.notifications.count > 0
                Layout.fillHeight: false
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.topMargin: window.app.notifications.count > 0 ? 16 : 0
                spacing: 8
                Repeater {
                    model: window.app.notifications
                    Banner {
                        Layout.fillWidth: true
                        onActionTriggered: action => window.app.runAction(action)
                        onDismissed: key => window.app.notifications.dismiss(key)
                    }
                }
            }

            StackLayout {
                objectName: "pages"
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.page
                VoicesPage { app: window.app }
                SoundboardPage { app: window.app }
                AudioPage { app: window.app }
                HotkeysPage { app: window.app }
                DesignerPage { app: window.app }
                SettingsPage { app: window.app }
            }

            // Bottom bar.
            Rectangle {
                id: bottomBar
                objectName: "bottomBar"
                // Chips drop their labels when the full bar would not fit.
                // The stop button counts even when hidden, so the layout does
                // not jump when a sound starts.
                readonly property int gap: 10
                readonly property int infoWidth: 140
                readonly property int meterWidth: 100
                readonly property real fullWidth: 2 * 20 + 9 * gap + 40 + infoWidth + voiceChip.fullWidth
                                                  + hearChip.fullWidth + backgroundChip.fullWidth
                                                  + noiseChip.fullWidth + 32 + 32 + meterWidth
                readonly property bool compact: width < fullWidth
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.bottomBarHeight
                color: Theme.sidebar

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: bottomBar.gap

                    Rectangle {
                        readonly property color tint: window.app.voices.currentColor.length > 0 ? window.app.voices.currentColor : Theme.accent
                        width: 40
                        height: 40
                        radius: 20
                        color: Qt.rgba(tint.r, tint.g, tint.b, 0.22)
                        Icon {
                            anchors.centerIn: parent
                            width: 22
                            height: 22
                            name: window.app.voices.currentIcon
                            color: parent.tint
                        }
                    }
                    ColumnLayout {
                        Layout.preferredWidth: bottomBar.compact ? 120 : bottomBar.infoWidth
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            objectName: "bottomVoiceName"
                            text: window.app.designer.editing && window.app.designer.previewing
                                  ? qsTr("Designing %1").arg(window.app.designer.name)
                                  : window.app.voices.currentName
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                        }
                        Text {
                            objectName: "statusLine"
                            text: !window.app.audio.running ? qsTr("Audio stopped")
                                  : window.app.audio.sendingToVirtualMic ? qsTr("Latency %1 ms").arg(window.app.audio.latencyMs.toFixed(0))
                                  : qsTr("Not sent to other apps")
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            color: !window.app.audio.running ? Theme.danger
                                   : window.app.audio.sendingToVirtualMic ? Theme.textMuted : Theme.warning
                        }
                    }

                    ToggleChip {
                        id: voiceChip
                        objectName: "voiceChangerToggle"
                        compact: bottomBar.compact
                        text: qsTr("Voice changer")
                        iconName: "power"
                        checked: window.app.audio.voiceEnabled
                        onToggled: window.app.audio.voiceEnabled = checked
                    }
                    ToggleChip {
                        id: hearChip
                        objectName: "hearMyselfToggle"
                        compact: bottomBar.compact
                        text: qsTr("Hear myself")
                        iconName: "headphones"
                        checked: window.app.audio.hearMyself
                        onToggled: window.app.audio.hearMyself = checked
                    }
                    ToggleChip {
                        id: backgroundChip
                        compact: bottomBar.compact
                        text: qsTr("Background")
                        iconName: "waves"
                        checked: window.app.audio.backgroundEnabled
                        onToggled: window.app.audio.backgroundEnabled = checked
                    }
                    ToggleChip {
                        id: noiseChip
                        compact: bottomBar.compact
                        text: qsTr("Noise reduction")
                        iconName: "filter"
                        checked: window.app.audio.noiseReduction
                        onToggled: window.app.audio.noiseReduction = checked
                    }

                    Item { Layout.fillWidth: true }

                    AbstractButton {
                        id: stopSounds
                        objectName: "stopSoundsButton"
                        visible: window.app.soundboard.playingCount > 0
                        implicitWidth: 32
                        implicitHeight: 32
                        hoverEnabled: true
                        onClicked: window.app.soundboard.stopAll()
                        Accessible.name: qsTr("Stop all sounds")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Stop all sounds")
                        background: Rectangle {
                            radius: 16
                            color: stopSounds.hovered ? Theme.surfaceHover : Theme.accentSoft
                        }
                        contentItem: Item {
                            Icon { anchors.centerIn: parent; width: 16; height: 16; name: "stop"; color: Theme.accent }
                        }
                    }
                    AbstractButton {
                        id: muteButton
                        objectName: "muteButton"
                        implicitWidth: 32
                        implicitHeight: 32
                        checkable: true
                        checked: window.app.audio.muted
                        onToggled: window.app.audio.muted = checked
                        Accessible.name: checked ? qsTr("Unmute microphone") : qsTr("Mute microphone")
                        background: Rectangle {
                            radius: 16
                            color: muteButton.checked ? Qt.rgba(0.95, 0.33, 0.36, 0.2)
                                                      : (muteButton.hovered ? Theme.surfaceHover : "transparent")
                        }
                        contentItem: Item {
                            Icon {
                                anchors.centerIn: parent
                                width: 18
                                height: 18
                                name: muteButton.checked ? "mic-off" : "mic"
                                color: muteButton.checked ? Theme.danger : Theme.textSecondary
                            }
                        }
                    }
                    LevelMeter {
                        objectName: "inputMeter"
                        Layout.preferredWidth: bottomBar.compact ? 80 : bottomBar.meterWidth
                        level: window.app.audio.inputLevel
                    }
                }
            }
        }
    }
}
