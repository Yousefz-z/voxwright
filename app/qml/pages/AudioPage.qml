import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// Devices, microphone processing, monitoring, and the latency readout.
Item {
    id: page

    required property AppContext app
    readonly property AudioController audio: app.audio

    function dbLabel(v) { return (v > 0 ? "+" : "") + v.toFixed(1) + " dB" }

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroller.availableWidth
            spacing: 16

            Item { height: 4 }

            Text {
                Layout.leftMargin: 20
                text: qsTr("Audio")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
                color: Theme.textPrimary
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 20
                columns: width > 900 ? 2 : 1
                columnSpacing: 16
                rowSpacing: 16

                Card {
                    objectName: "devicesCard"
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: qsTr("Devices")
                    subtitle: qsTr("Voxwright listens to your microphone and plays your changed voice into a virtual microphone that other apps record from.")

                    DevicePicker {
                        objectName: "inputPicker"
                        label: qsTr("Microphone")
                        iconName: "mic"
                        model: page.audio.inputDevices
                        currentId: page.audio.inputDeviceId
                        onPicked: id => page.audio.inputDeviceId = id
                    }
                    DevicePicker {
                        objectName: "virtualMicPicker"
                        label: qsTr("Virtual microphone output")
                        iconName: "cable"
                        model: page.audio.virtualMicDevices
                        currentId: page.audio.virtualMicDeviceId
                        onPicked: id => page.audio.virtualMicDeviceId = id
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: cableRow.implicitHeight + 16
                        radius: Theme.radiusSmall
                        color: Theme.surfaceRaised
                        RowLayout {
                            id: cableRow
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 10
                            Icon {
                                name: page.audio.virtualCableFound ? "check" : "warning"
                                color: page.audio.virtualCableFound ? Theme.positive : Theme.warning
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                color: Theme.textSecondary
                                text: page.audio.virtualCableFound
                                      ? qsTr("In Discord, Zoom, OBS, or your game, choose \"%1\" as the microphone.").arg(page.audio.chatAppMicrophoneName)
                                      : qsTr("No virtual microphone found. Install %1 (free) to use your voice in other apps.").arg(page.audio.virtualCableProduct)
                            }
                            Button {
                                id: getCable
                                visible: !page.audio.virtualCableFound
                                text: qsTr("Get %1").arg(page.audio.virtualCableProduct)
                                onClicked: page.app.runAction("get-virtual-cable")
                                contentItem: Text {
                                    text: getCable.text
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: Font.DemiBold
                                    color: "#1A0E0A"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    implicitHeight: 30
                                    implicitWidth: 110
                                    radius: Theme.radiusSmall
                                    color: getCable.pressed ? Theme.accentPressed : Theme.accent
                                }
                            }
                        }
                    }
                    DevicePicker {
                        objectName: "monitorPicker"
                        label: qsTr("Headphones (hear myself and sounds)")
                        iconName: "headphones"
                        model: page.audio.monitorDevices
                        currentId: page.audio.monitorDeviceId
                        onPicked: id => page.audio.monitorDeviceId = id
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: qsTr("Microphone")
                    subtitle: qsTr("Clean up the input before the voice effect.")

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Icon { name: "mic"; width: 18; height: 18; color: page.audio.gateOpen ? Theme.positive : Theme.textMuted }
                        LevelMeter {
                            Layout.fillWidth: true
                            level: page.audio.inputLevel
                            threshold: page.audio.gateEnabled ? (page.audio.gateThresholdDb + 60) / 60 : -1
                        }
                        Text {
                            text: page.audio.inputLevelDb > -100 ? page.audio.inputLevelDb.toFixed(0) + " dB" : qsTr("silent")
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textMuted
                            Layout.preferredWidth: 48
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                    LabeledSlider {
                        label: qsTr("Input gain")
                        from: -24
                        to: 24
                        stepSize: 0.5
                        value: page.audio.inputGainDb
                        formatValue: page.dbLabel
                        onMoved: v => page.audio.inputGainDb = v
                    }
                    SwitchRow {
                        objectName: "noiseReductionSwitch"
                        label: qsTr("Noise reduction")
                        detail: qsTr("Removes fans, hum, and room noise. Adds 20 ms of delay while on.")
                        checked: page.audio.noiseReduction
                        onToggled: on => page.audio.noiseReduction = on
                    }
                    LabeledSlider {
                        visible: page.audio.noiseReduction
                        label: qsTr("Strength")
                        from: 0
                        to: 1
                        value: page.audio.noiseReductionStrength
                        formatValue: v => Math.round(v * 100) + " %"
                        onMoved: v => page.audio.noiseReductionStrength = v
                    }
                    SwitchRow {
                        label: qsTr("Noise gate")
                        detail: qsTr("Mutes the microphone between words. Set the line just above your background noise.")
                        checked: page.audio.gateEnabled
                        onToggled: on => page.audio.gateEnabled = on
                    }
                    LabeledSlider {
                        visible: page.audio.gateEnabled
                        label: qsTr("Gate threshold")
                        from: -80
                        to: -10
                        value: page.audio.gateThresholdDb
                        formatValue: page.dbLabel
                        onMoved: v => page.audio.gateThresholdDb = v
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: qsTr("Monitoring and mix")

                    SwitchRow {
                        objectName: "hearMyselfSwitch"
                        label: qsTr("Hear myself")
                        detail: qsTr("Play your changed voice in your headphones. Turns itself off if it starts to howl.")
                        checked: page.audio.hearMyself
                        onToggled: on => page.audio.hearMyself = on
                    }
                    LabeledSlider {
                        label: qsTr("Headphone volume")
                        from: -40
                        to: 6
                        stepSize: 0.5
                        value: page.audio.monitorLevelDb
                        formatValue: page.dbLabel
                        onMoved: v => page.audio.monitorLevelDb = v
                    }
                    LabeledSlider {
                        label: qsTr("Voice level")
                        from: -40
                        to: 6
                        stepSize: 0.5
                        value: page.audio.voiceLevelDb
                        formatValue: page.dbLabel
                        onMoved: v => page.audio.voiceLevelDb = v
                    }
                    LabeledSlider {
                        label: qsTr("Sounds level")
                        from: -40
                        to: 6
                        stepSize: 0.5
                        value: page.audio.soundsLevelDb
                        formatValue: page.dbLabel
                        onMoved: v => page.audio.soundsLevelDb = v
                    }
                    LabeledSlider {
                        label: qsTr("Text-to-speech level")
                        from: -40
                        to: 6
                        stepSize: 0.5
                        value: page.audio.speechLevelDb
                        formatValue: page.dbLabel
                        onMoved: v => page.audio.speechLevelDb = v
                    }
                }

                Card {
                    objectName: "latencyCard"
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    title: qsTr("Latency")
                    subtitle: qsTr("Delay from your microphone to the virtual microphone inside Voxwright. Your sound card and system add their own buffering on top.")

                    RowLayout {
                        spacing: 8
                        Icon { name: "clock"; color: Theme.accent }
                        Text {
                            objectName: "latencyValue"
                            text: page.audio.running ? qsTr("%1 ms").arg(page.audio.latencyMs.toFixed(1)) : qsTr("Audio stopped")
                            font.family: Theme.fontFamily
                            font.pixelSize: 20
                            font.weight: Font.Bold
                            color: Theme.textPrimary
                        }
                    }
                    Repeater {
                        model: page.audio.latencyBreakdown
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: modelData.label
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                color: Theme.textSecondary
                            }
                            Text {
                                text: modelData.ms.toFixed(1) + " ms"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                color: Theme.textMuted
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Processing load")
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textSecondary
                        }
                        Text {
                            objectName: "processingLoad"
                            text: page.audio.running ? Math.round(page.audio.processingLoad * 100) + " %" : "-"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            color: page.audio.processingLoad > 0.5 ? Theme.warning : Theme.textMuted
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Buffer size")
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            color: Theme.textPrimary
                        }
                        ComboBox {
                            id: bufferBox
                            model: [64, 128, 256, 480]
                            currentIndex: Math.max(0, model.indexOf(page.audio.bufferFrames))
                            displayText: currentValue + qsTr(" samples")
                            onActivated: index => page.audio.bufferFrames = model[index]
                            Accessible.name: qsTr("Buffer size")
                        }
                    }
                    SwitchRow {
                        visible: Qt.platform.os === "windows"
                        label: qsTr("Exclusive mode")
                        detail: qsTr("Bypasses the Windows mixer for about 10 ms less delay. Other apps cannot use the devices meanwhile.")
                        checked: page.audio.exclusiveMode
                        onToggled: on => page.audio.exclusiveMode = on
                    }
                }
            }
        }
    }
}
