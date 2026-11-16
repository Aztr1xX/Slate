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
    // Beat grid from the Music section: [bpm, first beat, beats per bar] or empty.
    property var beatGrid: []
    property bool beatsShown: false
    function beatTimes(from, to) {
        const out = []
        if (!beatsShown || beatGrid.length < 2 || beatGrid[0] <= 0) return out
        const step = 60 / beatGrid[0], first = beatGrid[1], bar = beatGrid[2] || 4
        let k = Math.ceil((from - first) / step)
        for (let t = first + k * step; t <= to && out.length < 4000; t += step, ++k) if (t >= 0) out.push({ t: t, bar: ((k % bar) + bar) % bar === 0 })
        return out
    }
    // Snaps to markers, select ends, In / Out and (when shown) beats. skipId leaves one select out.
    function snapped(x, free, skipId) {
        const t = tFor(x)
        if (free) return t
        let best = t, bestPx = 7
        const consider = (c) => { if (c < 0) return; const d = Math.abs(xFor(c) - x); if (d < bestPx) { bestPx = d; best = c } }
        consider(App.inPoint)
        consider(App.outPoint)
        for (let i = 0; i < App.markers.count; ++i) {
            if (skipId !== undefined && App.markers.idAt(i) === skipId) continue
            consider(App.markers.timeAt(i))
            consider(App.markers.endAt(i))
        }
        if (beatsShown) { const span = viewSpan * 7 / Math.max(1, track.width); for (const b of beatTimes(t - span, t + span)) consider(b.t) }
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
                Rectangle { width: 1; height: 4; y: 12; color: Theme.ink4 }
                Text {
                    x: 4; y: -1
                    visible: parent.x + 4 + implicitWidth < ruler.width
                    text: ruler.step < 1 ? App.formatClock(modelData) + "." + Math.round((modelData % 1) * 10) % 10 : App.formatClock(modelData)
                    color: Theme.ink3; font.pixelSize: Theme.fontTiny
                }
            }
        }
    }

    // ---- track -------------------------------------------------------------------
    Rectangle {
        id: track
        x: 12; y: 24
        width: parent.width - 24; height: 48
        radius: 4
        color: tl.overlay ? Qt.rgba(0, 0, 0, 0.5) : Theme.well
        clip: true

        WaveformItem {
            anchors.fill: parent; anchors.topMargin: 4; anchors.bottomMargin: 4
            viewStart: tl.viewStart; viewEnd: tl.viewEnd
            color: "#7e8696"; peakColor: "#353a45"
            visible: hasData
        }
        Text {
            anchors.centerIn: parent
            visible: tl.dur > 0 && !Media.waveformReady && Media.file !== "" && Media.peakRevision >= 0 && Media.waveformProgress < 0.02
            text: "Reading audio…"
            color: Theme.ink3; font.pixelSize: Theme.fontSmall
        }
        // beat grid
        Repeater {
            model: tl.beatsShown && tl.viewSpan > 0 && tl.beatGrid.length >= 2 && (60 / tl.beatGrid[0]) / tl.viewSpan * track.width >= 3 ? tl.beatTimes(tl.viewStart, tl.viewEnd) : []
            delegate: Rectangle {
                required property var modelData
                x: tl.xFor(modelData.t); width: 1; height: track.height
                color: modelData.bar ? Qt.rgba(1, 1, 1, 0.22) : Qt.rgba(1, 1, 1, 0.08)
            }
        }
        // played part
        Rectangle {
            width: Math.max(0, Math.min(track.width, tl.xFor(tl.mpv.position)))
            height: parent.height
            color: Qt.rgba(1, 1, 1, 0.035)
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
            color: Theme.accentSoft
            Rectangle { visible: App.inPoint >= 0; width: 2; height: parent.height; color: Theme.accent }
            Rectangle { visible: App.outPoint >= 0; x: parent.width - 2; width: 2; height: parent.height; color: Theme.accent }
            Rectangle {
                visible: App.inPoint >= 0 && App.outPoint > App.inPoint && parent.width > lenText.implicitWidth + 16
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height - height - 3
                width: lenText.implicitWidth + 12; height: 16; radius: 4
                color: Theme.popup; border.color: Theme.lineStrong
                Text { id: lenText; anchors.centerIn: parent; text: App.formatClock(range.b - range.a) + (range.b - range.a < 60 ? " · " + (range.b - range.a).toFixed(1) + " s" : ""); color: Theme.ink; font.pixelSize: Theme.fontTiny; font.weight: Font.Medium }
            }
        }
        // hover line
        Rectangle {
            visible: area.containsMouse && !area.pressed && tl.dur > 0
            x: area.mouseX
            width: 1; height: parent.height
            color: Qt.rgba(1, 1, 1, 0.28)
        }
        // playhead
        Rectangle {
            x: tl.xFor(tl.mpv.position) - 1
            width: 2; height: parent.height
            color: Theme.ink
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
                required property double end
                required property bool isRange
                required property int kind
                required property string note
                required property int markerId
                readonly property color tone: Theme.kindColor(kind)
                readonly property real x0: tl.xFor(time)
                readonly property real x1: isRange ? tl.xFor(end) : x0
                x: isRange ? x0 : x0 - 7
                width: isRange ? Math.max(4, x1 - x0) : 14
                height: parent.height
                visible: x + width > -14 && x < parent.width + 14
                z: isRange ? 0 : 1

                // ---- point marker: a flag head and a line ----
                Rectangle { visible: !mk.isRange; x: 6; width: 2; height: parent.height; color: mk.tone; opacity: mh.hovered ? 1 : 0.7 }
                Rectangle {
                    visible: !mk.isRange
                    x: 3; y: 0
                    width: 8; height: 8; radius: 2
                    color: mk.tone
                    border.width: mh.hovered ? 1.5 : 0; border.color: Theme.ink
                }

                // ---- select: a tinted span with a labelled bar along the top ----
                Rectangle { visible: mk.isRange; anchors.fill: parent; color: mk.tone; opacity: mh.hovered || edgeA.active || edgeB.active ? 0.16 : 0.09 }
                Rectangle {
                    visible: mk.isRange
                    width: parent.width; height: 13; radius: 3
                    color: Qt.rgba(mk.tone.r, mk.tone.g, mk.tone.b, mh.hovered ? 0.95 : 0.8)
                    Text {
                        x: 5; width: parent.width - 10; anchors.verticalCenter: parent.verticalCenter
                        visible: parent.width > 34
                        text: mk.note && mk.note !== "Select" ? mk.note : App.formatClock(mk.end - mk.time)
                        color: "#0b0c0f"; font.pixelSize: Theme.fontTiny; font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
                HoverHandler { id: mh; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: { tl.mpv.paused = true; tl.mpv.seekTo(mk.time, true) }
                    onDoubleTapped: if (mk.isRange) { App.inPoint = mk.time; App.outPoint = mk.end; tl.mpv.seekTo(mk.time, true) }
                }
                Tip {
                    visible: mh.hovered && !edgeA.active && !edgeB.active
                    delay: 150
                    text: mk.isRange ? (mk.note || "Select") + "  ·  " + tl.fmt(mk.time) + " to " + tl.fmt(mk.end) + " (" + App.formatClock(mk.end - mk.time) + ")  ·  drag the ends, double-click for In / Out"
                                     : (mk.note || Theme.kindName(mk.kind)) + "  ·  " + tl.fmt(mk.time)
                }
                // only the head or bar takes clicks; the rest lets scrubbing through
                containmentMask: QtObject { function contains(point: point) : bool { return point.y < 14 } }

                // drag either end of a select
                component Edge: Item {
                    id: edge
                    property bool isStart: true
                    readonly property alias active: dh.active
                    visible: mk.isRange
                    x: isStart ? -5 : mk.width - 5
                    width: 10; height: 14
                    z: 2
                    Rectangle { anchors.centerIn: parent; width: 3; height: 11; radius: 1.5; color: Theme.ink; visible: eh.hovered || dh.active }
                    HoverHandler { id: eh; cursorShape: Qt.SizeHorCursor }
                    DragHandler {
                        id: dh
                        target: null
                        xAxis.enabled: true; yAxis.enabled: false
                        property double startT: 0
                        onActiveChanged: {
                            if (active) { startT = edge.isStart ? mk.time : mk.end; tl.mpv.paused = true; App.markers.beginGesture() }
                            else App.markers.endGesture("adjust select")
                        }
                        onTranslationChanged: {
                            if (!active) return
                            const row = App.markers.rowOfId(mk.markerId)
                            if (row < 0) return
                            const a = App.markers.timeAt(row), b = App.markers.endAt(row)
                            let nt = tl.snapped(tl.xFor(startT) + translation.x, false, mk.markerId)
                            if (edge.isStart) { nt = Math.max(0, Math.min(nt, b - tl.frameDur)); App.markers.setRange(row, nt, b) }
                            else { nt = Math.min(tl.dur, Math.max(nt, a + tl.frameDur)); App.markers.setRange(row, a, nt) }
                            tl.mpv.seekTo(nt, false)
                        }
                    }
                }
                Edge { id: edgeA; isStart: true }
                Edge { id: edgeB; isStart: false }
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
                color: hh.hovered || drag.active ? Theme.ink : Theme.accent
            }
            HoverHandler { id: hh; cursorShape: Qt.SizeHorCursor }
            DragHandler {
                id: drag
                target: null
                xAxis.enabled: true; yAxis.enabled: false
                property double startT: 0
                onActiveChanged: {
                    if (active) { startT = handle.t; tl.mpv.paused = true; App.beginInOutEdit() }
                    else App.endInOutEdit()
                }
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
        x: px - 5; y: track.y - 7
        width: 10; height: 7; radius: 2
        color: Theme.ink
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
        radius: Theme.radiusMd
        color: Theme.popup
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
            color: Theme.ink; font.pixelSize: Theme.fontSmall; font.weight: Font.Medium
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
