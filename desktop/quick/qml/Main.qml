import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import VideoStudio

ApplicationWindow {
    id: win
    width: 1440; height: 900
    minimumWidth: 1024; minimumHeight: 640
    visible: true
    color: Theme.bg
    title: "Video Studio — " + editor.projectName + (editor.dirty ? " •" : "")

    component ToolButtonX: Button {
        id: tb
        property bool primary: false
        flat: true
        padding: 8
        contentItem: Text {
            text: tb.text; font.pixelSize: 13
            color: tb.primary ? Theme.accentInk : (tb.enabled ? Theme.fg : Theme.fg3)
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radius
            color: tb.primary ? Theme.accent : (tb.hovered ? Theme.raised : "transparent")
            border.color: tb.primary ? Theme.accent : Theme.line
        }
    }

    component PanelTitle: Text {
        color: Theme.fg3; font.pixelSize: 11; font.capitalization: Font.AllUppercase; font.letterSpacing: 1
    }

    // ---- shortcuts (same as the prototype) ----
    Shortcut { sequences: [StandardKey.Undo]; onActivated: editor.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; onActivated: editor.redo() }
    Shortcut { sequence: "Space"; onActivated: editor.togglePlay() }
    Shortcut { sequences: ["S", "Ctrl+B"]; onActivated: editor.splitAtPlayhead() }
    Shortcut { sequences: [StandardKey.Delete, "Backspace"]; onActivated: editor.deleteSelected() }
    Shortcut { sequence: "Left"; onActivated: editor.stepFrames(-1) }
    Shortcut { sequence: "Right"; onActivated: editor.stepFrames(1) }
    Shortcut { sequences: [StandardKey.Save]; onActivated: editor.dirty && editor.status !== "" && saveDialog.open() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: importDialog.open() }

    FileDialog {
        id: importDialog
        title: "Import media"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Media (*.mp4 *.mov *.mkv *.webm *.m4v *.avi *.mp3 *.wav *.m4a *.aac *.flac *.jpg *.jpeg *.png *.webp)", "All files (*)"]
        onAccepted: editor.importMedia(selectedFiles)
    }
    FolderDialog { id: openDialog; title: "Open project (.vstudio folder)"; onAccepted: editor.openProject(selectedFolder) }
    FileDialog {
        id: saveDialog; title: "Save project"; fileMode: FileDialog.SaveFile; defaultSuffix: "vstudio"
        onAccepted: editor.saveProject(selectedFile)
    }
    FileDialog {
        id: exportDialog; title: "Export MP4"; fileMode: FileDialog.SaveFile; defaultSuffix: "mp4"
        nameFilters: ["MP4 (*.mp4)"]
        onAccepted: editor.exportTo(selectedFile, 0, 0)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 1

        // ---- top bar ----
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 48; color: Theme.panel
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 8
                Rectangle { width: 22; height: 22; radius: 5; color: Theme.accent
                    Text { anchors.centerIn: parent; text: "▶"; color: Theme.accentInk; font.pixelSize: 11 } }
                Text { text: "Video Studio"; color: Theme.fg; font.pixelSize: 14; font.bold: true }
                Text { text: "·  " + editor.projectName; color: Theme.fg2; font.pixelSize: 13 }
                Item { Layout.fillWidth: true }
                ToolButtonX { text: "Mở"; onClicked: openDialog.open() }
                ToolButtonX { text: "Lưu"; onClicked: saveDialog.open() }
                ToolButtonX { text: "↶"; enabled: editor.canUndo; onClicked: editor.undo(); ToolTip.visible: hovered; ToolTip.text: "Hoàn tác " + editor.undoLabel }
                ToolButtonX { text: "↷"; enabled: editor.canRedo; onClicked: editor.redo() }
                ToolButtonX {
                    primary: true
                    text: editor.exporting ? "Đang xuất " + Math.round(editor.exportProgress * 100) + "%  ✕" : "Xuất video"
                    onClicked: editor.exporting ? editor.cancelExport() : exportDialog.open()
                }
            }
        }

        // ---- workspace ----
        SplitView {
            Layout.fillWidth: true; Layout.fillHeight: true
            orientation: Qt.Vertical
            handle: Rectangle { implicitHeight: 3; color: SplitHandle.hovered ? Theme.accent : Theme.bg }

            SplitView {
                SplitView.fillHeight: true
                SplitView.minimumHeight: 260
                handle: Rectangle { implicitWidth: 3; color: SplitHandle.hovered ? Theme.accent : Theme.bg }

                // Media library
                Rectangle {
                    SplitView.preferredWidth: 280; SplitView.minimumWidth: 200
                    color: Theme.panel
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 8
                        RowLayout {
                            PanelTitle { text: "Thư viện media"; Layout.fillWidth: true }
                            ToolButtonX { text: "+ Nhập"; onClicked: importDialog.open() }
                        }
                        Text {
                            visible: editor.pendingJobs > 0
                            text: "Đang xử lý nền: " + editor.pendingJobs + " tác vụ"
                            color: Theme.warn; font.pixelSize: 11
                        }
                        ListView {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            clip: true; spacing: 6
                            model: editor.assets
                            delegate: Rectangle {
                                required property var modelData
                                width: ListView.view.width; height: 52; radius: Theme.radius
                                color: ma.containsMouse ? Theme.raised : Theme.panel2
                                border.color: Theme.lineSoft
                                Column {
                                    anchors.verticalCenter: parent.verticalCenter; x: 10; spacing: 2
                                    width: parent.width - 20
                                    Text { text: modelData.name; color: Theme.fg; font.pixelSize: 12; elide: Text.ElideMiddle; width: parent.width }
                                    Text { text: modelData.type + "  " + modelData.resolution + "  " + modelData.duration.toFixed(1) + " s"
                                           color: Theme.fg3; font.pixelSize: 11; font.family: Theme.fontMono }
                                }
                                MouseArea {
                                    id: ma; anchors.fill: parent; hoverEnabled: true
                                    onDoubleClicked: editor.addToTimeline(modelData.id)
                                    ToolTip.visible: containsMouse; ToolTip.delay: 600
                                    ToolTip.text: "Nhấp đúp để thêm vào Timeline"
                                }
                            }
                            Text {
                                anchors.centerIn: parent; visible: parent.count === 0
                                text: "Nhập video, ảnh hoặc âm thanh\n(Ctrl+O)"; horizontalAlignment: Text.AlignHCenter
                                color: Theme.fg3; font.pixelSize: 12
                            }
                        }
                    }
                }

                // Preview
                Rectangle {
                    SplitView.fillWidth: true; SplitView.minimumWidth: 360
                    color: Theme.bg
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 10; spacing: 8
                        PreviewItem {
                            id: preview
                            objectName: "preview"   // wired to the controller in main.cpp
                            Layout.fillWidth: true; Layout.fillHeight: true
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Text { text: editor.timecode; color: Theme.fg; font.family: Theme.fontMono; font.pixelSize: 13 }
                            Item { Layout.fillWidth: true }
                            ToolButtonX { text: "⏮"; onClicked: editor.seek(0) }
                            ToolButtonX { text: "◀"; onClicked: editor.stepFrames(-1) }
                            ToolButtonX { text: editor.playing ? "❚❚" : "▶"; primary: true; onClicked: editor.togglePlay() }
                            ToolButtonX { text: "▶|"; onClicked: editor.stepFrames(1) }
                            Item { Layout.fillWidth: true }
                            Text { text: editor.duration.toFixed(2) + " s"; color: Theme.fg3; font.family: Theme.fontMono; font.pixelSize: 12 }
                        }
                    }
                }

                // Inspector
                Rectangle {
                    SplitView.preferredWidth: 260; SplitView.minimumWidth: 200
                    color: Theme.panel
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 10
                        PanelTitle { text: "Thuộc tính" }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.Wrap
                            text: editor.selectedClip === "" ? "Chọn một clip trên Timeline" : "Clip đã chọn"
                            color: Theme.fg2; font.pixelSize: 12
                        }
                        ToolButtonX { text: "Tách tại playhead (S)"; Layout.fillWidth: true; onClicked: editor.splitAtPlayhead() }
                        ToolButtonX { text: "Xóa clip (Del)"; Layout.fillWidth: true; enabled: editor.selectedClip !== ""; onClicked: editor.deleteSelected() }
                        Item { Layout.fillHeight: true }
                    }
                }
            }

            TimelineView {
                SplitView.preferredHeight: 280; SplitView.minimumHeight: 140
            }
        }

        // ---- status bar ----
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 24; color: Theme.panel
            Text {
                anchors.verticalCenter: parent.verticalCenter; x: 12
                text: editor.status === "" ? "Sẵn sàng" : editor.status
                color: Theme.fg3; font.pixelSize: 11
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 12
                text: editor.fps.toFixed(2) + " fps"
                color: Theme.fg3; font.pixelSize: 11; font.family: Theme.fontMono
            }
        }
    }
}
