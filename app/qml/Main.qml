import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

ApplicationWindow {
    id: window

    required property AppContext app
    property int page: 0

    width: 1240
    height: 800
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    title: "Voxwright"
    color: Theme.background
    font.family: Theme.fontFamily

    Connections {
        target: window.app
        function onUiActionRequested(action) {
            if (action === "open-audio")
                window.page = 1
        }
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
                    current: window.page === 0
                    onClicked: window.page = 0
                }
                NavButton {
                    objectName: "navAudio"
                    Layout.fillWidth: true
                    text: qsTr("Audio")
                    iconName: "audio"
                    current: window.page === 1
                    onClicked: window.page = 1
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
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: window.page
                VoicesPage { app: window.app }
                AudioPage { app: window.app }
            }

            // Bottom bar.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.bottomBarHeight
                color: Theme.sidebar

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 12

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
                        Layout.preferredWidth: 170
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: window.app.voices.currentName
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
                        objectName: "voiceChangerToggle"
                        text: qsTr("Voice changer")
                        iconName: "power"
                        checked: window.app.audio.voiceEnabled
                        onToggled: window.app.audio.voiceEnabled = checked
                    }
                    ToggleChip {
                        objectName: "hearMyselfToggle"
                        text: qsTr("Hear myself")
                        iconName: "headphones"
                        checked: window.app.audio.hearMyself
                        onToggled: window.app.audio.hearMyself = checked
                    }
                    ToggleChip {
                        text: qsTr("Background")
                        iconName: "waves"
                        checked: window.app.audio.backgroundEnabled
                        onToggled: window.app.audio.backgroundEnabled = checked
                    }
                    ToggleChip {
                        text: qsTr("Noise reduction")
                        iconName: "filter"
                        checked: window.app.audio.noiseReduction
                        onToggled: window.app.audio.noiseReduction = checked
                    }

                    Item { Layout.fillWidth: true }

                    Icon { name: "mic"; width: 18; height: 18; color: Theme.textSecondary }
                    LevelMeter {
                        objectName: "inputMeter"
                        Layout.preferredWidth: 140
                        level: window.app.audio.inputLevel
                    }
                }
            }
        }
    }
}
