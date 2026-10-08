pragma Singleton
import QtQuick

// Colors, sizes, and type for the whole app.
QtObject {
    readonly property color background: "#0F1117"
    readonly property color sidebar: "#141720"
    readonly property color surface: "#1A1E29"
    readonly property color surfaceRaised: "#222736"
    readonly property color surfaceHover: "#2A3043"
    readonly property color border: "#2C3245"
    readonly property color textPrimary: "#EEF0F6"
    readonly property color textSecondary: "#A6ACBF"
    readonly property color textMuted: "#6E748A"
    readonly property color accent: "#FF7A59"
    readonly property color accentPressed: "#E8603F"
    readonly property color accentSoft: "#3B2620"
    readonly property color positive: "#3DD68C"
    readonly property color warning: "#F5B841"
    readonly property color danger: "#F2545B"
    readonly property color info: "#5AA9F5"
    readonly property color meterLow: "#3DD68C"
    readonly property color meterMid: "#F5B841"
    readonly property color meterHigh: "#F2545B"

    readonly property int radius: 12
    readonly property int radiusSmall: 7
    readonly property int spacing: 12
    readonly property int sidebarWidth: 208
    readonly property int bottomBarHeight: 76

    // Native UI fonts on the shipping platforms; Inter elsewhere.
    readonly property string fontFamily: Qt.platform.os === "windows" ? "Segoe UI"
                                       : Qt.platform.os === "osx" ? "Helvetica Neue" : "Inter"
    readonly property int fontTitle: 22
    readonly property int fontHeading: 15
    readonly property int fontBody: 13
    readonly property int fontSmall: 11

    function levelColor(level) {
        return level === 2 ? danger : level === 1 ? warning : info
    }
}
