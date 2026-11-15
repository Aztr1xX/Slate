import QtQuick
import QtQuick.Controls
import Slate

// The big timecode under the timeline. Click it (or press Ctrl+G) and type a time to jump there:
// 90, 1:30, 00:01:30:12, f2700 for a frame, or +5 / -2.5 to move relative to now.
Item {
    id: g
    required property var mpv
    property var win: null
    readonly property bool editing: field.visible
    implicitWidth: Math.max(shown.implicitWidth, 150)
    implicitHeight: col.implicitHeight

    function fmt(t) { return mpv.hasVideo && mpv.fps > 0 ? App.formatTimecode(t, mpv.fps) : App.formatClock(t) }
    function begin() {
        if (!mpv.loaded) return
        field.text = fmt(mpv.position)
        field.visible = true
        field.forceActiveFocus()
        field.selectAll()
    }
    function finish(go) {
        if (!field.visible) return
        const raw = field.text.trim()
        // hand the keyboard back to the window so shortcuts work straight away
        const had = field.activeFocus
        field.visible = false          // re-entrant calls stop at the check above
        field.focus = false            // or the window's focus scope would hand focus straight back
        if (had && g.win) g.win.releaseKeyboard()
        if (!go || !raw) return
        const rel = /^[+-]/.test(raw)
        const v = App.parseTime(rel ? raw.substring(1) : raw, mpv.fps)
        if (v < 0) { if (g.win) g.win.toast("Type a time like 1:30, 00:01:30:12 or f2700"); return }
        const t = rel ? mpv.position + (raw[0] === "-" ? -v : v) : v
        mpv.paused = true
        mpv.seekTo(Math.max(0, Math.min(mpv.duration, t)), true)
    }

    Column {
        id: col
        spacing: 1
        Item {
            width: Math.max(shown.implicitWidth, field.visible ? 150 : 0)
            height: shown.implicitHeight
            Text {
                id: shown
                visible: !field.visible
                text: g.mpv.loaded ? g.fmt(g.mpv.position) : (g.mpv.hasVideo ? "00:00:00:00" : "0:00")
                color: g.mpv.loaded ? (hov.hovered ? "#ffffff" : Theme.ink) : Theme.ink3
                font.pixelSize: 18; font.family: Theme.mono
            }
            Rectangle {
                visible: hov.hovered && !field.visible && g.mpv.loaded
                anchors.fill: shown; anchors.margins: -3
                radius: 4; color: "transparent"; border.color: Theme.lineStrong
            }
            TextField {
                id: field
                visible: false
                anchors.verticalCenter: parent.verticalCenter
                x: -6
                width: 170; height: 28
                leftPadding: 6; rightPadding: 6; topPadding: 0; bottomPadding: 0
                font.pixelSize: 16; font.family: Theme.mono
                color: Theme.ink
                selectByMouse: true; selectionColor: Theme.accent
                background: Rectangle { radius: 4; color: Theme.bg; border.color: Theme.accent }
                onAccepted: g.finish(true)
                onActiveFocusChanged: if (!activeFocus) g.finish(false)
                Keys.onEscapePressed: g.finish(false)
            }
            HoverHandler { id: hov; cursorShape: g.mpv.loaded ? Qt.IBeamCursor : Qt.ArrowCursor }
            TapHandler { onTapped: if (!field.visible) g.begin() }
            Tip { visible: hov.hovered && !field.visible && g.mpv.loaded; text: "Go to a time or frame"; keys: "Ctrl+G" }
        }
        Text {
            text: !g.mpv.loaded ? " "
                  : (field.visible ? "Enter to jump · 1:30 · f2700 · +5"
                     : (g.mpv.hasVideo && g.mpv.fps > 0
                        ? "Frame " + g.mpv.frame + "  ·  " + App.formatClock(g.mpv.duration)
                        : "of " + App.formatClock(g.mpv.duration)))
            color: Theme.ink3; font.pixelSize: Theme.fontSmall; font.family: Theme.mono
        }
    }
}
