import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Slate

ApplicationWindow {
    id: win
    width: 1440
    height: 880
    minimumWidth: 960
    minimumHeight: 600
    visible: true
    color: Theme.bg
    title: App.currentName ? App.currentName + " – Slate" : "Slate"

    property bool panelsVisible: App.setting("panelsVisible", true)
    // picture zoom: Ctrl+wheel zooms at the pointer, drag pans, Z switches between 100% and 200%
    property real zoomScale: 1
    property real panX: 0
    property real panY: 0
    function pictureRect() {
        const w = mpv.width, h = mpv.height
        const vw = mpv.info.width || 16, vh = mpv.info.height || 9
        const va = vw / vh
        return w / h > va ? Qt.rect((w - h * va) / 2, 0, h * va, h) : Qt.rect(0, (h - w / va) / 2, w, w / va)
    }
    function setZoom(s, px, py) {
        s = Math.max(1, Math.min(8, s))
        // keep the picture covering the frame: pan limited to the overflow
        const lim = (s - 1) / (2 * s)
        px = s <= 1.001 ? 0 : Math.max(-lim, Math.min(lim, px))
        py = s <= 1.001 ? 0 : Math.max(-lim, Math.min(lim, py))
        zoomScale = s; panX = px; panY = py
        mpv.setMpvProperty("video-zoom", Math.log2(s))
        mpv.setMpvProperty("video-pan-x", px)
        mpv.setMpvProperty("video-pan-y", py)
    }
    function zoomAt(s, mx, my) {
        s = Math.max(1, Math.min(8, s))
        const r = pictureRect()
        const cx = mpv.width / 2, cy = mpv.height / 2
        // picture coordinate under the pointer stays under the pointer
        const vx = (mx - cx) / (r.width * zoomScale) - panX
        const vy = (my - cy) / (r.height * zoomScale) - panY
        setZoom(s, (mx - cx) / (r.width * s) - vx, (my - cy) / (r.height * s) - vy)
    }
    function toggleZoom() {
        if (!mpv.hasVideo) return
        if (zoomScale > 1.001) setZoom(1, 0, 0)
        else if (videoMouse.containsMouse) zoomAt(2, videoMouse.mouseX, videoMouse.mouseY)
        else setZoom(2, 0, 0)
    }
    Connections { target: App; function onCurrentFileChanged() { win.setZoom(1, 0, 0) } }

    // crop guide: on/off with G, ratio remembered (1 = 9:16, 2 = 4:5, 3 = 1:1)
    property bool guideOn: false
    property int guideRatio: App.setting("guideRatio", 1)
    readonly property int guideMode: guideOn ? guideRatio : 0
    readonly property var guideNames: ["", "9:16", "4:5", "1:1"]
    function toggleGuide() {
        guideOn = !guideOn
        toast(guideOn ? guideNames[guideRatio] + " crop guide on. Drag it sideways to try a crop" : "Crop guide off")
    }
    function setGuideRatio(r) { guideRatio = r; App.setSetting("guideRatio", r); guideOn = true }
    function cycleGuide() { toggleGuide() }
    property int sideTab: App.setting("sideTab", 0)
    onSideTabChanged: App.setSetting("sideTab", sideTab)
    readonly property bool isFull: visibility === Window.FullScreen
    property bool mouseRecently: true
    readonly property bool chromeVisible: !isFull || mouseRecently || !mpv.loaded || mpv.paused || transport.goTo.editing
    property int rewindRate: 0          // J held: 1, 2, 4, 8
    property bool skipSilence: false
    property double silenceSaved: 0
    property string compareFile: ""
    property double compareOffset: 0
    property int keymapRevision: 0
    property bool wasMaximized: false
    readonly property var speeds: [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4]

    // ----- actions --------------------------------------------------------------
    function toast(msg, path) { toastItem.show(msg, path) }
    function hud(icon, msg, level) { hudItem.show(icon, msg, level) }
    // mpv applies these asynchronously, so the feedback shows the value asked for.
    function setVolume(v) {
        const nv = Math.max(0, Math.min(100, Math.round(v)))
        mpv.volume = nv
        if (mpv.muted && nv > 0) mpv.muted = false
        hud(nv === 0 ? "mute" : (nv < 50 ? "volume-low" : "volume"), "Volume " + nv + "%", nv / 100)
    }
    function toggleMute() {
        const m = !mpv.muted
        mpv.muted = m
        hud(m ? "mute" : "volume", m ? "Muted" : "Volume " + Math.round(mpv.volume) + "%", m ? 0 : mpv.volume / 100)
    }
    function fmt(t) { return mpv.hasVideo && mpv.fps > 0 ? App.formatTimecode(t, mpv.fps) : App.formatClock(t) }

    function openMedia(target) { openMediaAt(target, -1) }
    // Opens a file at a given time (a transcript search result, a repeated take), or the resume point when at < 0.
    function openMediaAt(target, at) {
        if (App.isSubtitleFile(target)) {
            if (mpv.loaded) { mpv.addSubtitle(App.localPath(target)); toast("Subtitles loaded") }
            return
        }
        if (mpv.loaded) App.savePosition(mpv.position, mpv.duration)
        const f = App.prepareOpen(target)
        if (!f) return
        stopCompare()
        selectStart = -1
        const resume = at >= 0 ? at : (App.setting("resume", true) ? App.resumePosition(f) : 0)
        mpv.open(f, resume)
        if (resume > 0 && at < 0) toast("Resumed at " + App.formatClock(resume) + ". Press Home to start over")
        Transcript.load(f)
        Tempo.load(f)
        Media.setQueue(App.queue)
    }
    function openNext(dir) {
        const f = App.nextInQueue(dir)
        if (f) openMedia(f); else toast(dir > 0 ? "That's the last clip in this folder" : "That's the first clip in this folder")
    }
    function openFileDialog() { fileDialog.open() }
    function openFolderDialog() { folderDialog.open() }
    function openSubtitleDialog() { subDialog.open() }
    function addMarker(kind) {
        if (!mpv.loaded) return
        kind = kind || 0
        App.markers.add(mpv.position, kind, "")
        hud("marker", Theme.kindName(kind) + " at " + fmt(mpv.position))
        if (!markerHintShown && panelsVisible && sideTab === 0) { markerHintShown = true; toast("Double-click a marker in the list to write a note") }
    }
    property bool markerHintShown: false
    // S: a select from In to Out when both are set, otherwise S at the start and S again at the end.
    property double selectStart: -1
    function toggleSelect() {
        if (!mpv.loaded) return
        if (selectStart < 0 && App.inPoint >= 0 && App.outPoint > App.inPoint) {
            const len = App.outPoint - App.inPoint
            mpv.setLoop(-1, -1)
            App.makeSelectFromInOut("")
            hud("select", "Select added  ·  " + App.formatClock(len))
            return
        }
        if (selectStart < 0) {
            selectStart = mpv.position
            hud("select", "Select starts at " + fmt(selectStart) + "  ·  S to end it")
            return
        }
        let a = selectStart, b = mpv.position
        selectStart = -1
        if (b < a) { const t = a; a = b; b = t }
        if (b - a < Math.max(0.04, mpv.fps > 0 ? 1 / mpv.fps : 0.04)) { hud("close", "Select cancelled"); return }
        App.markers.add(a, 3, "", b)
        hud("select", "Select added  ·  " + App.formatClock(b - a))
        if (!selectHintShown && panelsVisible && sideTab === 0) { selectHintShown = true; toast("Drag a select's ends on the timeline to adjust it. Ctrl+R sends selects to DaVinci Resolve") }
    }
    property bool selectHintShown: false
    function cancelSelect() { if (selectStart >= 0) { selectStart = -1; hud("close", "Select cancelled") } }
    function undo() {
        const what = App.undo()
        hud("undo", what ? "Undid " + what : "Nothing to undo")
    }
    function redo() {
        const what = App.redo()
        hud("redo", what ? "Redid " + what : "Nothing to redo")
    }
    function setIn() { if (mpv.loaded) { App.inPoint = mpv.position; syncLoop(); hud("in", "In " + fmt(App.inPoint)) } }
    function setOut() { if (mpv.loaded) { App.outPoint = mpv.position; syncLoop(); hud("out", "Out " + fmt(App.outPoint)) } }
    function clearInOut() { App.clearInOut(); mpv.setLoop(-1, -1); hud("close", "In and Out cleared") }
    function syncLoop() { if (mpv.loopA >= 0) { if (App.inPoint >= 0 && App.outPoint > App.inPoint) mpv.setLoop(App.inPoint, App.outPoint); else mpv.setLoop(-1, -1) } }
    function toggleLoop() {
        if (mpv.loopA >= 0) { mpv.setLoop(-1, -1); hud("loop", "Loop off"); return }
        if (App.inPoint >= 0 && App.outPoint > App.inPoint) { mpv.setLoop(App.inPoint, App.outPoint); hud("loop", "Looping In to Out") }
        else toast("Set In (I) and Out (O) first")
    }
    function cycleSpeed(dir) {
        let i = speeds.findIndex(s => Math.abs(s - mpv.speed) < 0.001)
        if (i < 0) i = speeds.indexOf(1)
        i = Math.max(0, Math.min(speeds.length - 1, i + dir))
        mpv.speed = speeds[i]
        hud("speed", "Speed " + speeds[i] + "×")
    }
    function resetSpeed() { mpv.speed = 1; hud("speed", "Speed 1×") }
    function shuttle(key) {
        // J / K / L like editing apps. Repeated L plays faster; J rewinds in steps.
        if (key === "k") { rewindRate = 0; mpv.paused = true; mpv.speed = 1; return }
        if (key === "l") {
            rewindRate = 0
            if (mpv.paused) { mpv.speed = 1; mpv.paused = false }
            else mpv.speed = Math.min(8, mpv.speed >= 1 ? mpv.speed * 2 : 1)
            if (mpv.speed !== 1) hud("speed", "Playing " + mpv.speed + "×")
            return
        }
        mpv.paused = true
        rewindRate = rewindRate === 0 ? 1 : Math.min(8, rewindRate * 2)
        hud("speed", "Rewinding " + rewindRate + "×  ·  K stops")
    }
    function grabFrame() {
        if (!mpv.loaded || !mpv.hasVideo) { toast("No video frame to save"); return }
        const p = App.frameGrabPath(mpv.frame)
        if (mpv.screenshot(p)) toast("Saved frame " + mpv.frame, p); else toast("Couldn't save the frame")
    }
    // Esc steps back out of whatever is layered on: full screen, comparison, zoom, crop guide.
    readonly property bool escapable: isFull || compareFile !== "" || zoomScale > 1.001 || guideOn || selectStart >= 0
    function stepBack() {
        if (selectStart >= 0) cancelSelect()
        else if (isFull) toggleFullscreen()
        else if (compareFile !== "") stopCompare()
        else if (zoomScale > 1.001) setZoom(1, 0, 0)
        else if (guideOn) toggleGuide()
    }
    function toggleFullscreen() {
        if (isFull) visibility = wasMaximized ? Window.Maximized : Window.Windowed
        else { wasMaximized = visibility === Window.Maximized; visibility = Window.FullScreen }
    }
    function togglePanels() { panelsVisible = !panelsVisible; App.setSetting("panelsVisible", panelsVisible) }
    // ---- music -------------------------------------------------------------------------
    property bool beatGridOn: App.setting("beatGrid", false)
    onBeatGridOnChanged: App.setSetting("beatGrid", beatGridOn)
    function toggleBeatGrid() {
        if (Tempo.bpm <= 0) { showTab(5); toast("Find or tap the tempo first, in Audio and music"); return }
        beatGridOn = !beatGridOn
        hud("metronome", beatGridOn ? "Beat grid on. Markers and edges snap to beats" : "Beat grid off")
    }
    property var taps: []
    // Tap along with the beat (B). Four taps set the tempo; it keeps refining while you tap.
    function tapTempo() {
        if (!mpv.loaded) return
        const now = Date.now()
        let t = taps.length && now - taps[taps.length - 1].at > 2000 ? [] : taps.slice()
        t.push({ at: now, pos: mpv.position })
        if (t.length > 12) t = t.slice(t.length - 12)
        taps = t
        if (t.length < 4) { hud("metronome", "Tap " + (4 - t.length) + " more"); return }
        const bpm = 60000 * (t.length - 1) / (t[t.length - 1].at - t[0].at)
        Tempo.setBpm(Math.round(bpm * 10) / 10, "tapped")
        if (!mpv.paused) Tempo.setFirstBeat(t[t.length - 1].pos - 0.03)  // taps land a touch late
        hud("metronome", Tempo.bpm.toFixed(1).replace(/\.0$/, "") + " BPM")
    }
    // Markers on every beat / bar / 2 bars / 4 bars, optionally only between In and Out.
    function addBeatMarkers(every, inOutOnly) {
        if (Tempo.bpm <= 0 || !mpv.loaded) return
        const step = 60 / Tempo.bpm * every
        const a = inOutOnly && App.inPoint >= 0 ? App.inPoint : 0
        const b = inOutOnly && App.outPoint > a ? App.outPoint : mpv.duration
        const beatsInStep = every
        let k = Math.ceil((a - Tempo.firstBeat) / step - 1e-6)
        const list = []
        for (let t = Tempo.firstBeat + k * step; t < b - 1e-6 && list.length < 1000; t += step, ++k) {
            if (t < a - 1e-6) continue
            const bars = Math.round((t - Tempo.firstBeat) / (60 / Tempo.bpm)) / Tempo.beatsPerBar
            const label = beatsInStep >= Tempo.beatsPerBar ? "Bar " + (Math.floor(bars) + 1) : "Beat " + (Math.round((t - Tempo.firstBeat) / (60 / Tempo.bpm)) + 1)
            list.push({ time: t, kind: 0, note: label })
        }
        if (!list.length) { toast("No beats in that range"); return }
        App.markers.addMany(list, "Beat markers")
        toast("Added " + list.length + " beat markers. Ctrl+Z removes them")
    }

    signal transcriptFocus()
    property bool pendingTranscriptFocus: false
    function showTab(i) { sideTab = i; if (!panelsVisible) togglePanels() }
    function jumpMarker(dir) {
        const t = dir > 0 ? App.markers.nextAfter(mpv.position + 0.001) : App.markers.prevBefore(mpv.position - 0.001)
        if (t >= 0) { mpv.paused = true; mpv.seekTo(t, true) } else toast(dir > 0 ? "No more markers after this" : "No markers before this")
    }
    function rate(n) {
        if (!App.currentFile) return
        App.setRating(App.currentFile, n)
        hud(n > 0 ? "star-filled" : "star", n > 0 ? "★".repeat(n) + "☆".repeat(5 - n) : "Rating cleared")
    }
    function toggleSkipSilence() {
        skipSilence = !skipSilence
        silenceSaved = 0
        toast(skipSilence ? (Media.waveformReady ? "Skipping silences" : "Skipping silences once the audio has been read") : "Playing silences again")
    }
    function startCompare(f) {
        if (!mpv.loaded || !f) return
        compareFile = f
        compareOffset = 0
        toast("Comparing with " + f.split(/[\\/]/).pop() + ". Esc ends the comparison")
    }
    function stopCompare() { compareFile = "" }
    function seekRelative(s) {
        if (!mpv.loaded) return
        mpv.seekBy(s, Math.abs(s) < 2)
        hud(s > 0 ? "frame-fwd" : "frame-back", (s > 0 ? "+" : "−") + Math.abs(s) + " s")
    }
    function mpvItem() { return mpv }
    // Takes keyboard focus away from any text field, so single-key shortcuts work again.
    function releaseKeyboard() { keySink.forceActiveFocus() }
    Item { id: keySink }
    // Names the lazily built panels and dialogs use (their own "mpv" and "win" properties would shadow the ids).
    property alias player: mpv
    readonly property var mainWindow: win
    // Dialogs are created the first time they're needed, so Slate starts faster.
    readonly property bool paletteOpen: paletteLoader.item ? paletteLoader.item.opened : false
    readonly property bool settingsOpen: settingsLoader.item ? settingsLoader.item.opened : false
    readonly property bool downloadsOpen: downloadLoader.item ? downloadLoader.item.opened : false
    readonly property bool urlOpen: urlLoader.item ? urlLoader.item.opened : false
    readonly property bool resolveOpen: resolveLoader.item ? resolveLoader.item.opened : false
    readonly property bool thumbsOpen: thumbLoader.item ? thumbLoader.item.opened : false
    readonly property bool dialogOpen: paletteOpen || settingsOpen || downloadsOpen || urlOpen || resolveOpen || thumbsOpen
    function openPalette() { paletteLoader.active = true; if (!paletteLoader.item.opened) paletteLoader.item.open() }
    function closePalette() { if (paletteLoader.item) paletteLoader.item.close() }
    function openSettings(page) { settingsLoader.active = true; settingsLoader.item.openAt(page) }
    function closeSettings() { if (settingsLoader.item) settingsLoader.item.close() }
    function openDownloads(url) {
        downloadLoader.active = true
        if (url) downloadLoader.item.presetUrl = url
        if (!downloadLoader.item.opened) downloadLoader.item.open()
    }
    function closeDownloads() { if (downloadLoader.item) downloadLoader.item.close() }
    function openUrlDialog() { urlLoader.active = true; urlLoader.item.open() }
    function openResolve() {
        if (!App.currentFile) { toast("Open a clip or a folder first"); return }
        resolveLoader.active = true
        if (!resolveLoader.item.opened) resolveLoader.item.open()
    }
    function closeResolve() { if (resolveLoader.item) resolveLoader.item.close() }
    function openThumbnails() {
        if (!App.currentFile || !mpv.hasVideo) { toast("Open a video first"); return }
        thumbLoader.active = true
        if (!thumbLoader.item.opened) thumbLoader.item.open()
    }
    function closeThumbnails() { if (thumbLoader.item) thumbLoader.item.close() }
    // The frame on screen as the file's Explorer thumbnail (asks first, the first time).
    function explorerThumbHere() {
        if (!mpv.loaded || !mpv.hasVideo) return
        if (!Thumbnails.canEmbed(App.currentFile)) { toast("Explorer thumbnails can be set on MP4, MOV, M4V and MKV files"); return }
        if (App.setting("thumb/noConfirm", false)) { setExplorerThumbnail(App.currentFile, mpv.position); return }
        thumbLoader.active = true
        thumbLoader.item.pendingTime = mpv.position
        thumbLoader.item.confirming = true
        if (!thumbLoader.item.opened) thumbLoader.item.open()
    }
    function setExplorerThumbnail(file, t) { Thumbnails.embed(file, t) }
    function removeExplorerThumbnail(file) { Thumbnails.removeEmbedded(file) }
    // The new copy is ready: let go of the file if it's playing, swap it in, then carry on where we were.
    property var swapState: null
    Timer {
        id: swapTimer
        interval: 250
        repeat: true
        onTriggered: {
            const s = win.swapState
            s.tries++
            const last = s.tries >= 12
            if (!Thumbnails.commitSwap(s.file, last) && !last) return
            stop()
            win.swapState = null
            if (s.reopen) { mpv.open(s.file, s.pos); if (s.paused) pauseAfterOpen = true }
        }
    }
    property bool pauseAfterOpen: false
    Connections { target: mpv; function onLoadedChanged() { if (mpv.loaded && win.pauseAfterOpen) { win.pauseAfterOpen = false; mpv.paused = true } } }
    function beginGoTo() { if (!mpv.loaded) return; if (isFull) mouseRecently = true; transport.goTo.begin() }

    // ----- keyboard -------------------------------------------------------------
    // Every action, in the order the shortcut list and command palette show them.
    readonly property var actionList: [
        { id: "open", category: "File", name: "Open file…", keys: ["Ctrl+O"], run: openFileDialog },
        { id: "openFolder", category: "File", name: "Open folder…", keys: ["Ctrl+Shift+O"], run: openFolderDialog },
        { id: "openUrl", category: "File", name: "Open URL…", keys: ["Ctrl+U"], run: openUrlDialog },
        { id: "download", category: "File", name: "Download from YouTube…", keys: ["Ctrl+D"], run: () => openDownloads() },
        { id: "loadSubs", category: "File", name: "Load subtitle file…", keys: [], run: openSubtitleDialog },
        { id: "nextFile", category: "File", name: "Next clip in folder", keys: ["PgDown"], run: () => openNext(1) },
        { id: "prevFile", category: "File", name: "Previous clip in folder", keys: ["PgUp"], run: () => openNext(-1) },

        { id: "play", category: "Playback", name: "Play / pause", keys: ["Space"], run: () => { rewindRate = 0; mpv.togglePause() } },
        { id: "frameNext", category: "Playback", name: "Next frame", keys: ["."], run: () => mpv.stepFrame(1) },
        { id: "framePrev", category: "Playback", name: "Previous frame", keys: [","], run: () => mpv.stepFrame(-1) },
        { id: "fwd5", category: "Playback", name: "Forward 5 seconds", keys: ["Right"], run: () => seekRelative(5) },
        { id: "back5", category: "Playback", name: "Back 5 seconds", keys: ["Left"], run: () => seekRelative(-5) },
        { id: "fwd1", category: "Playback", name: "Forward 1 second", keys: ["Shift+Right"], run: () => seekRelative(1) },
        { id: "back1", category: "Playback", name: "Back 1 second", keys: ["Shift+Left"], run: () => seekRelative(-1) },
        { id: "start", category: "Playback", name: "Go to start", keys: ["Home"], run: () => mpv.seekTo(0) },
        { id: "end", category: "Playback", name: "Go to end", keys: ["End"], run: () => mpv.seekTo(mpv.duration) },
        { id: "goTo", category: "Playback", name: "Go to a time or frame…", keys: ["Ctrl+G"], run: beginGoTo },
        { id: "shuttleJ", category: "Playback", name: "Rewind (J)", keys: ["J"], run: () => shuttle("j") },
        { id: "shuttleK", category: "Playback", name: "Stop (K)", keys: ["K"], run: () => shuttle("k") },
        { id: "shuttleL", category: "Playback", name: "Play, faster each press (L)", keys: ["L"], run: () => shuttle("l") },
        { id: "faster", category: "Playback", name: "Faster", keys: ["]"], run: () => cycleSpeed(1) },
        { id: "slower", category: "Playback", name: "Slower", keys: ["["], run: () => cycleSpeed(-1) },
        { id: "normalSpeed", category: "Playback", name: "Normal speed", keys: ["="], run: resetSpeed },
        { id: "skipSilence", category: "Playback", name: "Skip silences", keys: ["Ctrl+Shift+K"], run: toggleSkipSilence },
        { id: "volUp", category: "Playback", name: "Volume up", keys: ["Up"], run: () => setVolume(mpv.volume + 5) },
        { id: "volDown", category: "Playback", name: "Volume down", keys: ["Down"], run: () => setVolume(mpv.volume - 5) },
        { id: "mute", category: "Playback", name: "Mute", keys: ["Ctrl+M"], run: toggleMute },

        { id: "marker", category: "Review", name: "Add marker (note)", keys: ["M"], run: () => addMarker(0) },
        { id: "markerGood", category: "Review", name: "Mark a good take", keys: ["Shift+M"], run: () => addMarker(1) },
        { id: "markerBad", category: "Review", name: "Mark a mistake", keys: ["Alt+M"], run: () => addMarker(2) },
        { id: "select", category: "Review", name: "Start or end a select (or turn In to Out into one)", keys: ["S"], run: toggleSelect },
        { id: "undo", category: "Review", name: "Undo", keys: ["Ctrl+Z"], run: undo },
        { id: "redo", category: "Review", name: "Redo", keys: ["Ctrl+Shift+Z", "Ctrl+Y"], run: redo },
        { id: "nextMarker", category: "Review", name: "Next marker", keys: ["Ctrl+Right"], run: () => jumpMarker(1) },
        { id: "prevMarker", category: "Review", name: "Previous marker", keys: ["Ctrl+Left"], run: () => jumpMarker(-1) },
        { id: "in", category: "Review", name: "Set In point", keys: ["I"], run: setIn },
        { id: "out", category: "Review", name: "Set Out point", keys: ["O"], run: setOut },
        { id: "clearInOut", category: "Review", name: "Clear In and Out", keys: ["X"], run: clearInOut },
        { id: "loop", category: "Review", name: "Loop In to Out", keys: ["Ctrl+L"], run: toggleLoop },
        { id: "rate1", category: "Review", name: "Rate 1 star", keys: ["1"], run: () => rate(1) },
        { id: "rate2", category: "Review", name: "Rate 2 stars", keys: ["2"], run: () => rate(2) },
        { id: "rate3", category: "Review", name: "Rate 3 stars", keys: ["3"], run: () => rate(3) },
        { id: "rate4", category: "Review", name: "Rate 4 stars", keys: ["4"], run: () => rate(4) },
        { id: "rate5", category: "Review", name: "Rate 5 stars", keys: ["5"], run: () => rate(5) },
        { id: "rate0", category: "Review", name: "Clear rating", keys: ["0"], run: () => rate(0) },
        { id: "zoomIn", category: "Review", name: "Zoom timeline in", keys: ["Ctrl+="], run: () => timeline.zoomBy(0.6) },
        { id: "zoomOut", category: "Review", name: "Zoom timeline out", keys: ["Ctrl+-"], run: () => timeline.zoomBy(1.6) },
        { id: "zoomReset", category: "Review", name: "Show whole timeline", keys: ["Ctrl+0"], run: () => timeline.resetZoom() },

        { id: "exportClip", category: "Export", name: "Export In to Out (lossless)", keys: ["Ctrl+E"], run: () => App.exportClip(true) },
        { id: "exportExact", category: "Export", name: "Export In to Out (frame-exact)", keys: ["Ctrl+Shift+E"], run: () => App.exportClip(false) },
        { id: "edl", category: "Export", name: "Export markers for DaVinci Resolve (EDL)", keys: [], run: () => App.exportMarkersEdl(mpv.fps) },
        { id: "resolve", category: "Export", name: "Send to DaVinci Resolve (timeline)…", keys: ["Ctrl+R"], run: openResolve },
        { id: "exportSelects", category: "Export", name: "Export every select as its own clip", keys: [], run: () => { if (App.markers.selectCount) App.exportRanges(App.markers.selects(), true); else toast("No selects yet. Press S at the start and end of a part to keep") } },
        { id: "csv", category: "Export", name: "Export markers as CSV", keys: [], run: () => App.exportMarkersCsv(mpv.fps) },
        { id: "chapters", category: "Export", name: "Copy markers as YouTube chapters", keys: [], run: () => App.copyYouTubeChapters(mpv.duration) },
        { id: "tapTempo", category: "Review", name: "Tap tempo", keys: ["B"], run: tapTempo },
        { id: "beatGrid", category: "View", name: "Show beat grid (snap to beats)", keys: ["Shift+B"], run: toggleBeatGrid },
        { id: "findTempo", category: "Review", name: "Find the tempo (BPM)", keys: [], run: () => { if (App.currentFile) { showTab(5); Tempo.detect(App.currentFile) } } },
        { id: "thumbnails", category: "Export", name: "Find thumbnail frames…", keys: ["Ctrl+T"], run: openThumbnails },
        { id: "explorerThumb", category: "Export", name: "Make this frame the file's Explorer thumbnail", keys: [], run: explorerThumbHere },
        { id: "frameGrab", category: "Export", name: "Save current frame as PNG", keys: ["Ctrl+Shift+S"], run: grabFrame },
        { id: "transcribe", category: "Export", name: "Transcribe this clip", keys: [], run: () => { showTab(3); Transcript.start(App.currentFile) } },
        { id: "transcribeAll", category: "Export", name: "Transcribe every clip in this folder", keys: [], run: () => { showTab(3); const n = Transcript.transcribeAll(App.queue); toast(n ? "Transcribing " + n + " clips in the background" : "Every clip already has a transcript") } },
        { id: "findSpoken", category: "Review", name: "Search what's said (transcripts)", keys: ["Ctrl+F"], run: () => { pendingTranscriptFocus = true; showTab(3); transcriptFocus() } },

        { id: "panels", category: "View", name: "Show or hide side panel", keys: ["H"], run: togglePanels },
        { id: "fullscreen", category: "View", name: "Full screen", keys: ["F", "F11"], run: toggleFullscreen },
        { id: "copyFrame", category: "Export", name: "Copy current frame to clipboard", keys: ["Ctrl+C"], run: () => { if (mpv.hasVideo) toast(mpv.copyFrame() ? "Frame copied. Paste it anywhere" : "Couldn't copy this frame") } },
        { id: "zoom", category: "View", name: "Zoom picture to 200% / back to fit", keys: ["Z"], run: toggleZoom },
        { id: "cropGuide", category: "View", name: "Show or hide crop guide", keys: ["G"], run: toggleGuide },
        { id: "guide916", category: "View", name: "Crop guide: 9:16 (Shorts)", keys: [], run: () => setGuideRatio(1) },
        { id: "guide45", category: "View", name: "Crop guide: 4:5", keys: [], run: () => setGuideRatio(2) },
        { id: "guide11", category: "View", name: "Crop guide: 1:1", keys: [], run: () => setGuideRatio(3) },
        { id: "tabMarkers", category: "View", name: "Show markers", keys: ["Alt+1"], run: () => showTab(0) },
        { id: "tabClips", category: "View", name: "Show clips in folder", keys: ["Alt+2"], run: () => showTab(1) },
        { id: "tabInfo", category: "View", name: "Show media info", keys: ["Alt+3"], run: () => showTab(2) },
        { id: "tabTranscript", category: "View", name: "Show transcript", keys: ["Alt+4"], run: () => showTab(3) },
        { id: "tabScopes", category: "View", name: "Show video scopes", keys: ["Alt+5"], run: () => showTab(4) },
        { id: "tabAudio", category: "View", name: "Show audio levels", keys: ["Alt+6"], run: () => showTab(5) },
        { id: "palette", category: "View", name: "Search actions", keys: ["Ctrl+K"], run: openPalette },
        { id: "shortcuts", category: "View", name: "Keyboard shortcuts", keys: ["?"], run: () => openSettings(4) },
        { id: "settings", category: "View", name: "Settings", keys: ["Ctrl+,"], run: () => openSettings(0) }
    ]
    function keysFor(a) {
        keymapRevision
        const o = App.setting("keys/" + a.id, "")
        if (o === "-") return []
        return o ? o.split("|") : a.keys
    }
    function keysLabel(a) { return keysFor(a).join("  ") }

    Instantiator {
        model: win.actionList
        delegate: Shortcut {
            required property var modelData
            sequences: win.keysFor(modelData)
            context: Qt.WindowShortcut
            enabled: !win.dialogOpen
            autoRepeat: ["frameNext", "framePrev", "fwd5", "back5", "fwd1", "back1", "volUp", "volDown"].indexOf(modelData.id) >= 0
            onActivated: modelData.run()
        }
    }
    // keyboard media keys
    Shortcut { sequences: ["Toggle Media Play/Pause", "Media Play", "Media Pause"]; context: Qt.WindowShortcut; enabled: mpv.loaded; onActivated: mpv.togglePause() }
    Shortcut { sequence: "Media Stop"; context: Qt.WindowShortcut; enabled: mpv.loaded; onActivated: mpv.paused = true }
    Shortcut { sequence: "Media Next"; context: Qt.WindowShortcut; onActivated: win.openNext(1) }
    Shortcut { sequence: "Media Previous"; context: Qt.WindowShortcut; onActivated: win.openNext(-1) }
    Shortcut {
        sequence: "Escape"
        enabled: !win.dialogOpen && win.escapable && !transport.goTo.editing
        onActivated: win.stepBack()
    }

    Timer { // J rewind
        interval: 100; repeat: true; running: win.rewindRate > 0
        onTriggered: { if (mpv.position <= 0.05) { win.rewindRate = 0; return } mpv.seekBy(-0.1 * win.rewindRate * 2, false) }
    }
    Timer { // skip silences
        interval: 80; repeat: true
        running: win.skipSilence && mpv.loaded && !mpv.paused
        onTriggered: {
            const s = Media.silenceAt(mpv.position + 0.05)
            if (s.length === 2 && s[1] - mpv.position > 0.3) { win.silenceSaved += s[1] - mpv.position; mpv.seekTo(s[1], false) }
        }
    }
    Timer { id: idleTimer; interval: 2200; onTriggered: win.mouseRecently = false }
    Timer { // keep the comparison clip in step
        interval: 200; repeat: true; running: win.compareFile !== "" && compareLoader.item !== null
        onTriggered: {
            const b = compareLoader.item
            if (!b || !b.loaded) return
            if (b.paused !== mpv.paused) b.paused = mpv.paused
            if (Math.abs(b.speed - mpv.speed) > 0.001) b.speed = mpv.speed
            const want = Math.max(0, mpv.position + win.compareOffset)
            const drift = Math.abs(b.position - want)
            if ((mpv.paused && drift > 0.001) || (!mpv.paused && drift > 0.12)) b.seekTo(Math.min(want, b.duration), true)
        }
    }

    // ----- engine events ------------------------------------------------------------
    Connections {
        target: mpv
        function onErrorOccurred(message) { win.toast(message) }
        function onEndOfFile() {
            win.rewindRate = 0
            if (App.setting("autoAdvance", false) && App.nextInQueue(1)) Qt.callLater(() => win.openNext(1))
        }
        function onFileLoaded() {
            Media.analyse(App.currentFile, mpv.duration, mpv.hasVideo)
            // music files: find the tempo straight away (a couple of seconds, in the background)
            if (!mpv.hasVideo && Tempo.bpm <= 0 && Tempo.file === App.currentFile && App.setting("autoTempo", true)) Tempo.detect(App.currentFile)
        }
        function onDurationChanged() { if (mpv.loaded) Media.analyse(App.currentFile, mpv.duration, mpv.hasVideo) }
        function onVideoInfoChanged() { if (mpv.loaded) Media.analyse(App.currentFile, mpv.duration, mpv.hasVideo) }
        function onVolumeChanged() { volumeSave.restart() }
    }
    Timer { id: volumeSave; interval: 800; onTriggered: if (App.setting("rememberVolume", true)) App.setSetting("volume", mpv.volume) }
    Connections {
        target: App
        function onMessage(text, revealPath) { win.toast(text, revealPath) }
        function onOpenRequested(file) {
            if (win.visibility === Window.Minimized) win.showNormal()
            win.raise(); win.requestActivate()
            if (file) win.openMedia(file)
        }
    }
    Connections {
        target: Downloader
        function onFinished(ok, file, message) {
            if (!ok) { if (!win.downloadsOpen) win.toast(message); return }
            if (App.setting("dl/openWhenDone", true)) { win.closeDownloads(); win.openMedia(file) }
            win.toast(message, file)
        }
    }
    Connections {
        target: Transcript
        function onFinished(ok, message) { win.toast(message) }
    }
    Connections {
        target: ResolveExport
        function onFinished(ok, message, path) { win.toast(message, ok ? path : "") }
    }
    Connections {
        target: Tempo
        function onDetected(ok, message) { if (Tempo.file === App.currentFile) { if (ok) hud("metronome", message); else toast(message) } }
    }
    Connections {
        target: Thumbnails
        function onFinished(ok, message, path) { if (message) win.toast(message, ok && path && !path.endsWith(".mp4") ? path : "") }
        function onSwapReady(file) {
            const playing = file === App.currentFile && mpv.loaded
            win.swapState = { file: file, tries: 0, reopen: playing, pos: mpv.position, paused: mpv.paused }
            if (playing) mpv.command(["stop"])
            swapTimer.start()
        }
    }

    Component.onCompleted: {
        const g = App.setting("window", "")
        if (g) {
            const p = g.split(",").map(Number)
            if (p.length >= 5 && p[2] >= minimumWidth && p[3] >= minimumHeight) {
                x = p[0]; y = p[1]; width = p[2]; height = p[3]
                if (p[4] === 1) Qt.callLater(() => win.showMaximized())
            }
        }
        if (App.setting("rememberVolume", true)) mpv.volume = App.setting("volume", 100)
        mpv.setMpvProperty("hwdec", App.setting("hwdec", true) ? "auto-safe" : "no")
        if (App.setting("lightRendering", false)) mpv.setLightRendering(true)
        Transcript.model = App.setting("whisperModel", "base.en")
    }
    // Look for a newer build a few seconds after start, and fetch it in the background.
    Timer {
        interval: 4000; running: Updater.supported && App.setting("autoUpdate", true); repeat: false
        onTriggered: Updater.check(true)
    }
    Connections {
        target: Updater
        function onChanged() {
            if (Updater.supported && Updater.state === "available" && App.setting("autoUpdate", true)) Updater.download()
        }
    }
    onClosing: {
        if (mpv.loaded) App.savePosition(mpv.position, mpv.duration)
        const maxed = visibility === Window.Maximized || (isFull && wasMaximized)
        const old = String(App.setting("window", "")).split(",")
        const geom = (!isFull && visibility !== Window.Maximized) || old.length < 5 ? [x, y, width, height] : old.slice(0, 4)
        App.setSetting("window", geom.concat([maxed ? 1 : 0]).join(","))
    }

    FileDialog {
        id: fileDialog
        title: "Open media"
        nameFilters: ["Media files (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.mts *.m2ts *.ts *.wmv *.flv *.mxf *.mpg *.mpeg *.3gp *.mp3 *.wav *.flac *.m4a *.aac *.ogg *.opus *.aiff *.wma)", "All files (*)"]
        onAccepted: win.openMedia(selectedFile)
    }
    FolderDialog { id: folderDialog; title: "Open folder"; onAccepted: win.openMedia(selectedFolder) }
    FileDialog {
        id: subDialog
        title: "Load subtitles"
        nameFilters: ["Subtitles (*.srt *.ass *.ssa *.vtt *.sub)", "All files (*)"]
        onAccepted: { mpv.addSubtitle(App.localPath(selectedFile)); win.toast("Subtitles loaded") }
    }

    // ----- layout -------------------------------------------------------------------
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            visible: !win.isFull
            color: Theme.chrome
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14; anchors.rightMargin: 8
                spacing: 8
                Image { source: "qrc:/Slate/assets/slate-small-64.png"; sourceSize: Qt.size(20, 20); Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
                // one line: the folder, dimmed, then the clip; the full path is in the tooltip
                Item {
                    id: titleBox
                    // natural width, shrinking (folder first) only when the header runs out of room
                    Layout.fillWidth: true
                    Layout.minimumWidth: 80
                    Layout.preferredWidth: Math.min(620, titleText.implicitWidth + (pathText.visible ? pathText.implicitWidth + 18 : 0) + 1)
                    Layout.maximumWidth: Layout.preferredWidth
                    Layout.preferredHeight: 22
                    readonly property string folder: {
                        const f = App.currentFile.substring(0, App.currentFile.length - App.currentName.length).replace(/[\\/]$/, "")
                        return f.split(/[\\/]/).pop()
                    }
                    Row {
                        id: titleRow
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        spacing: 6
                        Text {
                            id: pathText
                            visible: App.currentFile !== "" && titleBox.folder !== ""
                            width: Math.max(0, Math.min(implicitWidth, titleBox.width - titleText.implicitWidth - 18, titleBox.width * 0.5))
                            text: titleBox.folder
                            color: Theme.ink3; font.pixelSize: Theme.fontBody
                            elide: Text.ElideMiddle
                        }
                        Text { visible: pathText.visible; text: "/"; color: Theme.ink4; font.pixelSize: Theme.fontBody }
                        Text {
                            id: titleText
                            width: Math.min(implicitWidth, titleBox.width - (pathText.visible ? pathText.width + 18 : 0))
                            text: App.currentName || "Slate"
                            color: Theme.ink; font.pixelSize: Theme.fontBody; font.weight: Font.Medium
                            elide: Text.ElideMiddle
                        }
                    }
                    HoverHandler { id: titleHover }
                    Tip { visible: titleHover.hovered && App.currentFile !== ""; text: App.currentFile }
                }
                // the current clip's rating: click a star, or press 1 to 5
                Row {
                    visible: App.currentFile !== "" && App.queue.length > 0
                    Layout.leftMargin: 6
                    spacing: 0
                    readonly property int stars: { App.ratingsRevision; return App.rating(App.currentFile) }
                    Repeater {
                        model: 5
                        delegate: Item {
                            id: hs
                            required property int index
                            width: 18; height: 22
                            readonly property bool on: index < parent.stars
                            Icon {
                                anchors.centerIn: parent
                                name: hs.on ? "star-filled" : "star"; size: 14
                                color: hs.on ? Theme.note : (starHov.hovered ? Theme.ink3 : Theme.ink4)
                            }
                            HoverHandler { id: starHov; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: win.rate(hs.parent.stars === hs.index + 1 ? 0 : hs.index + 1) }
                            Tip { visible: starHov.hovered; text: "Rate " + (hs.index + 1) + (hs.index ? " stars" : " star"); keys: String(hs.index + 1) }
                        }
                    }
                }
                Rectangle { visible: App.queue.length > 1; Layout.preferredWidth: 1; Layout.preferredHeight: 18; color: Theme.line }
                Row {
                    visible: App.queue.length > 1
                    spacing: 0
                    Btn { glyph: "prev"; iconSize: 15; implicitWidth: 28; tip: "Previous clip"; keys: "PgUp"; enabled: App.queueIndex > 0; onClicked: win.openNext(-1) }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 64; horizontalAlignment: Text.AlignHCenter
                        text: (App.queueIndex + 1) + " of " + App.queue.length
                        color: Theme.ink2; font.pixelSize: Theme.fontLabel
                    }
                    Btn { glyph: "next"; iconSize: 15; implicitWidth: 28; tip: "Next clip"; keys: "PgDown"; enabled: App.queueIndex < App.queue.length - 1; onClicked: win.openNext(1) }
                }
                Item { Layout.fillWidth: true; Layout.preferredWidth: 0; Layout.minimumWidth: 0 }

                // update chip: appears only when a newer build exists
                Btn {
                    id: updateChip
                    visible: ["available", "downloading", "ready"].indexOf(Updater.state) >= 0
                    primary: Updater.state === "ready"
                    active: Updater.state !== "ready"
                    glyph: Updater.state === "ready" ? "loop" : "download"
                    text: Updater.state === "ready" ? "Restart to update"
                          : (Updater.state === "downloading" ? "Downloading update " + Math.round(Updater.progress * 100) + "%" : "Update to " + Updater.latestVersion)
                    tip: Updater.state === "ready" ? "Slate closes, updates and reopens in a few seconds" : "A newer version of Slate is available"
                    enabled: Updater.state !== "downloading"
                    onClicked: Updater.state === "ready" ? Updater.installAndRestart() : Updater.download()
                }

                Rectangle {
                    Layout.preferredWidth: 200
                    Layout.preferredHeight: 28
                    radius: Theme.radius
                    color: sbMa.containsMouse ? Theme.hover : Theme.raise
                    Behavior on color { ColorAnimation { duration: Theme.fast } }
                    Icon { x: 9; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 14; color: Theme.ink3 }
                    Text { x: 30; anchors.verticalCenter: parent.verticalCenter; text: "Search actions"; color: Theme.ink3; font.pixelSize: Theme.fontLabel }
                    KeyCap { anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter; text: "Ctrl K" }
                    MouseArea { id: sbMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: win.openPalette() }
                }
                // download chip: shows progress while a video downloads in the background
                Btn {
                    visible: Downloader.busy
                    active: true
                    glyph: "download"
                    text: (Downloader.state === "converting" ? "Converting " : "Downloading ") + Math.round(Downloader.progress * 100) + "%"
                    tip: Downloader.status
                    onClicked: win.openDownloads()
                }
                Btn { glyph: "open"; tip: "Open file"; keys: "Ctrl+O"; onClicked: fileDialog.open() }
                Btn { visible: !Downloader.busy; glyph: "download"; tip: "Download from YouTube"; keys: "Ctrl+D"; onClicked: win.openDownloads() }
                Btn { glyph: "sidebar"; tip: win.panelsVisible ? "Hide side panel" : "Show side panel"; keys: "H"; active: win.panelsVisible && App.currentFile !== ""; onClicked: win.togglePanels() }
                Btn { glyph: "settings"; tip: "Settings"; keys: "Ctrl+,"; onClicked: win.openSettings(0) }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- picture ----
            Rectangle {
                id: videoArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: mpv.loaded && mpv.hasVideo ? "#000000" : Theme.bg
                clip: true

                Row {
                    anchors.fill: parent
                    MpvObject {
                        id: mpv
                        width: win.compareFile ? parent.width / 2 : parent.width
                        height: parent.height
                    }
                    Loader {
                        id: compareLoader
                        active: win.compareFile !== ""
                        width: active ? parent.width / 2 : 0
                        height: parent.height
                        sourceComponent: MpvObject {
                            muted: true
                            Component.onCompleted: open(win.compareFile, Math.max(0, mpv.position))
                        }
                    }
                }
                Row {
                    visible: win.compareFile !== ""
                    anchors.fill: parent
                    Repeater {
                        model: 2
                        delegate: Item {
                            required property int index
                            width: parent.width / 2; height: parent.height
                            Rectangle {
                                x: 12; y: 12
                                width: cl.implicitWidth + 16; height: 24; radius: 4
                                color: Qt.rgba(0, 0, 0, 0.6)
                                Text {
                                    id: cl; anchors.centerIn: parent
                                    text: (index === 0 ? "A   " + App.currentName : "B   " + win.compareFile.split(/[\\/]/).pop())
                                    color: "white"; font.pixelSize: 12
                                }
                            }
                        }
                    }
                }

                AudioView { anchors.fill: parent; mpv: mpv; visible: mpv.loaded && !mpv.hasVideo }
                EmptyState { anchors.fill: parent; win: win; visible: !mpv.loaded && App.currentFile === "" }
                Column {
                    anchors.centerIn: parent
                    visible: !mpv.loaded && App.currentFile !== ""
                    spacing: 10
                    BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: parent.visible; implicitWidth: 36; implicitHeight: 36 }
                    Text { text: "Opening " + App.currentName; color: Theme.ink3; font.pixelSize: 12 }
                }

                Row {
                    x: 12; y: 12
                    spacing: 6
                    visible: mpv.loaded && win.compareFile === "" && (win.chromeVisible || win.skipSilence)
                    component Chip: Rectangle {
                        property alias text: t.text
                        property color tone: "white"
                        width: t.implicitWidth + 14; height: 22; radius: 4
                        color: Qt.rgba(0, 0, 0, 0.55)
                        Text { id: t; anchors.centerIn: parent; color: parent.tone; font.pixelSize: 11 }
                    }
                    Chip { visible: win.selectStart >= 0; tone: Theme.select; text: "Select from " + win.fmt(win.selectStart) + "  ·  S ends it, Esc cancels" }
                    Chip { visible: mpv.loopA >= 0; text: "Looping In → Out" }
                    Chip { visible: Math.abs(mpv.speed - 1) > 0.001; text: mpv.speed + "×" }
                    Chip { visible: win.skipSilence; text: "Skipping silences" + (win.silenceSaved >= 1 ? " · " + App.formatClock(win.silenceSaved) + " saved" : "") }
                    Chip { visible: Media.probe.vfr === true; tone: Theme.warn; text: "Variable frame rate" }
                    Chip { visible: win.zoomScale > 1.001; text: "Zoom " + Math.round(win.zoomScale * 100) + "%  ·  Z to fit" }
                }

                MouseArea {
                    id: videoMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    enabled: mpv.loaded
                    property point pressAt
                    property real pressPanX: 0
                    property real pressPanY: 0
                    property bool dragged: false
                    onPressed: (m) => {
                        win.releaseKeyboard()
                        if (m.button === Qt.RightButton) { videoMenu.popup(); return }
                        pressAt = Qt.point(m.x, m.y); pressPanX = win.panX; pressPanY = win.panY; dragged = false
                    }
                    onPositionChanged: (m) => {
                        win.mouseRecently = true; idleTimer.restart()
                        if (pressed && (pressedButtons & Qt.LeftButton) && win.zoomScale > 1.001) {
                            if (Math.abs(m.x - pressAt.x) + Math.abs(m.y - pressAt.y) > 4) dragged = true
                            const r = win.pictureRect()
                            win.setZoom(win.zoomScale, pressPanX + (m.x - pressAt.x) / (r.width * win.zoomScale), pressPanY + (m.y - pressAt.y) / (r.height * win.zoomScale))
                        }
                    }
                    onClicked: (m) => { if (m.button === Qt.LeftButton && !dragged) mpv.togglePause() }
                    onDoubleClicked: (m) => { if (m.button === Qt.LeftButton) { mpv.togglePause(); win.toggleFullscreen() } }
                    onWheel: (w) => {
                        const up = (w.angleDelta.y || w.angleDelta.x) > 0
                        if ((w.modifiers & Qt.ControlModifier) && mpv.hasVideo) win.zoomAt(win.zoomScale * (up ? 1.25 : 0.8), w.x, w.y)
                        else win.setVolume(mpv.volume + (up ? 5 : -5))
                    }
                    cursorShape: win.isFull && !win.mouseRecently ? Qt.BlankCursor
                                 : (win.zoomScale > 1.001 ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.ArrowCursor)
                }
                SMenu {
                    id: videoMenu
                    implicitWidth: 270
                    SMenuItem { text: mpv.paused ? "Play" : "Pause"; glyph: mpv.paused ? "play" : "pause"; keys: "Space"; onTriggered: mpv.togglePause() }
                    SMenuItem { text: "Add marker"; glyph: "marker"; keys: "M"; onTriggered: win.addMarker(0) }
                    SMenuItem { text: "Mark a good take"; glyph: "check"; keys: "Shift+M"; onTriggered: win.addMarker(1) }
                    SMenuItem { text: "Set In here"; glyph: "in"; keys: "I"; onTriggered: win.setIn() }
                    SMenuItem { text: "Set Out here"; glyph: "out"; keys: "O"; onTriggered: win.setOut() }
                    SMenuItem {
                        text: App.inPoint >= 0 && App.outPoint > App.inPoint ? "Make a select from In to Out" : (win.selectStart >= 0 ? "End the select here" : "Start a select here")
                        glyph: "select"; keys: "S"; onTriggered: win.toggleSelect()
                    }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.line } }
                    SMenuItem { text: "Copy frame"; glyph: "copy-frame"; keys: "Ctrl+C"; enabled: mpv.hasVideo; onTriggered: win.toast(mpv.copyFrame() ? "Frame copied. Paste it anywhere" : "Couldn't copy this frame") }
                    SMenuItem { text: "Save frame as PNG"; glyph: "camera"; keys: "Ctrl+Shift+S"; enabled: mpv.hasVideo; onTriggered: win.grabFrame() }
                    SMenuItem { text: "Find thumbnail frames…"; glyph: "image"; keys: "Ctrl+T"; enabled: mpv.hasVideo; onTriggered: win.openThumbnails() }
                    SMenuItem { text: "Make this the Explorer thumbnail"; glyph: "image"; enabled: mpv.hasVideo && Thumbnails.canEmbed(App.currentFile) && !Thumbnails.busy; onTriggered: win.explorerThumbHere() }
                    SMenuItem { text: "Go to a time…"; glyph: "search"; keys: "Ctrl+G"; onTriggered: win.beginGoTo() }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.line } }
                    SMenuItem { text: win.zoomScale > 1.001 ? "Fit picture" : "Zoom to 200%"; glyph: "zoom"; keys: "Z"; enabled: mpv.hasVideo; onTriggered: win.toggleZoom() }
                    SMenuItem { text: "Crop guide"; glyph: "crop"; keys: "G"; checkable: true; checked: win.guideOn; enabled: mpv.hasVideo; onTriggered: win.toggleGuide() }
                    SMenuItem { text: win.isFull ? "Exit full screen" : "Full screen"; glyph: win.isFull ? "fullscreen-exit" : "fullscreen"; keys: "F"; onTriggered: win.toggleFullscreen() }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.line } }
                    SMenuItem { text: App.isWindows ? "Show in Explorer" : "Show in folder"; glyph: "open"; enabled: App.currentFile !== ""; onTriggered: App.revealInFolder(App.currentFile) }
                }
                CropGuide {
                    x: mpv.x; y: mpv.y; width: mpv.width; height: mpv.height
                    visible: mode > 0 && mpv.loaded && mpv.hasVideo && win.zoomScale < 1.001
                    mode: win.guideMode
                    videoWidth: mpv.info.width || 0
                    videoHeight: mpv.info.height || 0
                }

                Btn {
                    visible: win.compareFile !== ""
                    anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
                    flat: false; glyph: "close"; text: "End comparison"; keys: "Esc"
                    onClicked: win.stopCompare()
                }

                DropArea {
                    id: drop
                    anchors.fill: parent
                    onDropped: (d) => { if (d.hasUrls) win.openMedia(d.urls[0]) }
                    Rectangle {
                        anchors.fill: parent; anchors.margins: 14
                        visible: drop.containsDrag
                        color: Qt.rgba(0.31, 0.55, 1, 0.08); radius: Theme.radiusLg
                        border.color: Theme.accent; border.width: 2
                        Column {
                            anchors.centerIn: parent; spacing: 10
                            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "drop"; size: 34; color: Theme.accent }
                            Text { text: "Drop to open"; color: Theme.ink; font.pixelSize: 16 }
                        }
                    }
                }

                // full-screen controls float over the picture
                Item {
                    id: overlayControls
                    visible: win.isFull
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 200
                    opacity: win.chromeVisible ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 220 } }
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0; color: "transparent" }
                            GradientStop { position: 0.35; color: Qt.rgba(0, 0, 0, 0.6) }
                            GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.88) }
                        }
                    }
                    HoverHandler { onHoveredChanged: if (hovered) { win.mouseRecently = true; idleTimer.stop() } else idleTimer.restart() }
                }

                Hud {
                    id: hudItem
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: win.isFull ? 40 : 28
                    z: 20
                }
                // After a crash: what happened, with the report one click away.
                Rectangle {
                    id: crashCard
                    objectName: "crashCard"
                    visible: Logs.crashReport !== ""
                    z: 25
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 20
                    width: Math.min(520, parent.width - 40)
                    height: crashCol.implicitHeight + 28
                    radius: Theme.radiusLg
                    color: Theme.popup
                    border.color: Theme.lineStrong
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 14
                        spacing: 12
                        Icon { Layout.alignment: Qt.AlignTop; name: "bug"; size: 20; color: Theme.warn }
                        ColumnLayout {
                            id: crashCol
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: Logs.crashFile ? "Slate closed unexpectedly last time" : "Slate didn't close normally last time"
                                color: Theme.ink; font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold; wrapMode: Text.WordWrap
                            }
                            Text {
                                Layout.fillWidth: true
                                text: Logs.crashFile ? "A crash report was saved. Your markers and notes are kept as you go, so nothing should be lost." : "It may have frozen or been closed from Task Manager. Your markers and notes are kept as you go."
                                color: Theme.ink2; font.pixelSize: 12; wrapMode: Text.WordWrap
                            }
                            RowLayout {
                                spacing: 6
                                Layout.topMargin: 4
                                Btn { flat: false; glyph: "copy"; text: "Copy details"; onClicked: { Logs.copyDetails(); win.toast("Copied. Paste it into your bug report") } }
                                Btn { flat: false; glyph: "open"; text: "Show report"; onClicked: Logs.openFolder() }
                            }
                        }
                        Btn { Layout.alignment: Qt.AlignTop; glyph: "close"; tip: "Dismiss"; onClicked: Logs.dismissCrash() }
                    }
                }
                Toast {
                    id: toastItem
                    objectName: "toast"
                    maxWidth: Math.min(640, parent.width - 40)
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: win.isFull ? 160 : 20
                }
            }

            // ---- side panel ----
            Rectangle {
                id: side
                property real panelWidth: Math.max(300, Math.min(560, App.setting("panelWidth", 344)))
                Layout.preferredWidth: Math.min(panelWidth, win.width - 480)
                Layout.fillHeight: true
                visible: win.panelsVisible && !win.isFull && App.currentFile !== ""
                color: Theme.panel
                Rectangle { width: 1; height: parent.height; color: resizer.containsMouse || resizer.pressed ? Theme.accent : Theme.line }
                // drag the left edge to resize
                MouseArea {
                    id: resizer
                    x: -3; width: 7; height: parent.height
                    z: 5
                    hoverEnabled: true
                    cursorShape: Qt.SizeHorCursor
                    property real startX: 0
                    property real startW: 0
                    onPressed: (m) => { startX = mapToItem(null, m.x, 0).x; startW = side.width }
                    onPositionChanged: (m) => { if (pressed) side.panelWidth = Math.max(300, Math.min(560, startW - (mapToItem(null, m.x, 0).x - startX))) }
                    onReleased: App.setSetting("panelWidth", Math.round(side.panelWidth))
                    onDoubleClicked: { side.panelWidth = 344; App.setSetting("panelWidth", 344) }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 1
                    spacing: 0
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                        Row {
                            id: tabRow
                            anchors.fill: parent
                            anchors.leftMargin: 8; anchors.rightMargin: 8
                            Repeater {
                                model: [
                                    { icon: "marker", tip: "Markers and exports", keys: "Alt+1", warn: false },
                                    { icon: "list", tip: "Clips in this folder", keys: "Alt+2", warn: false },
                                    { icon: "info", tip: "Media info", keys: "Alt+3", warn: Media.probe.vfr === true },
                                    { icon: "transcript", tip: "Transcript", keys: "Alt+4", warn: false },
                                    { icon: "scope", tip: "Video scopes", keys: "Alt+5", warn: false },
                                    { icon: "audio-track", tip: "Audio and music", keys: "Alt+6", warn: false }
                                ]
                                delegate: AbstractButton {
                                    id: tabBtn
                                    required property var modelData
                                    required property int index
                                    readonly property bool current: win.sideTab === index
                                    width: tabRow.width / 6
                                    height: tabRow.height
                                    focusPolicy: Qt.NoFocus
                                    hoverEnabled: true
                                    onClicked: win.sideTab = index
                                    background: Item {
                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 34; height: 30; radius: Theme.radius
                                            color: tabBtn.hovered && !tabBtn.current ? Theme.hover : "transparent"
                                            Behavior on color { ColorAnimation { duration: Theme.fast } }
                                        }
                                        Rectangle {
                                            anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                                            width: 22; height: 2; radius: 1
                                            color: Theme.accent
                                            opacity: tabBtn.current ? 1 : 0
                                            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                                        }
                                    }
                                    contentItem: Item {
                                        Icon {
                                            id: ti
                                            anchors.centerIn: parent
                                            name: tabBtn.modelData.icon; size: 18
                                            color: tabBtn.current ? Theme.ink : (tabBtn.hovered ? Theme.ink2 : Theme.ink3)
                                        }
                                        Rectangle {
                                            visible: tabBtn.modelData.warn
                                            x: ti.x + ti.width - 2; y: ti.y - 2
                                            width: 6; height: 6; radius: 3; color: Theme.warn
                                        }
                                    }
                                    Tip { visible: tabBtn.hovered; delay: 250; text: tabBtn.modelData.tip + (tabBtn.modelData.warn ? ". Variable frame rate" : ""); keys: tabBtn.modelData.keys }
                                }
                            }
                        }
                    }
                    // Each tab is built the first time it's shown, then kept.
                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: win.sideTab
                        component Tab: Loader {
                            required property int tab
                            property bool seen: false
                            readonly property bool current: win.sideTab === tab && side.visible
                            onCurrentChanged: if (current) seen = true
                            Component.onCompleted: if (current) seen = true
                            active: seen
                        }
                        Tab { tab: 0; sourceComponent: MarkerPanel { mpv: player; win: mainWindow } }
                        Tab { tab: 1; sourceComponent: ClipsPanel { win: mainWindow } }
                        Tab { tab: 2; sourceComponent: InfoPanel { mpv: player } }
                        Tab { tab: 3; sourceComponent: TranscriptPanel { mpv: player; win: mainWindow } }
                        Tab { tab: 4; sourceComponent: ScopesPanel { mpv: player; shown: side.visible && win.sideTab === 4 } }
                        Tab { tab: 5; sourceComponent: AudioPanel { mpv: player; win: mainWindow; shown: side.visible && win.sideTab === 5 } }
                    }
                }
            }
        }

        // Docked timeline and transport; in full screen they move over the picture.
        Item {
            id: dock
            Layout.fillWidth: true
            Layout.preferredHeight: win.isFull || App.currentFile === "" ? 0 : controls.implicitHeight + 1
            visible: !win.isFull && App.currentFile !== ""
            Rectangle { width: parent.width; height: 1; color: Theme.line }
        }
    }

    Column {
        id: controls
        parent: win.isFull ? overlayControls : dock
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.bottomMargin: win.isFull ? 8 : 0
        Timeline {
            id: timeline; objectName: "timeline"; width: parent.width; mpv: mpv; overlay: win.isFull
            beatGrid: Tempo.bpm > 0 && Tempo.file === App.currentFile ? [Tempo.bpm, Tempo.firstBeat, Tempo.beatsPerBar] : []
            beatsShown: win.beatGridOn
        }
        TransportBar { id: transport; width: parent.width; mpv: mpv; win: win; overlay: win.isFull }
    }

    Loader {
        id: paletteLoader
        active: false
        sourceComponent: CommandPalette {
            actions: win.actionList.map(a => ({ id: a.id, name: a.name, category: a.category, keys: win.keysLabel(a), run: a.run }))
        }
    }
    Loader { id: settingsLoader; active: false; sourceComponent: SettingsDialog { win: mainWindow } }
    Loader {
        id: urlLoader
        active: false
        sourceComponent: OpenUrlDialog { onAccepted: (u) => win.openMedia(u); onDownload: (u) => win.openDownloads(u) }
    }
    Loader { id: resolveLoader; active: false; sourceComponent: ResolveDialog { win: mainWindow } }
    Loader { id: thumbLoader; active: false; sourceComponent: ThumbnailDialog { win: mainWindow; mpv: player } }
    Loader { id: downloadLoader; active: false; sourceComponent: DownloadDialog { objectName: "downloadDialog"; win: mainWindow } }
}
