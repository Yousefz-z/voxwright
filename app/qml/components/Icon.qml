import QtQuick
import QtQuick.Shapes
import Voxwright

// A line icon from Icons.qml in any color and size.
Item {
    id: root

    property string name: "utility"
    property color color: Theme.textSecondary
    property bool filled: false
    property real lineWidth: 1.8

    implicitWidth: 20
    implicitHeight: 20

    // Rendered into a texture: the software renderer (used without a GPU,
    // and in tests) does not clip Shape nodes to a scrolling view, while
    // textures are clipped like everything else.
    layer.enabled: true
    layer.smooth: true

    Shape {
        anchors.fill: parent
        antialiasing: true

        ShapePath {
            scale: Qt.size(root.width / 24, root.height / 24)
            strokeColor: root.color
            strokeWidth: root.lineWidth * Math.min(root.width, root.height) / 24
            fillColor: root.filled ? root.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: Icons.path(root.name) }
        }
    }
}
