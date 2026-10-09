import QtQuick
import QtQuick.Shapes
import Voxwright

// The Voxwright mark: a waveform peak on a warm tile.
Item {
    id: root
    implicitWidth: 32
    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        radius: width * 0.24
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: "#FF9A6B" }
            GradientStop { position: 1.0; color: "#E8503A" }
        }
    }
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer // smooth edges, as in Icon.qml
        antialiasing: true
        ShapePath {
            scale: Qt.size(root.width / 32, root.height / 32)
            strokeColor: "#1A0E0A"
            strokeWidth: 2.8 * root.width / 32
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: "M7.5 11.5 L13 22 L16 15.5 L19 22 L24.5 11.5" }
        }
        ShapePath {
            scale: Qt.size(root.width / 32, root.height / 32)
            strokeColor: "transparent"
            fillColor: "#1A0E0A"
            PathSvg { path: "M16 7.6 a1.6 1.6 0 1 1 0 3.2 a1.6 1.6 0 1 1 0 -3.2 Z" }
        }
    }
}
