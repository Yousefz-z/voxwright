import QtQuick
import QtQuick.Controls.Basic
import Voxwright

// A themed drop-down for a list of strings (or objects with textRole).
ComboBox {
    id: combo

    implicitHeight: 34
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody

    background: Rectangle {
        radius: Theme.radiusSmall
        color: combo.hovered ? Theme.surfaceHover : Theme.surfaceRaised
        border.width: 1
        border.color: combo.visualFocus ? Theme.accent : Theme.border
    }
    contentItem: Text {
        leftPadding: 10
        rightPadding: combo.indicator.width + 8
        text: combo.displayText
        font: combo.font
        color: combo.enabled ? Theme.textPrimary : Theme.textMuted
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: combo.width - width - 10
        y: (combo.height - height) / 2
        text: "▾"
        color: Theme.textSecondary
        font.pixelSize: 12
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: combo.width
        highlighted: combo.highlightedIndex === index
        contentItem: Text {
            text: combo.textRole.length > 0 ? option.modelData[combo.textRole] : option.modelData
            font: combo.font
            color: Theme.textPrimary
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: option.highlighted ? Theme.surfaceHover : Theme.surfaceRaised
        }
    }
    popup: Popup {
        y: combo.height + 2
        width: combo.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 320)
        padding: 1
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: combo.popup.visible ? combo.delegateModel : null
            currentIndex: combo.highlightedIndex
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.border
        }
    }
}
