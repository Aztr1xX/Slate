import QtQuick
import QtQuick.Controls
import Slate

// Timeline: time ruler, audio waveform, In/Out range, markers and playhead.
// Click or drag to scrub (fast while dragging, exact on release); the playhead snaps to markers
// and In/Out when it passes close (hold Shift to scrub freely). Drag the In and Out edges to trim.
// Ctrl+wheel zooms around the pointer, wheel scrolls when zoomed; hover shows a frame preview.
Item {
    id: tl
    required property var mpv
    property bool overlay: false            // drawn over the video in full screen
    implicitHeight: 84

    readonly property double dur: mpv.duration > 0 ? mpv.duration : 0
    property double viewStart: 0
    property double viewSpan: dur           // seconds visible
    readonly property double viewEnd: viewStart + viewSpan
    readonly property bool zoomed: dur > 0 && viewSpan < dur - 0.001
    readonly property double frameDur: mpv.fps > 0 ? 1 / mpv.fps : 0.04
    onDurChanged: { viewStart = 0; viewSpan = dur }

    function xFor(t) { return viewSpan > 0 ? (t - viewStart) / viewSpan * track.width : 0 }
    function tFor(x) { return viewSpan > 0 ? Math.max(0, Math.min(dur, viewStart + x / track.width * viewSpan)) : 0 }
    function fmt(t) { return mpv.hasVideo && mpv.fps > 0 ? App.formatTimecode(t, mpv.fps) : App.formatClock(t) }
    function clampView() {
        viewSpan = Math.max(Math.min(dur, viewSpan), Math.min(dur, 2))
        viewStart = Math.max(0, Math.min(dur - viewSpan, viewStart))
    }
    function zoomAt(factor, x) {
        if (dur <= 0) return
        const t = tFor(x)
        viewSpan = viewSpan * factor
        clampView()
        viewStart = t - x / track.width * viewSpan
        clampView()
    }
    function zoomBy(factor) { zoomAt(factor, Math.max(0, Math.min(track.width, xFor(mpv.position)))) }
    function resetZoom() { viewStart = 0; viewSpan = dur }
    // Nearest marker, In or Out within 7 px of x, else the time under x.
    function snapped(x, free) {
        const t = tFor(x)
        if (free) return t
        let best = t, bestPx = 7
        const consider = (c) => { if (c < 0) return; const d = Math.abs(xFor(c) - x); if (d < bestPx) { bestPx = d; best = c } }
        consider(App.inPoint)
        consider(App.outPoint)
        for (let i = 0; i < App.markers.count; ++i) consider(App.markers.timeAt(i))
        return best
    }

    // Keep the playhead in view while playing when zoomed in.
    Connections {
        target: tl.mpv
        function onPositionChanged() {
            if (!tl.zoomed || area.pressed) return
            const p = tl.mpv.position
            if (p < tl.viewStart || p > tl.viewEnd) { tl.viewStart = p - tl.viewSpan * 0.1; tl.clampView() }
        }
    }

    Rectangle { anchors.fill: parent; color: tl.overlay ? "transparent" : Theme.chrome }

    // ---- ruler -------------------------------------------------------------------
    Item {
        id: ruler
        x: 12; y: 5
        width: parent.width - 24; height: 16
        readonly property var steps: [0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600]
        readonly property double step: {
            if (tl.viewSpan <= 0 || width <= 0) return 1
            for (let i = 0; i < steps.length; ++i) if (steps[i] / tl.viewSpan * width >= 84) return steps[i]
            return 7200
        }
        readonly property var ticks: {
            const out = []
            if (tl.viewSpan <= 0) return out
            const first = Math.ceil(tl.viewStart / step - 1e-9) * step
            for (let t = first; t <= tl.viewEnd + 1e-6 && out.length < 60; t += step) out.push(t)
            return out
        }
        Repeater {
            model: ruler.ticks
            delegate: Item {
                required property double modelData
                x: tl.xFor(modelData)
                Rectangle { width: 1; height: 5; y: 11; color: Theme.lineStrong }
                Text {
                    x: 4; y: -1
                    visible: parent.x + 4 + implicitWidth < ruler.width
                    text: ruler.step < 1 ? App.formatClock(modelData) + "." + Math.round((modelData % 1) * 10) % 10 : App.formatClock(modelData)
                    color: Theme.ink3; font.pixelSize: Theme.fontTiny; font.family: Theme.mono
                }
            }
        }
    }

    // ---- track -------------------------------------------------------------------
    Rectangle {
        id: track
        x: 12; y: 24
        width: parent.width - 24; height: 48
        radius: 5
        color: tl.overlay ? Qt.rgba(0, 0, 0, 0.45) : Theme.bg
        border.color: Theme.line
        clip: true

        WaveformItem {
            anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: 4
            viewStart: tl.viewStart; viewEnd: tl.viewEnd
            color: "#8a93a0"; peakColor: "#3d434c"
            visible: hasData
        }
        Text {
            anchors.centerIn: parent
            visible: tl.dur > 0 && !Media.waveformReady && Media.file !== "" && Media.peakRevision >= 0 && Media.waveformProgress < 0.02
            text: "Reading audio…"
            color: Theme.ink3; font.pixelSize: Theme.fontSmall
        }
        // played part
        Rectangle {
            width: Math.max(0, Math.min(track.width, tl.xFor(tl.mpv.position)))
            height: parent.height
            color: Qt.rgba(0.31, 0.55, 1, 0.13)
        }
        // in/out range
        Rectangle {
            id: range
            visible: App.inPoint >= 0 || App.outPoint >= 0
            readonly property double a: App.inPoint >= 0 ? App.inPoint : 0
            readonly property double b: App.outPoint >= 0 ? App.outPoint : tl.dur
            x: tl.xFor(a)
            width: Math.max(2, tl.xFor(b) - tl.xFor(a))
            height: parent.height
            color: Qt.rgba(0.31, 0.55, 1, 0.16)
            Rectangle { visible: App.inPoint >= 0; width: 2; height: parent.height; color: Theme.accent }
            Rectangle { visible: App.outPoint >= 0; x: parent.width - 2; width: 2; height: parent.height; color: Theme.accent }
            Rectangle {
                visible: App.inPoint >= 0 && App.outPoint > App.inPoint && parent.width > lenText.implicitWidth + 16
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height - height - 3
                width: lenText.implicitWidth + 10; height: 15; radius: 3
                color: Qt.rgba(0.31, 0.55, 1, 0.85)
                Text { id: lenText; anchors.centerIn: parent; text: App.formatClock(range.b - range.a) + (range.b - range.a < 60 ? " · " + (range.b - range.a).toFixed(1) + " s" : ""); color: "white"; font.pixelSize: Theme.fontTiny; font.family: Theme.mono }
            }
        }
        // hover line
        Rectangle {
            visible: area.containsMouse && !area.pressed && tl.dur > 0
            x: area.mouseX
            width: 1; height: parent.height
            color: Qt.rgba(1, 1, 1, 0.35)
        }
        // playhead
        Rectangle {
            x: tl.xFor(tl.mpv.position) - 1
            width: 2; height: parent.height
            color: "#ffffff"
            visible: tl.dur > 0 && x >= -2 && x <= track.width
        }
    }

    // markers sit above the track so they can be clicked and hovered
    Item {
        x: track.x; y: track.y
        width: track.width; height: track.height
        clip: true
        z: 3
        Repeater {
            model: App.markers
            delegate: Item {
                id: mk
                required property int index
                required property double time
                required property int kind
                required property string note
                x: tl.xFor(time) - 7
                width: 14; height: parent.height
                visible: x > -14 && x < parent.width + 14
                Rectangle { x: 6; width: 2; height: parent.height; color: Theme.kindColor(mk.kind); opacity: mh.hovered ? 1 : 0.75 }
                Rectangle {
                    x: 2; y: 0
                    width: 10; height: 10; radius: 2
                    color: Theme.kindColor(mk.kind)
                    border.width: mh.hovered ? 2 : 0; border.color: "white"
                }
                HoverHandler { id: mh; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: { tl.mpv.paused = true; tl.mpv.seekTo(mk.time, true) } }
                Tip {
                    visible: mh.hovered
                    delay: 150
                    text: (mk.note || Theme.kindName(mk.kind)) + "  ·  " + tl.fmt(mk.time)
                }
                // only the head takes clicks; the line lets scrubbing through
                containmentMask: QtObject { function contains(point: point) : bool { return point.y < 14 } }
            }
        }
    }

    // In / Out handles: drag to trim
    Repeater {
        model: [{ which: "in" }, { which: "out" }]
        delegate: Item {
            id: handle
            required property var modelData
            readonly property bool isIn: modelData.which === "in"
            readonly property double t: isIn ? App.inPoint : App.outPoint
            visible: t >= 0 && tl.dur > 0 && hx >= -6 && hx <= track.width + 6
            readonly property real hx: tl.xFor(t)
            x: track.x + hx - (isIn ? 1 : 9)
            y: track.y
            z: 4
            width: 10; height: track.height
            Rectangle {
                x: handle.isIn ? 1 : 5
                y: track.height - 14
                width: 4; height: 12; radius: 2
                color: hh.hovered || drag.active ? "white" : Theme.accent
            }
            HoverHandler { id: hh; cursorShape: Qt.SizeHorCursor }
            DragHandler {
                id: drag
                target: null
                xAxis.enabled: true; yAxis.enabled: false
                property double startT: 0
                onActiveChanged: if (active) { startT = handle.t; tl.mpv.paused = true }
                onTranslationChanged: {
                    if (!active) return
                    let nt = tl.snapped(tl.xFor(startT) + translation.x, false)
                    if (handle.isIn) {
                        if (App.outPoint >= 0) nt = Math.min(nt, App.outPoint - tl.frameDur)
                        App.inPoint = Math.max(0, nt)
                    } else {
                        if (App.inPoint >= 0) nt = Math.max(nt, App.inPoint + tl.frameDur)
                        App.outPoint = Math.min(tl.dur, nt)
                    }
                    tl.mpv.seekTo(handle.t, false)
                }
            }
            Tip { visible: hh.hovered && !drag.active; delay: 300; text: (handle.isIn ? "In " : "Out ") + tl.fmt(handle.t) + ". Drag to trim" }
        }
    }

    // playhead knob on the ruler
    Rectangle {
        readonly property double px: track.x + tl.xFor(tl.mpv.position)
        visible: tl.dur > 0 && px >= track.x - 1 && px <= track.x + track.width + 1
        x: px - 6; y: track.y - 8
        width: 12; height: 8; radius: 2
        color: "#ffffff"
    }

    // zoom scrollbar
    Rectangle {
        visible: tl.zoomed
        x: track.x; y: track.y + track.height + 4
        width: track.width; height: 4; radius: 2
        color: Theme.raise
        Rectangle {
            x: tl.dur > 0 ? tl.viewStart / tl.dur * parent.width : 0
            width: tl.dur > 0 ? Math.max(14, tl.viewSpan / tl.dur * parent.width) : 0
            height: parent.height; radius: 2
            color: Theme.ink3
        }
    }

    // ---- hover preview ----------------------------------------------------------------
    Rectangle {
        id: preview
        readonly property double t: tl.tFor(area.mouseX)
        readonly property bool hasThumb: Media.spriteUrl !== "" && tl.mpv.hasVideo && Media.spriteCount > 0
        visible: area.containsMouse && tl.dur > 0 && !area.pressed
        width: hasThumb ? 194 : timeLabel.implicitWidth + 18
        height: hasThumb ? 109 + 26 : 26
        x: Math.max(4, Math.min(tl.width - width - 4, track.x + area.mouseX - width / 2))
        y: track.y - height - 12
        radius: 7
        color: "#f21b1d21"
        border.color: Theme.lineStrong
        z: 10
        Image {
            visible: preview.hasThumb
            x: 1; y: 1; width: 192; height: 108
            source: preview.hasThumb ? Media.spriteUrl : ""
            readonly property int idx: Math.max(0, Math.min(Media.spriteCount - 1, Math.floor(preview.t / Math.max(0.001, Media.spriteInterval))))
            sourceClipRect: Qt.rect((idx % 10) * 192, Math.floor(idx / 10) * 108, 192, 108)
            fillMode: Image.PreserveAspectFit
            cache: true
        }
        Text {
            id: timeLabel
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom; anchors.bottomMargin: 5
            text: tl.fmt(preview.t)
            color: Theme.ink; font.pixelSize: Theme.fontSmall; font.family: Theme.mono
        }
    }

    MouseArea {
        id: area
        x: track.x; y: ruler.y
        width: track.width; height: track.y + track.height - ruler.y + 6
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        enabled: tl.dur > 0
        property bool wasPaused: true
        onPressed: (m) => { wasPaused = tl.mpv.paused; tl.mpv.paused = true; tl.mpv.seekTo(tl.snapped(m.x, m.modifiers & Qt.ShiftModifier), false) }
        onPositionChanged: (m) => { if (pressed) tl.mpv.seekTo(tl.snapped(m.x, m.modifiers & Qt.ShiftModifier), false) }
        onReleased: (m) => { tl.mpv.seekTo(tl.snapped(m.x, m.modifiers & Qt.ShiftModifier), true); if (!wasPaused) tl.mpv.paused = false }
        onDoubleClicked: if (tl.zoomed) tl.resetZoom()
        onWheel: (w) => {
            const d = w.angleDelta.y !== 0 ? w.angleDelta.y : w.angleDelta.x
            if (w.modifiers & Qt.ControlModifier) tl.zoomAt(d > 0 ? 0.8 : 1.25, w.x)
            else if (tl.zoomed) { tl.viewStart -= d / 120 * tl.viewSpan * 0.1; tl.clampView() }
            else { w.accepted = false; return }
            w.accepted = true
        }
    }
}
