import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// Startup, tray, the virtual microphone check, and the setup guide.
Item {
    id: page

    required property AppContext app
    readonly property SystemController system: app.system
    property string error

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 16

            Text {
                Layout.leftMargin: 20
                Layout.topMargin: 20
                text: qsTr("Settings")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
                color: Theme.textPrimary
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("Startup and tray")

                SwitchRow {
                    objectName: "startAtLogin"
                    label: page.system.platform === "macos" ? qsTr("Open Voxwright when you log in")
                                                            : qsTr("Start Voxwright when you sign in")
                    checked: page.system.startAtLogin
                    onToggled: on => page.error = page.system.setStartAtLogin(on)
                }
                SwitchRow {
                    objectName: "startMinimized"
                    label: qsTr("Start hidden in the tray")
                    detail: qsTr("When it starts at sign-in, Voxwright runs in the background until you open it.")
                    enabled: page.system.trayAvailable
                    checked: page.system.startMinimized
                    onToggled: on => page.system.startMinimized = on
                }
                SwitchRow {
                    objectName: "closeToTray"
                    label: qsTr("Keep running when the window is closed")
                    detail: page.system.trayAvailable
                            ? qsTr("Your voice keeps changing; open Voxwright again from its tray icon.")
                            : qsTr("This desktop has no tray, so closing the window quits Voxwright.")
                    enabled: page.system.trayAvailable
                    checked: page.system.closeToTray
                    onToggled: on => page.system.closeToTray = on
                }
                Text {
                    visible: page.error.length > 0
                    Layout.fillWidth: true
                    text: page.error
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    color: Theme.danger
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("Virtual microphone")
                subtitle: page.app.audio.virtualCableFound
                          ? qsTr("Plays a short test sound into the virtual cable and listens for it where chat apps record.")
                          : qsTr("No virtual cable is installed yet. %1 is free.").arg(page.app.audio.virtualCableProduct)

                VirtualMicTest {
                    Layout.fillWidth: true
                    app: page.app
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("Using Voxwright in other apps")
                subtitle: qsTr("Choose \"%1\" as the microphone in the app. The menus are usually here:").arg(page.app.audio.chatAppMicrophoneName.length > 0 ? page.app.audio.chatAppMicrophoneName : qsTr("the virtual cable's output"))

                Repeater {
                    model: [
                        { app: "Discord", path: qsTr("User Settings, Voice & Video, Input Device") },
                        { app: "Zoom", path: qsTr("Settings, Audio, Microphone") },
                        { app: "Microsoft Teams", path: qsTr("Settings, Devices, Microphone") },
                        { app: "OBS Studio", path: qsTr("Settings, Audio, Mic/Auxiliary Audio (or add an Audio Input Capture source)") },
                        { app: qsTr("Games"), path: qsTr("The game's voice chat or audio settings; if it has none, it uses the system's default microphone") },
                        { app: page.system.platform === "macos" ? "macOS" : "Windows",
                          path: page.system.platform === "macos" ? qsTr("System Settings, Sound, Input sets the default microphone")
                                                                 : qsTr("Settings, System, Sound, Input sets the default microphone") }
                    ]
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 12
                        Text {
                            Layout.preferredWidth: 130
                            text: modelData.app
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                            color: Theme.textPrimary
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.path
                            wrapMode: Text.WordWrap
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            color: Theme.textSecondary
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("Setup guide")
                subtitle: qsTr("Walks through choosing your microphone, the virtual microphone, and headphones.")
                PageButton {
                    objectName: "runSetupAgain"
                    text: qsTr("Run the setup guide")
                    iconName: "info"
                    onClicked: page.system.showFirstRunAgain()
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                title: qsTr("Advanced")
                RowLayout {
                    spacing: 10
                    PageButton {
                        objectName: "openSettingsFolder"
                        text: qsTr("Open the settings folder")
                        iconName: "external"
                        onClicked: page.app.runAction("open-settings-folder")
                    }
                    PageButton {
                        objectName: "resetSettings"
                        text: qsTr("Reset all settings")
                        iconName: "reset"
                        onClicked: confirmReset.open()
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 20
                title: qsTr("About")
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Voxwright %1. Built with Qt, miniaudio, RNNoise, libsamplerate, and Signalsmith Stretch, among others; their licenses are in the licenses folder installed with Voxwright.").arg(page.app.version)
                    wrapMode: Text.WordWrap
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    color: Theme.textSecondary
                }
            }
        }
    }

    Popup {
        id: confirmReset
        objectName: "confirmReset"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 400
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
                text: qsTr("Reset all settings?")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Devices, levels, hotkeys, favorites, and voice adjustments go back to how they were on first start. Your own voices and soundboards are kept.")
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                color: Theme.textSecondary
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 10
                PageButton {
                    text: qsTr("Cancel")
                    onClicked: confirmReset.close()
                }
                PageButton {
                    objectName: "confirmResetButton"
                    text: qsTr("Reset")
                    iconName: "reset"
                    onClicked: {
                        page.app.runAction("reset-settings")
                        confirmReset.close()
                    }
                }
            }
        }
    }
}
