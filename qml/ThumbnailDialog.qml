import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Slate

// Thumbnail frames: the sharpest, best-lit frames of the clip, spread out. Pick one to save as a
// PNG, a YouTube thumbnail, copy it, jump to it, or make it the file's thumbnail in File Explorer.
Popup {
    id: dlg
    required property var win
    required property var mpv

    parent: Overlay.overlay
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(1000, parent.width - 48)
    height: Math.min(660, parent.height - 48)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.scrim }
    enter: Transition { ParallelAnimation { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.normal; easing.type: Easing.OutCubic } } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast } }
    background: Rectangle { color: Theme.popup; radius: Theme.radiusLg; border.color: Theme.lineStrong }

    property string file: ""
    property int picked: -1
    property bool confirming: false
    property double pendingTime: -1   // set when asked from the right-click menu (the frame on screen)
    property double embedded: -1
    readonly property var cands: Thumbnails.file === file ? Thumbnails.candidates : []
    readonly property bool searching: Thumbnails.busy && Thumbnails.file === file && cands.length === 0
    readonly property var current: picked >= 0 && picked < cands.length ? cands[picked] : null
    readonly property bool canEmbed: file !== "" && Thumbnails.canEmbed(file)

    function refresh() {
        file = App.currentFile
        embedded = Thumbnails.embeddedTime(file)
        picked = -1
        if (pendingTime < 0) confirming = false
        if (Thumbnails.file !== file || Thumbnails.candidates.length === 0) Thumbnails.find(file, mpv.duration)
        else pickBest()
    }
    function pickBest() {
        let best = -1, s = -1
        for (let i = 0; i < cands.length; ++i) if (cands[i].score > s) { s = cands[i].score; best = i }
        picked = best
    }
    onAboutToShow: refresh()
    Connections {
        target: Thumbnails
        function onCandidatesChanged() { if (dlg.opened && dlg.picked < 0) dlg.pickBest() }
        function onFinished(ok, message, path) {
            if (!dlg.opened) return
            dlg.embedded = Thumbnails.embeddedTime(dlg.file)
        }
    }
    function setExplorer() {
        if (!current) return
        if (!App.setting("thumb/noConfirm", false)) { confirming = true; return }
        win.setExplorerThumbnail(file, current.time)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Layout.leftMargin: 20; Layout.rightMargin: 12
            spacing: 10
            Icon { name: "image"; size: 20; color: Theme.accentText }
            Column {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Thumbnail frames"; color: Theme.ink; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold }
                Text { text: "The sharpest, best-lit frames, spread across the clip. Double-click one to go to it."; color: Theme.ink3; font.pixelSize: Theme.fontLabel; elide: Text.ElideRight; width: parent.width }
            }
            Btn { glyph: "loop"; tip: "Look again"; enabled: !Thumbnails.busy; onClicked: { Thumbnails.find(dlg.file, dlg.mpv.duration); dlg.picked = -1 } }
            Btn { glyph: "close"; tip: "Close"; keys: "Esc"; onClicked: dlg.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // candidates
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitWidth: 400
                GridView {
                    id: grid
                    anchors.fill: parent
                    anchors.margins: 14
                    clip: true
                    visible: dlg.cands.length > 0
                    readonly property int cols: width > 620 ? 4 : 3
                    cellWidth: Math.floor(width / cols)
                    cellHeight: Math.round((cellWidth - 10) * 9 / 16) + 34
                    model: dlg.cands
                    boundsBehavior: Flickable.StopAtBounds
                    WheelScroll {}
                    ScrollBar.vertical: SScrollBar {}
                    delegate: Item {
                        id: cell
                        required property var modelData
                        required property int index
                        readonly property bool sel: dlg.picked === index
                        readonly property bool isEmbedded: dlg.embedded >= 0 && Math.abs(dlg.embedded - modelData.time) < 0.05
                        width: grid.cellWidth; height: grid.cellHeight
                        Rectangle {
                            id: frameBox
                            x: 5; y: 5
                            width: parent.width - 10
                            height: Math.round(width * 9 / 16)
                            radius: Theme.radius
                            color: "#000"
                            border.width: cell.sel ? 2 : 1
                            border.color: cell.sel ? Theme.accent : (ch.hovered ? Theme.lineStrong : Theme.line)
                            Image {
                                anchors.fill: parent; anchors.margins: cell.sel ? 2 : 1
                                source: cell.modelData.url
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: false
                                sourceSize.width: 400
                            }
                            Rectangle {
                                visible: cell.modelData.score >= 0.999
                                x: 6; y: 6
                                width: bestTxt.implicitWidth + 12; height: 18; radius: 9
                                color: Theme.accentFill
                                Text { id: bestTxt; anchors.centerIn: parent; text: "Sharpest"; color: "#fff"; font.pixelSize: 10; font.weight: Font.DemiBold }
                            }
                            Rectangle {
                                visible: cell.isEmbedded
                                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 6
                                width: embTxt.implicitWidth + 12; height: 18; radius: 9
                                color: Theme.good
                                Text { id: embTxt; anchors.centerIn: parent; text: "In Explorer"; color: "#0b1a10"; font.pixelSize: 10; font.weight: Font.DemiBold }
                            }
                        }
                        Text {
                            anchors.top: frameBox.bottom; anchors.topMargin: 5
                            x: 6
                            text: dlg.win.fmt(cell.modelData.time)
                            color: cell.sel ? Theme.ink : Theme.ink3
                            font.pixelSize: 11; font.family: Theme.mono
                        }
                        HoverHandler { id: ch }
                        TapHandler { onTapped: dlg.picked = cell.index; onDoubleTapped: { dlg.mpv.seekTo(cell.modelData.time, true); dlg.close() } }
                    }
                }
                Column {
                    anchors.centerIn: parent
                    width: Math.min(360, parent.width - 48)
                    spacing: 10
                    visible: dlg.cands.length === 0
                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "image"; size: 28; color: Theme.ink3 }
                    Text {
                        width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                        text: dlg.searching ? Thumbnails.status : "No good frames found. Try Look again, or use the right-click menu on any frame you like."
                        color: Theme.ink2; font.pixelSize: 12
                    }
                    Rectangle {
                        visible: dlg.searching
                        width: parent.width; height: 4; radius: 2; color: Theme.press
                        Rectangle { width: parent.width * Thumbnails.progress; height: parent.height; radius: 2; color: Theme.accent; Behavior on width { NumberAnimation { duration: 200 } } }
                    }
                }
            }

            Rectangle { Layout.fillHeight: true; width: 1; color: Theme.line }

            // the picked frame
            ColumnLayout {
                Layout.preferredWidth: 290
                Layout.minimumWidth: 290
                Layout.maximumWidth: 290
                Layout.fillHeight: true
                Layout.margins: 16
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 163
                    radius: Theme.radius
                    color: "#000"
                    border.color: Theme.line
                    Image {
                        anchors.fill: parent; anchors.margins: 1
                        source: dlg.current ? dlg.current.url : ""
                        fillMode: Image.PreserveAspectFit
                        cache: false
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: dlg.current ? "Frame at " + dlg.win.fmt(dlg.current.time) : "Pick a frame"
                    color: Theme.ink; font.pixelSize: Theme.fontBody
                }
                Item { Layout.preferredHeight: 4 }
                component Action: Btn {
                    id: act
                    Layout.fillWidth: true
                    flat: false
                    enabled: dlg.current !== null && !Thumbnails.busy
                    contentItem: RowLayout {
                        spacing: 8
                        Icon { Layout.leftMargin: 10; name: act.glyph; size: 16; color: act.iconColor }
                        Text { Layout.fillWidth: true; text: act.text; color: act.primary ? Theme.accentInk : Theme.ink; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                    }
                }
                Action { text: "Make it the Explorer thumbnail"; glyph: "image"; primary: true; visible: dlg.canEmbed; onClicked: dlg.setExplorer() }
                Action { text: "Save for YouTube (1280×720 JPG)"; glyph: "download"; onClicked: Thumbnails.saveFrame(dlg.file, dlg.current.time, "youtube") }
                Action { text: "Save as PNG (full size)"; glyph: "camera"; onClicked: Thumbnails.saveFrame(dlg.file, dlg.current.time, "png") }
                Action { text: "Copy"; glyph: "copy"; onClicked: Thumbnails.copyFrame(dlg.file, dlg.current.time) }
                Action { text: "Go to this frame"; glyph: "frame-fwd"; onClicked: { dlg.mpv.seekTo(dlg.current.time, true); dlg.close() } }
                Item { Layout.fillHeight: true }
                Text {
                    visible: !dlg.canEmbed && dlg.file !== ""
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: "Explorer thumbnails can be set on MP4, MOV, M4V and MKV files."
                    color: Theme.ink3; font.pixelSize: 11
                }
                RowLayout {
                    visible: dlg.embedded >= 0
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: "Explorer shows the frame at " + dlg.win.fmt(dlg.embedded); color: Theme.ink3; font.pixelSize: 11; elide: Text.ElideRight }
                    Btn { text: "Remove"; enabled: !Thumbnails.busy; onClicked: dlg.win.removeExplorerThumbnail(dlg.file) }
                }
                Text {
                    visible: Thumbnails.busy && dlg.cands.length > 0
                    Layout.fillWidth: true
                    text: Thumbnails.status
                    color: Theme.accentText; font.pixelSize: 11
                }
            }
        }
    }

    // first-time explanation before a file is rewritten
    Rectangle {
        anchors.fill: parent
        visible: dlg.confirming
        radius: Theme.radiusLg
        color: Theme.scrim
        MouseArea { anchors.fill: parent }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(440, parent.width - 48)
            height: cc.implicitHeight + 40
            radius: Theme.radiusLg
            color: Theme.popup
            border.color: Theme.lineStrong
            ColumnLayout {
                id: cc
                anchors.fill: parent; anchors.margins: 20
                spacing: 10
                Text { text: "Put this frame inside the file?"; color: Theme.ink; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: "File Explorer shows a video's cover picture as its icon. Slate copies the clip with the picture added (nothing is re-encoded), checks the copy, then swaps it in. Dates, markers and notes stay. If the clip is playing it pauses for a moment."
                    color: Theme.ink2; font.pixelSize: 12; lineHeight: 1.2
                }
                CheckBox {
                    id: dontAsk
                    text: "Don't ask again"
                    leftPadding: 0
                    contentItem: Text { leftPadding: dontAsk.indicator.width + 8; text: dontAsk.text; color: Theme.ink2; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Btn { text: "Cancel"; flat: false; onClicked: { dlg.confirming = false; dlg.pendingTime = -1 } }
                    Btn {
                        text: "Add thumbnail"; primary: true
                        onClicked: {
                            if (dontAsk.checked) App.setSetting("thumb/noConfirm", true)
                            dlg.confirming = false
                            dlg.win.setExplorerThumbnail(dlg.file, dlg.pendingTime >= 0 ? dlg.pendingTime : dlg.current.time)
                            dlg.pendingTime = -1
                        }
                    }
                }
            }
        }
    }
}
