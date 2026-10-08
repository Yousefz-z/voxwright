import QtQuick
import Voxwright

// Horizontal level bar (0..1 = -60..0 dBFS) with an optional threshold mark.
Item {
    id: root

    property real level: 0
    property real threshold: -1 // 0..1, negative hides the mark

    implicitHeight: 6
    implicitWidth: 160

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Theme.surfaceHover
    }
    Rectangle {
        width: Math.max(0, Math.min(1, root.level)) * parent.width
        height: parent.height
        radius: height / 2
        color: root.level > 0.95 ? Theme.meterHigh : root.level > 0.8 ? Theme.meterMid : Theme.meterLow
        Behavior on width { NumberAnimation { duration: 60 } }
    }
    Rectangle {
        visible: root.threshold >= 0
        x: root.threshold * parent.width - 1
        y: -3
        width: 2
        height: parent.height + 6
        radius: 1
        color: Theme.textPrimary
    }
}
