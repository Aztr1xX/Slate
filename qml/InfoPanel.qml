import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Slate

// Media inspector: file, video, audio and loudness facts, with warnings that matter for editing.
Item {
    id: panel
    required property var mpv
    readonly property var p: Media.probe
    readonly property var i: mpv.info
    readonly property var loud: Media.loudness

    function mbps(bits) { return bits > 0 ? (bits / 1e6).toFixed(bits >= 1e7 ? 0 : 1) + " Mb/s" : "" }
    function kbps(bits) { return bits > 0 ? Math.round(bits / 1000) + " kb/s" : "" }
    function rate(r) { return r > 0 ? (Math.abs(r - Math.round(r)) < 0.005 ? Math.round(r) : r.toFixed(3)) + " fps" : "" }
    readonly property bool hdr: p.colorTransfer === "smpte2084" || p.colorTransfer === "arib-std-b67"

    component Section: Text {
        Layout.topMargin: 14; Layout.bottomMargin: 2
        color: Theme.ink2; font.pixelSize: Theme.fontLabel; font.weight: Font.Medium
    }
    component Fact: RowLayout {
        property string label
        property string value
        property bool show: true
        visible: show && value !== "" && value !== "undefined"
        Layout.fillWidth: true
        spacing: 10
        Text { Layout.preferredWidth: 96; text: label; color: Theme.ink3; font.pixelSize: Theme.fontLabel }
        Text { Layout.fillWidth: true; text: value; color: Theme.ink; font.pixelSize: Theme.fontLabel; wrapMode: Text.Wrap }
    }
    component Badge: Rectangle {
        property alias text: bt.text
        property color tone: Theme.warn
        Layout.fillWidth: true
        implicitHeight: bt.implicitHeight + 14
        radius: Theme.radius
        color: Qt.rgba(tone.r, tone.g, tone.b, 0.09)
        Rectangle { width: 2; height: parent.height - 12; y: 6; x: 0; radius: 1; color: parent.tone }
        Text { id: bt; anchors.fill: parent; anchors.margins: 7; anchors.leftMargin: 12; color: Theme.ink; font.pixelSize: Theme.fontLabel; lineHeight: 1.15; wrapMode: Text.WordWrap }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        WheelScroll {}
        ScrollBar.vertical: SScrollBar {}

        ColumnLayout {
            id: col
            x: 14; width: parent.width - 28
            spacing: 6

            Item { Layout.preferredHeight: 2 }

            Text {
                visible: !panel.mpv.loaded
                Layout.topMargin: 8
                text: "Open a file to see its details."
                color: Theme.ink3; font.pixelSize: 12
            }

            Badge {
                visible: panel.p.vfr === true
                Layout.topMargin: 8
                text: "Variable frame rate (" + Math.round(panel.p.fpsMin) + " to " + Math.round(panel.p.fpsMax) + " fps). Phones and screen recorders do this. Some editors drift out of sync with VFR, so convert it to a constant frame rate before editing if you see audio slip."
            }
            Badge {
                visible: panel.hdr
                tone: Theme.accent
                text: "HDR video. Slate shows it tone-mapped to SDR, so colours on screen are an approximation."
            }

            Section { visible: panel.mpv.loaded; text: "File" }
            Fact { label: "Name"; value: App.currentName }
            Fact { label: "Format"; value: panel.p.container || (panel.i.container || "") }
            Fact { label: "Size"; value: panel.i.fileSize ? App.formatBytes(panel.i.fileSize) : "" }
            Fact { label: "Duration"; value: panel.mpv.duration > 0 ? App.formatClock(panel.mpv.duration) : "" }
            Fact { label: "Bitrate"; value: panel.mbps(panel.p.bitrate) }

            Section { visible: panel.mpv.hasVideo; text: "Video" }
            Fact { show: panel.mpv.hasVideo; label: "Codec"; value: panel.p.videoCodec ? panel.p.videoCodec.toUpperCase() + (panel.p.videoProfile ? " · " + panel.p.videoProfile : "") : (panel.i.videoCodec || "") }
            Fact { show: panel.mpv.hasVideo; label: "Resolution"; value: panel.i.width ? panel.i.width + " × " + panel.i.height : "" }
            Fact { show: panel.mpv.hasVideo; label: "Frame rate"; value: panel.rate(panel.p.frameRate || panel.mpv.fps) + (panel.p.vfr === true ? " (variable)" : (panel.p.vfr === false ? " (constant)" : "")) }
            Fact { show: panel.mpv.hasVideo; label: "Bitrate"; value: panel.mbps(panel.p.videoBitrate) }
            Fact { show: panel.mpv.hasVideo; label: "Pixels"; value: panel.p.pixelFormat || panel.i.pixelFormat || "" }
            Fact { show: panel.mpv.hasVideo; label: "Colour"; value: [panel.p.colorPrimaries || panel.i.primaries, panel.p.colorTransfer || panel.i.gamma].filter(x => x && x !== "unknown").join(" · ") }
            Fact { show: panel.mpv.hasVideo; label: "Scan"; value: panel.p.fieldOrder && panel.p.fieldOrder !== "progressive" && panel.p.fieldOrder !== "unknown" ? "Interlaced (" + panel.p.fieldOrder + ")" : "" }
            Fact { show: panel.mpv.hasVideo; label: "Decoding"; value: panel.i.hwdec && panel.i.hwdec !== "no" ? "Hardware (" + panel.i.hwdec + ")" : (panel.mpv.loaded ? "Software" : "") }

            Section { visible: panel.mpv.loaded && (panel.p.audioCodec || panel.i.audioCodec); text: "Audio" }
            Fact { label: "Codec"; value: (panel.p.audioCodec || panel.i.audioCodec || "").toUpperCase() }
            Fact { label: "Channels"; value: panel.p.channels ? panel.p.channels + (panel.p.channelLayout ? " (" + panel.p.channelLayout + ")" : "") : "" }
            Fact { label: "Sample rate"; value: panel.p.sampleRate ? (panel.p.sampleRate / 1000) + " kHz" : "" }
            Fact { label: "Bitrate"; value: panel.kbps(panel.p.audioBitrate) }

            Section { visible: panel.mpv.loaded && (panel.loud.measuring || panel.loud.integrated !== undefined); text: "Loudness" }
            Text { visible: panel.loud.measuring === true; text: "Measuring…"; color: Theme.ink3; font.pixelSize: 12 }
            Fact { label: "Integrated"; value: panel.loud.integrated !== undefined ? panel.loud.integrated.toFixed(1) + " LUFS" : "" }
            Fact { label: "Range"; value: panel.loud.range !== undefined ? panel.loud.range.toFixed(1) + " LU" : "" }
            Fact { label: "True peak"; value: panel.loud.truePeak !== undefined ? panel.loud.truePeak.toFixed(1) + " dBTP" : "" }
            Text {
                visible: panel.loud.integrated !== undefined
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "YouTube turns playback down to about −14 LUFS, so louder than that gains nothing. Keep true peak below −1 dBTP to avoid clipping."
                color: Theme.ink3; font.pixelSize: 11
            }
        }
    }
}
