import QtQuick
import QtQuick.Controls

// Timeline: drawing and dragging are pure UI. A drag only moves the visual
// clip; the model is edited once, on release (one command, one undo step).
Rectangle {
    id: root
    color: Theme.panel
    property double pxPerSec: 60
    readonly property int headerW: 120
    readonly property int trackH: 54
    readonly property int rulerH: 24

    function timeToX(t) { return headerW + t * pxPerSec - flick.contentX }

    Flickable {
        id: flick
        objectName: "timelineFlick"
        anchors.fill: parent
        anchors.leftMargin: root.headerW
        contentWidth: Math.max(width, (editor.duration + 30) * root.pxPerSec)
        contentHeight: root.rulerH + editor.tracks.length * root.trackH + 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar {}

        // Ruler
        Item {
            width: flick.contentWidth; height: root.rulerH
            Repeater {
                model: Math.ceil(flick.contentWidth / root.pxPerSec)
                Item {
                    x: index * root.pxPerSec; height: root.rulerH; width: 1
                    Rectangle { width: 1; height: index % 5 === 0 ? 10 : 5; anchors.bottom: parent.bottom; color: Theme.fg3 }
                    Text {
                        visible: index % 5 === 0; text: Math.floor(index / 60) + ":" + ("0" + index % 60).slice(-2)
                        color: Theme.fg3; font.pixelSize: 10; font.family: Theme.fontMono; x: 3; y: 2
                    }
                }
            }
            MouseArea { anchors.fill: parent; onPressed: (m) => editor.seek(m.x / root.pxPerSec)
                        onPositionChanged: (m) => editor.seek(m.x / root.pxPerSec) }
        }

        // Tracks and clips
        Repeater {
            model: editor.tracks
            delegate: Item {
                id: trackItem
                required property var modelData
                required property int index
                y: root.rulerH + index * root.trackH
                width: flick.contentWidth; height: root.trackH
                Rectangle { anchors.fill: parent; anchors.bottomMargin: 2; color: Theme.panel2 }
                Repeater {
                    model: trackItem.modelData.clips
                    delegate: Rectangle {
                        id: clipRect
                        required property var modelData
                        readonly property bool isAudio: trackItem.modelData.type === "audio"
                        readonly property bool selected: editor.selectedClip === modelData.id
                        x: modelData.start * root.pxPerSec
                        y: 4; height: root.trackH - 10
                        width: Math.max(4, modelData.duration * root.pxPerSec)
                        radius: 4
                        color: isAudio ? Theme.clipAudio2 : Theme.clipVideo2
                        border.color: selected ? Theme.accent : (isAudio ? Theme.clipAudio : Theme.clipVideo)
                        border.width: selected ? 2 : 1
                        Text {
                            anchors { left: parent.left; leftMargin: 6; top: parent.top; topMargin: 4; right: parent.right }
                            text: (clipRect.modelData.hasFx ? "fx · " : "") + clipRect.modelData.name
                            elide: Text.ElideRight; color: Theme.fg; font.pixelSize: 11
                        }
                        // Body drag = move (committed on release)
                        MouseArea {
                            anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
                            drag.target: clipRect; drag.axis: Drag.XAxis; drag.minimumX: 0
                            onPressed: editor.selectedClip = clipRect.modelData.id
                            onReleased: if (clipRect.x !== clipRect.modelData.start * root.pxPerSec)
                                            editor.moveClip(clipRect.modelData.id, trackItem.modelData.id, clipRect.x / root.pxPerSec)
                        }
                        // Edge handles = trim
                        Repeater {
                            model: 2
                            MouseArea {
                                required property int index
                                width: 6; height: parent.height
                                x: index === 0 ? 0 : parent.width - 6
                                cursorShape: Qt.SizeHorCursor
                                property double startX: 0
                                onPressed: (m) => { startX = m.x; editor.selectedClip = clipRect.modelData.id }
                                onReleased: (m) => {
                                    const dx = (m.x - startX) / root.pxPerSec
                                    const t = index === 0 ? clipRect.modelData.start + dx
                                                          : clipRect.modelData.start + clipRect.modelData.duration + dx
                                    if (Math.abs(dx) > 0.01) editor.trimClip(clipRect.modelData.id, index === 0, t)
                                }
                            }
                        }
                    }
                }
            }
        }

        // Playhead
        Rectangle {
            x: editor.playhead * root.pxPerSec; width: 2; height: flick.contentHeight
            color: Theme.accent
            Rectangle { width: 10; height: 10; radius: 2; color: Theme.accent; x: -4; y: 0; rotation: 45 }
        }
    }

    // Track headers
    Column {
        y: root.rulerH; width: root.headerW
        Repeater {
            model: editor.tracks
            delegate: Rectangle {
                required property var modelData
                width: root.headerW; height: root.trackH; color: Theme.panel
                border.color: Theme.lineSoft
                Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: modelData.name
                       color: Theme.fg2; font.pixelSize: 12 }
            }
        }
    }

    // Zoom with Ctrl + wheel
    WheelHandler {
        acceptedModifiers: Qt.ControlModifier
        onWheel: (e) => root.pxPerSec = Math.max(5, Math.min(400, root.pxPerSec * (e.angleDelta.y > 0 ? 1.15 : 0.87)))
    }
}
