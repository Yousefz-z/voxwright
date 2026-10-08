import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Voxwright

// A slider with its name and current value.
ColumnLayout {
    id: root

    property string label
    property alias from: slider.from
    property alias to: slider.to
    property alias value: slider.value
    property alias stepSize: slider.stepSize
    property string unit
    property int decimals: 0
    property var formatValue: null
    signal moved(real value)

    spacing: 4
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: root.label
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontBody
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
        Text {
            text: root.formatValue ? root.formatValue(slider.value)
                                   : slider.value.toFixed(root.decimals) + (root.unit.length > 0 ? " " + root.unit : "")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            color: Theme.textMuted
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        Accessible.name: root.label
        onMoved: root.moved(value)

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: Theme.surfaceHover
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Theme.accent
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 16
            height: 16
            radius: 8
            color: slider.pressed ? Theme.accentPressed : Theme.textPrimary
            border.width: 2
            border.color: Theme.accent
        }
    }
}
