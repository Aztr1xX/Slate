import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Slate

// Download a video from YouTube (or any site yt-dlp supports) at the best quality on offer.
// Paste a link, pick a format and resolution, download. It keeps going if the dialog is closed;
// the header shows the progress.
Popup {
    id: dlg
    required property var win

    property string kind: App.setting("dl/kind", "video")
    property string videoFormat: App.setting("dl/videoFormat", "mp4")
    property string audioFormat: App.setting("dl/audioFormat", "mp3")
    property int maxHeight: 0                     // 0 = best available
    property bool editFriendly: App.setting("dl/editFriendly", false)
    property bool openWhenDone: App.setting("dl/openWhenDone", true)
    property string lookedUp: ""
    property string presetUrl: ""                 // handed over from Open URL

    readonly property string st: Downloader.state
    readonly property var info: Downloader.info
    readonly property bool hasInfo: info && info.title !== undefined
    readonly property bool working: st === "downloading" || st === "converting"
    readonly property bool hasVideo: hasInfo && info.hasVideo === true
    readonly property string format: kind === "video" && hasVideo ? videoFormat : audioFormat

    readonly property var videoFormats: [
        { id: "mp4", name: "MP4", note: "Best quality, plays anywhere" },
        { id: "mkv", name: "MKV", note: "Original streams, untouched" },
        { id: "webm", name: "WebM", note: "YouTube's own VP9 and Opus" },
        { id: "mov", name: "MOV", note: "H.264 and AAC, for editing" },
        { id: "avi", name: "AVI", note: "For older players" }
    ]
    readonly property var audioFormats: [
        { id: "mp3", name: "MP3", note: "320 kbps, plays anywhere" },
        { id: "m4a", name: "M4A", note: "AAC stream, not re-encoded" },
        { id: "wav", name: "WAV", note: "Uncompressed, for editing" },
        { id: "flac", name: "FLAC", note: "Lossless, smaller than WAV" },
        { id: "opus", name: "Opus", note: "Best stream, not re-encoded" },
        { id: "ogg", name: "OGG", note: "Vorbis, open format" }
    ]

    function quality() {
        const qs = hasInfo ? (info.qualities || []) : []
        if (!qs.length) return null
        if (maxHeight > 0) { const q = qs.find(x => x.height === maxHeight); if (q) return q }
        return qs[0]
    }
    function summary() {
        if (!hasInfo) return ""
        if (kind === "audio" || !hasVideo) {
            const src = "the " + (info.bestAudio || "best audio") + " stream"
            return {
                mp3: "Made from " + src + " at 320 kbps.",
                m4a: "YouTube's AAC audio stream, saved as it is. Nothing is re-encoded.",
                wav: "Made from " + src + ", uncompressed, with no further loss.",
                flac: "Made from " + src + ", losslessly compressed, with no further loss.",
                opus: "YouTube's Opus audio stream, saved as it is. Nothing is re-encoded.",
                ogg: "Made from " + src + " at the top Vorbis quality setting."
            }[audioFormat]
        }
        const q = quality()
        if (!q) return ""
        const pic = q.label + (q.hdr ? " HDR" : "")
        const size = q.size ? "  About " + q.size + "." : ""
        let how = ""
        if (videoFormat === "mkv") how = q.codec + " video and " + (info.bestAudio || "the best audio") + ", exactly as YouTube has them. Nothing is re-encoded."
        else if (videoFormat === "webm") how = "VP9 video and Opus audio, exactly as YouTube has them. Nothing is re-encoded."
        else if (videoFormat === "mp4" && !editFriendly) how = q.codec + " video and AAC audio, joined without re-encoding." + (q.codec !== "H.264" ? " Some editing apps can't open " + q.codec + ". If yours can't, turn on the option above." : "")
        else how = "H.264 video and " + (videoFormat === "avi" ? "MP3" : "AAC") + " audio. Uses YouTube's H.264 stream when it has one at this size, otherwise converts at a visually lossless setting (this takes a while for long or 4K videos)."
        return pic + ": " + how + size
    }
    function lookUp() {
        const u = field.text.trim()
        if (!u || working) return
        if (u === lookedUp && (st === "ready" || st === "probing")) return
        lookedUp = u
        maxHeight = 0
        Downloader.lookUp(u)
    }
    function begin() {
        App.setSetting("dl/kind", kind)
        if (kind === "video") App.setSetting("dl/videoFormat", videoFormat); else App.setSetting("dl/audioFormat", audioFormat)
        Downloader.start(kind === "video" && hasVideo ? "video" : "audio", format, kind === "video" ? maxHeight : 0, videoFormat === "mp4" && editFriendly)
    }
    function startOver() { Downloader.reset(); field.text = ""; lookedUp = ""; field.forceActiveFocus() }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(680, parent.width - 60)
    padding: 22
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
    background: Rectangle { color: Theme.panel; radius: Theme.radiusLg; border.color: Theme.lineStrong }
    onOpened: {
        Downloader.prepare()
        if (working) return
        if (presetUrl) { field.text = presetUrl; presetUrl = ""; lookUp(); return }
        if (st === "done") return
        if (st === "error" && !hasInfo) Downloader.reset()
        if (field.text.trim() === "") {
            const c = Downloader.clipboardUrl()
            if (c) { field.text = c; lookUp() }
        }
        field.forceActiveFocus()
    }

    FolderDialog {
        id: folderPicker
        title: "Choose where downloads go"
        onAccepted: Downloader.folder = App.localPath(selectedFolder)
    }

    component FormatTile: Rectangle {
        id: tile
        required property var modelData
        property bool selected: false
        signal pick()
        implicitHeight: 54
        radius: Theme.radius
        color: selected ? Theme.accentSoft : (tma.containsMouse ? Theme.raise : Theme.bg)
        border.color: selected ? Theme.accent : Theme.line
        Column {
            anchors.left: parent.left; anchors.leftMargin: 12
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text { text: tile.modelData.name; color: Theme.ink; font.pixelSize: 14; font.weight: Font.DemiBold }
            Text { width: parent.width; text: tile.modelData.note; color: tile.selected ? Theme.ink2 : Theme.ink3; font.pixelSize: 11; elide: Text.ElideRight }
        }
        MouseArea { id: tma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: tile.pick() }
    }

    ColumnLayout {
        width: parent.width
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Icon { name: "download"; size: 20; color: Theme.accent }
            Text { Layout.fillWidth: true; text: "Download from YouTube"; color: Theme.ink; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold }
            Btn { glyph: "close"; iconSize: 15; implicitWidth: 28; implicitHeight: 28; tip: dlg.working ? "Hide (the download keeps going)" : "Close"; keys: "Esc"; onClicked: dlg.close() }
        }

        // link
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: field
                Layout.fillWidth: true
                implicitHeight: 36
                enabled: !dlg.working
                placeholderText: "Paste a YouTube link"
                placeholderTextColor: Theme.ink3
                color: Theme.ink; font.pixelSize: Theme.fontBody
                selectByMouse: true; selectionColor: Theme.accent
                leftPadding: 12
                background: Rectangle { radius: Theme.radius; color: Theme.bg; border.color: field.activeFocus ? Theme.accent : Theme.line }
                onAccepted: dlg.lookUp()
                onTextEdited: if (Downloader.looksLikeUrl(text)) pasteTimer.restart()
                Keys.onEscapePressed: dlg.close()
                Timer { id: pasteTimer; interval: 450; onTriggered: dlg.lookUp() }
            }
            Btn {
                flat: false; text: "Paste"; glyph: "copy"; implicitHeight: 36
                enabled: !dlg.working
                tip: "Paste the link you copied"
                onClicked: { const c = Downloader.clipboardUrl(); if (c) { field.text = c; dlg.lookUp() } else win.toast("There's no link on the clipboard. Copy the video's address first") }
            }
        }

        // looking up / tools / errors
        RowLayout {
            visible: dlg.st === "probing"
            Layout.fillWidth: true
            spacing: 10
            BusyIndicator { running: parent.visible; implicitWidth: 22; implicitHeight: 22 }
            Text { Layout.fillWidth: true; text: Downloader.toolsNote || "Looking up the video"; color: Theme.ink2; font.pixelSize: 12; elide: Text.ElideRight }
            Btn { text: "Cancel"; onClicked: { Downloader.cancel(); dlg.lookedUp = "" } }
        }
        Rectangle {
            visible: dlg.st === "error"
            Layout.fillWidth: true
            implicitHeight: errText.implicitHeight + 20
            radius: Theme.radius
            color: Qt.rgba(0.9, 0.28, 0.3, 0.12)
            border.color: Qt.rgba(0.9, 0.28, 0.3, 0.45)
            Text { id: errText; x: 12; width: parent.width - 24; anchors.verticalCenter: parent.verticalCenter; text: Downloader.error; color: Theme.ink; font.pixelSize: 12; wrapMode: Text.WordWrap }
        }
        Text {
            visible: !dlg.hasInfo && dlg.st !== "probing"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Slate downloads the highest resolution, frame rate and audio quality the video has, and joins them without re-encoding wherever the format allows. Most other video sites work too."
            color: Theme.ink3; font.pixelSize: 12
        }

        // the video
        Rectangle {
            visible: dlg.hasInfo
            Layout.fillWidth: true
            implicitHeight: 92
            radius: Theme.radius
            color: Theme.bg
            border.color: Theme.line
            Rectangle {
                id: thumbBox
                x: 8; y: 8; width: 135; height: 76
                color: Theme.raise
                clip: true
                Icon { anchors.centerIn: parent; name: "film"; size: 22; color: Theme.ink3; visible: thumb.status !== Image.Ready }
                Image { id: thumb; anchors.fill: parent; source: dlg.hasInfo ? (dlg.info.thumbnail || "") : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true }
            }
            Column {
                anchors.left: thumbBox.right; anchors.leftMargin: 14
                anchors.right: parent.right; anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Text { width: parent.width; text: dlg.hasInfo ? dlg.info.title : ""; color: Theme.ink; font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                Text {
                    width: parent.width
                    text: dlg.hasInfo ? [dlg.info.channel, dlg.info.duration > 0 ? App.formatClock(dlg.info.duration) : ""].filter(s => s).join("  ·  ") : ""
                    color: Theme.ink3; font.pixelSize: 12; elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: dlg.hasInfo ? "Best on offer: " + [dlg.info.bestVideo || "", dlg.info.bestAudio || ""].filter(s => s).join(" + ") : ""
                    color: Theme.ink2; font.pixelSize: 12; elide: Text.ElideRight
                }
            }
        }

        // options
        ColumnLayout {
            visible: dlg.hasInfo && (dlg.st === "ready" || dlg.st === "error")
            Layout.fillWidth: true
            spacing: 12

            Segmented {
                visible: dlg.hasVideo
                Layout.preferredWidth: 280
                options: [{ text: "Video", icon: "film" }, { text: "Audio only", icon: "music" }]
                current: dlg.kind === "video" ? 0 : 1
                onPicked: (i) => dlg.kind = i === 0 ? "video" : "audio"
            }

            Text { text: "Format"; color: Theme.ink2; font.pixelSize: 12; font.weight: Font.DemiBold }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 8; rowSpacing: 8
                Repeater {
                    model: dlg.kind === "video" && dlg.hasVideo ? dlg.videoFormats : dlg.audioFormats
                    delegate: FormatTile {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 100
                        selected: modelData.id === dlg.format
                        onPick: { if (dlg.kind === "video" && dlg.hasVideo) dlg.videoFormat = modelData.id; else dlg.audioFormat = modelData.id }
                    }
                }
            }

            Text { visible: dlg.kind === "video" && dlg.hasVideo; text: "Resolution"; color: Theme.ink2; font.pixelSize: 12; font.weight: Font.DemiBold }
            Flow {
                visible: dlg.kind === "video" && dlg.hasVideo
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: dlg.hasVideo ? dlg.info.qualities : []
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        required property int index
                        readonly property bool selected: dlg.maxHeight === 0 ? index === 0 : modelData.height === dlg.maxHeight
                        width: chipRow.implicitWidth + 20; height: 30; radius: Theme.radius
                        color: selected ? Theme.accentSoft : (cma.containsMouse ? Theme.raise : Theme.bg)
                        border.color: selected ? Theme.accent : Theme.line
                        Row {
                            id: chipRow
                            anchors.centerIn: parent; spacing: 6
                            Text { text: chip.modelData.label; color: Theme.ink; font.pixelSize: 12; font.weight: chip.index === 0 ? Font.DemiBold : Font.Normal; anchors.verticalCenter: parent.verticalCenter }
                            Text { visible: !!chip.modelData.tag || chip.modelData.hdr; text: [chip.modelData.tag, chip.modelData.hdr ? "HDR" : ""].filter(s => s).join(" "); color: chip.index === 0 ? Theme.accent : Theme.ink3; font.pixelSize: 10; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                            Text { visible: chip.index === 0; text: "Best"; color: Theme.ink3; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                        }
                        MouseArea { id: cma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: dlg.maxHeight = chip.index === 0 ? 0 : chip.modelData.height }
                    }
                }
            }

            SSwitch {
                visible: dlg.kind === "video" && dlg.hasVideo && dlg.videoFormat === "mp4"
                Layout.fillWidth: true
                text: "Make sure editing apps can open it (H.264, converts if needed)"
                checked: dlg.editFriendly
                onToggled: { dlg.editFriendly = checked; App.setSetting("dl/editFriendly", checked) }
            }

            Text {
                Layout.fillWidth: true
                text: dlg.summary()
                color: Theme.ink3; font.pixelSize: 12; wrapMode: Text.WordWrap
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Icon { name: "open"; size: 16; color: Theme.ink3 }
                Text { Layout.fillWidth: true; text: Downloader.folderLabel; color: Theme.ink2; font.pixelSize: 12; elide: Text.ElideMiddle }
                Btn { flat: false; text: "Change…"; onClicked: folderPicker.open() }
            }
            RowLayout {
                Layout.fillWidth: true
                SSwitch {
                    Layout.fillWidth: true
                    text: "Open in Slate when it's done"
                    checked: dlg.openWhenDone
                    onToggled: { dlg.openWhenDone = checked; App.setSetting("dl/openWhenDone", checked) }
                }
                Btn { primary: true; glyph: "download"; text: "Download"; implicitHeight: 36; onClicked: dlg.begin() }
            }
        }

        // progress
        ColumnLayout {
            visible: dlg.working
            Layout.fillWidth: true
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: Downloader.status; color: Theme.ink; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                Text { text: Math.round(Downloader.progress * 100) + "%"; color: Theme.ink2; font.pixelSize: Theme.fontBody; font.family: Theme.mono }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 6; radius: 3; color: Theme.bg; border.color: Theme.line
                Rectangle { width: Math.max(6, parent.width * Downloader.progress); height: parent.height; radius: 3; color: Theme.accent; Behavior on width { NumberAnimation { duration: 200 } } }
            }
            Text { text: [Downloader.speed, Downloader.eta].filter(s => s).join("  ·  ") || " "; color: Theme.ink3; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: "You can close this and keep reviewing. The header shows the progress."; color: Theme.ink3; font.pixelSize: 12; wrapMode: Text.WordWrap }
                Btn { flat: false; text: "Cancel download"; onClicked: Downloader.cancel() }
                Btn { primary: true; text: "Keep reviewing"; onClicked: dlg.close() }
            }
        }

        // done
        ColumnLayout {
            visible: dlg.st === "done"
            Layout.fillWidth: true
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Icon { name: "check"; size: 18; color: Theme.good }
                Text { Layout.fillWidth: true; text: Downloader.status; color: Theme.ink; font.pixelSize: Theme.fontBody; elide: Text.ElideMiddle }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Btn { flat: false; text: "Download another"; onClicked: dlg.startOver() }
                Btn { flat: false; text: "Other format"; tip: "Download this video again in another format"; onClicked: Downloader.backToOptions() }
                Item { Layout.fillWidth: true }
                Btn { flat: false; text: App.isWindows ? "Show in Explorer" : "Show in folder"; onClicked: App.revealInFolder(Downloader.outputFile) }
                Btn { primary: true; glyph: "play"; text: "Open in Slate"; onClicked: { dlg.close(); dlg.win.openMedia(Downloader.outputFile) } }
            }
        }
    }
}
