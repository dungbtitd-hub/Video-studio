pragma Singleton
import QtQuick

// Design tokens shared with the approved prototype and the Flutter app (design/tokens.json).
QtObject {
    readonly property color bg: "#0f1115"
    readonly property color panel: "#171a20"
    readonly property color panel2: "#1d2128"
    readonly property color raised: "#252a33"
    readonly property color line: "#2c323c"
    readonly property color lineSoft: "#232830"
    readonly property color fg: "#e7e9ee"
    readonly property color fg2: "#a4abb8"
    readonly property color fg3: "#6e7685"
    readonly property color accent: "#f2a93b"
    readonly property color accentInk: "#1b1206"
    readonly property color accentSoft: Qt.rgba(242 / 255, 169 / 255, 59 / 255, 0.16)
    readonly property color clipVideo: "#4a6fa5"
    readonly property color clipVideo2: "#36557f"
    readonly property color clipAudio: "#2f9c8f"
    readonly property color clipAudio2: "#22766c"
    readonly property color ok: "#5cc28a"
    readonly property color warn: "#e5b94a"
    readonly property color bad: "#e2665e"
    readonly property string fontBody: "Inter, Segoe UI, sans-serif"
    readonly property string fontMono: "JetBrains Mono, Consolas, monospace"
    readonly property int radius: 6
}
