import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Slate

// Send to DaVinci Resolve: builds a timeline (FCPXML) from selects, markers and ratings.
// Options on the left, a live preview of the timeline on the right (drag rows to reorder,
// untick to leave one out). Every option is remembered.
Popup {
    id: dlg
    required property var win

    parent: Overlay.overlay
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(1000, parent.width - 48)
    height: Math.min(680, parent.height - 48)
    padding: 0
    closePolicy: ResolveExport.busy ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
    Overlay.modal: Rectangle { color: Theme.scrim }
    enter: Transition { ParallelAnimation { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.normal; easing.type: Easing.OutCubic } } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast } }
    background: Rectangle { color: Theme.popup; radius: Theme.radiusLg; border.color: Theme.lineStrong }

    // ---- options (remembered) -------------------------------------------------------
    function opt(k, d) { return App.setting("resolve/" + k, d) }
    property string scope: opt("scope", "folder")
    property int minStars: opt("minStars", 3)
    property string use: opt("use", "selectsOrWhole")
    property string order: opt("order", "folder")
    property bool reverse: opt("reverse", false)
    property string handles: opt("handles", "0")
    property real gap: opt("gap", 0)
    property bool incNotes: opt("incNotes", true)
    property bool incGood: opt("incGood", true)
    property bool incBad: opt("incBad", true)
    property bool markClipStarts: opt("markClipStarts", false)
    property string markerStyle: opt("markerStyle", "clips")
    property string names: opt("names", "fileNote")
    property bool starsInName: opt("starsInName", false)
    property bool ratingKeyword: opt("ratingKeyword", true)
    property string fps: opt("fps", "match")
    property string resolution: opt("resolution", "match")
    property string output: opt("output", "timeline")
    property string title: ""
    property bool titleEdited: false
    function set(k, v) { dlg[k] = v; App.setSetting("resolve/" + k, v); rebuild() }

    property var customOrder: []     // item keys, for the Custom order
    property var excluded: ({})      // item keys left out
    property double total: 0
    property int includedCount: 0
    readonly property string folderName: { const p = App.currentFile.split(/[\\/]/); return p.length > 1 ? p[p.length - 2] : "" }

    onAboutToShow: {
        if (!titleEdited) title = (folderName || "Slate") + " selects"
        const saved = App.setting("resolve/order/" + folderName, "")
        customOrder = saved ? saved.split("\n") : []
        if (App.queue.length <= 1 && scope !== "clip") scope = "clip"
        rebuild()
    }

    function handleSeconds(file) {
        return handles.endsWith("f") ? -1 : Number(handles)  // frames are converted per clip in C++
    }
    function itemName(base, note, rating) {
        let n = base
        const hasNote = note && note !== "Select"
        if (names === "fileNote" && hasNote) n = base + " · " + note
        else if (names === "note" && hasNote) n = note
        if (starsInName && rating > 0) n += " " + "★".repeat(rating)
        return n
    }
    function markersFor(all, a, b, wholeClip) {
        const out = []
        for (const m of all) {
            const range = m.end !== undefined && m.end > m.time
            if (range) { if (wholeClip) out.push(m); continue }  // selects become clips, or show as ranges on whole clips
            if ((m.kind === 0 && !incNotes) || (m.kind === 1 && !incGood) || (m.kind === 2 && !incBad)) continue
            if (m.time >= a - 0.001 && (b < 0 || m.time < b)) out.push(m)
        }
        return out
    }

    // Builds the list of timeline items from the options.
    function plan() {
        const files = App.queue.length ? App.queue : (App.currentFile ? [App.currentFile] : [])
        const out = []
        let fileIndex = 0
        for (const f of files) {
            const idx = fileIndex++
            if (scope === "clip" && f !== App.currentFile) continue
            const facts = App.clipFacts(f)
            if (scope === "rated" && facts.rating < minStars) continue
            const all = App.markersFor(f)
            const sels = all.filter(m => m.end !== undefined && m.end > m.time)
            if (scope === "selects" && sels.length === 0) continue
            const whole = use === "whole" || (use === "selectsOrWhole" && sels.length === 0)
            if (use === "selects" && sels.length === 0) continue
            const base = facts.base
            if (whole) {
                const d = facts.duration
                out.push({ key: f + "|whole", file: f, start: 0, end: -1, length: d > 0 ? d : -1, note: "", rating: facts.rating,
                           modified: facts.modified, fileIndex: idx, selIndex: 0, name: itemName(base, "", facts.rating),
                           markers: markersFor(all, 0, -1, true) })
            } else {
                sels.forEach((s, k) => out.push({ key: f + "|" + s.time.toFixed(3), file: f, start: s.time, end: s.end, length: s.end - s.time,
                                                  note: s.note, rating: facts.rating, modified: facts.modified, fileIndex: idx, selIndex: k,
                                                  name: itemName(base, s.note, facts.rating), markers: markersFor(all, s.time, s.end, false) }))
            }
        }
        const byFile = (a, b) => a.fileIndex - b.fileIndex || a.selIndex - b.selIndex
        const sorters = {
            folder: byFile,
            rating: (a, b) => b.rating - a.rating || byFile(a, b),
            time: (a, b) => a.modified - b.modified || byFile(a, b),
            name: (a, b) => a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" }) || byFile(a, b),
            length: (a, b) => b.length - a.length || byFile(a, b),
            custom: (a, b) => {
                const ia = customOrder.indexOf(a.key), ib = customOrder.indexOf(b.key)
                return (ia < 0 ? 1e9 + a.fileIndex : ia) - (ib < 0 ? 1e9 + b.fileIndex : ib) || byFile(a, b)
            }
        }
        out.sort(sorters[order] || byFile)
        if (reverse) out.reverse()
        return out
    }
    function rebuild() {
        const p = plan()
        items.clear()
        let t = 0, n = 0
        for (const it of p) {
            const inc = !excluded[it.key]
            items.append({ key: it.key, file: it.file, start: it.start, end: it.end, length: it.length, name: it.name, rating: it.rating,
                           note: it.note, markerCount: it.markers.length, included: inc, payload: JSON.stringify(it.markers) })
            if (inc) { n++; if (it.length > 0) t += it.length }
        }
        includedCount = n
        total = t + (n > 1 ? gap * (n - 1) : 0)
    }
    function recount() {
        let t = 0, n = 0
        for (let i = 0; i < items.count; ++i) { const it = items.get(i); if (it.included) { n++; if (it.length > 0) t += it.length } }
        includedCount = n
        total = t + (n > 1 ? gap * (n - 1) : 0)
    }
    function saveCustomOrder() {
        const keys = []
        for (let i = 0; i < items.count; ++i) keys.push(items.get(i).key)
        customOrder = keys
        App.setSetting("resolve/order/" + folderName, keys.join("\n"))
        if (order !== "custom") { order = "custom"; App.setSetting("resolve/order", "custom") }
        if (reverse) { reverse = false; App.setSetting("resolve/reverse", false) }
    }
    function startExport() {
        const list = []
        for (let i = 0; i < items.count; ++i) {
            const it = items.get(i)
            if (!it.included) continue
            list.push({ file: it.file, start: it.start, end: it.end, name: it.name, markers: JSON.parse(it.payload),
                        keywords: ratingKeyword && it.rating > 0 ? [it.rating + (it.rating === 1 ? " star" : " stars")] : [] })
        }
        const hs = handles.endsWith("f") ? Number(handles.slice(0, -1)) : Number(handles)
        ResolveExport.start(list, { title: title, fps: fps, resolution: resolution, gap: gap, handles: hs,
                                    handlesUnit: handles.endsWith("f") ? "frames" : "seconds",
                                    clipMarkers: markerStyle === "clips", markClipStarts: markClipStarts, output: output })
    }
    Connections {
        target: ResolveExport
        function onFinished(ok, message, path) { if (ok && dlg.opened) dlg.close() }
    }

    ListModel { id: items }

    // ---- layout ---------------------------------------------------------------------------
    component Section: Text { color: Theme.ink; font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold; Layout.topMargin: 14; Layout.bottomMargin: 2 }
    component Toggle: SSwitch { Layout.fillWidth: true; implicitHeight: 28; leftPadding: 0 }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // header
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            Layout.leftMargin: 20; Layout.rightMargin: 12
            spacing: 10
            Icon { name: "resolve"; size: 20; color: Theme.accentText }
            Column {
                Layout.fillWidth: true
                spacing: 2
                Text { text: "Send to DaVinci Resolve"; color: Theme.ink; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold }
                Text { text: "Builds a timeline from your selects, markers and ratings. Resolve links it to the original files."; color: Theme.ink3; font.pixelSize: Theme.fontLabel }
            }
            Btn { glyph: "close"; tip: "Close"; keys: "Esc"; enabled: !ResolveExport.busy; onClicked: dlg.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // options
            Flickable {
                id: optsFlick
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                contentHeight: optCol.implicitHeight + 24
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                WheelScroll {}
                ScrollBar.vertical: SScrollBar {}
                ColumnLayout {
                    id: optCol
                    x: 20; y: 4; width: optsFlick.width - 36
                    spacing: 6

                    Section { text: "What goes on the timeline" }
                    Choice {
                        label: "Clips"; value: dlg.scope
                        options: [{ value: "clip", text: "This clip" }, { value: "folder", text: "All clips in the folder" },
                                  { value: "rated", text: "Rated clips only" }, { value: "selects", text: "Clips with selects" }]
                        onPicked: (v) => dlg.set("scope", v)
                    }
                    Choice {
                        visible: dlg.scope === "rated"
                        label: "At least"; value: dlg.minStars
                        options: [5, 4, 3, 2, 1].map(n => ({ value: n, text: "★".repeat(n) + (n < 5 ? " and up" : " only") }))
                        onPicked: (v) => dlg.set("minStars", v)
                    }
                    Choice {
                        label: "Use"; value: dlg.use
                        options: [{ value: "selectsOrWhole", text: "Selects, else the whole clip" }, { value: "selects", text: "Selects only" },
                                  { value: "whole", text: "Whole clips" }]
                        onPicked: (v) => dlg.set("use", v)
                    }
                    Choice {
                        label: "Order"; value: dlg.order
                        options: [{ value: "folder", text: "Folder order" }, { value: "rating", text: "Best rated first" },
                                  { value: "time", text: "Recording time" }, { value: "name", text: "Name" },
                                  { value: "length", text: "Longest first" }, { value: "custom", text: "Custom (drag in the list)" }]
                        onPicked: (v) => dlg.set("order", v)
                    }
                    Toggle { text: "Reverse the order"; checked: dlg.reverse; onToggled: dlg.set("reverse", checked) }
                    Choice {
                        label: "Handles"; value: dlg.handles
                        tip: "Extra footage before and after each select, so you have room to trim in Resolve"
                        options: [{ value: "0", text: "None" }, { value: "6f", text: "6 frames" }, { value: "12f", text: "12 frames" },
                                  { value: "0.5", text: "Half a second" }, { value: "1", text: "1 second" }, { value: "2", text: "2 seconds" }]
                        onPicked: (v) => dlg.set("handles", v)
                    }
                    Choice {
                        label: "Gap between clips"; value: dlg.gap
                        options: [{ value: 0, text: "None" }, { value: 1, text: "1 second" }, { value: 2, text: "2 seconds" }, { value: 5, text: "5 seconds" }]
                        onPicked: (v) => dlg.set("gap", v)
                    }

                    Section { text: "Markers" }
                    Toggle { text: "Notes"; checked: dlg.incNotes; onToggled: dlg.set("incNotes", checked) }
                    Toggle { text: "Good takes"; checked: dlg.incGood; onToggled: dlg.set("incGood", checked) }
                    Toggle { text: "Mistakes"; checked: dlg.incBad; onToggled: dlg.set("incBad", checked) }
                    Toggle { text: "Clip name where each clip starts"; checked: dlg.markClipStarts; onToggled: dlg.set("markClipStarts", checked) }
                    Choice {
                        label: "Put them"; value: dlg.markerStyle
                        options: [{ value: "clips", text: "On the clips" }, { value: "timeline", text: "On the timeline, coloured" }]
                        tip: "On the clips: they move with each clip. Coloured: Slate also saves a marker file to import onto the new timeline"
                        onPicked: (v) => dlg.set("markerStyle", v)
                    }

                    Section { text: "Clip names" }
                    Choice {
                        label: "Name clips by"; value: dlg.names
                        options: [{ value: "file", text: "File name" }, { value: "fileNote", text: "File name and select note" }, { value: "note", text: "Select note" }]
                        onPicked: (v) => dlg.set("names", v)
                    }
                    Toggle { text: "Add the star rating to names"; checked: dlg.starsInName; onToggled: dlg.set("starsInName", checked) }
                    Toggle { text: "Add the star rating as a keyword"; checked: dlg.ratingKeyword; onToggled: dlg.set("ratingKeyword", checked) }

                    Section { text: "Timeline" }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Name"; color: Theme.ink2; font.pixelSize: Theme.fontLabel }
                        TextField {
                            id: titleField
                            Layout.fillWidth: true
                            Layout.leftMargin: 20
                            implicitHeight: 28
                            text: dlg.title
                            leftPadding: 8; rightPadding: 8
                            color: Theme.ink; font.pixelSize: Theme.fontLabel
                            selectByMouse: true; selectionColor: Theme.accentFill
                            background: Rectangle { radius: Theme.radius; color: Theme.well; border.color: titleField.activeFocus ? Theme.accent : Theme.line }
                            onTextEdited: { dlg.title = text; dlg.titleEdited = true }
                        }
                    }
                    Choice {
                        label: "Frame rate"; value: dlg.fps
                        options: [{ value: "match", text: "Same as the first clip" }].concat(["23.976", "24", "25", "29.97", "30", "50", "59.94", "60"].map(f => ({ value: f, text: f + " fps" })))
                        onPicked: (v) => dlg.set("fps", v)
                    }
                    Choice {
                        label: "Resolution"; value: dlg.resolution
                        options: [{ value: "match", text: "Same as the first clip" }, { value: "1080p", text: "1920 × 1080" },
                                  { value: "4k", text: "3840 × 2160 (4K UHD)" }, { value: "vertical", text: "1080 × 1920 (vertical)" }]
                        onPicked: (v) => dlg.set("resolution", v)
                    }

                    Section { text: "Save" }
                    Choice {
                        label: "Make"; value: dlg.output
                        options: [{ value: "timeline", text: "A Resolve timeline" }, { value: "both", text: "Timeline and each clip as a file" },
                                  { value: "files", text: "Each clip as a file only" }]
                        tip: "Clip files are cut without re-encoding, into a folder named after the timeline"
                        onPicked: (v) => dlg.set("output", v)
                    }
                    Text {
                        Layout.fillWidth: true; Layout.topMargin: 2
                        wrapMode: Text.WordWrap
                        text: "Saved to " + App.exportFolderLabel()
                        color: Theme.ink3; font.pixelSize: Theme.fontSmall
                    }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.line }

            // preview
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    Layout.leftMargin: 16; Layout.rightMargin: 16
                    Text {
                        text: dlg.includedCount === 0 ? "Nothing to send yet" : (dlg.includedCount === 1 ? "1 clip" : dlg.includedCount + " clips") + "  ·  " + App.formatClock(dlg.total)
                        color: Theme.ink; font.pixelSize: Theme.fontBody; font.weight: Font.Medium
                    }
                    Item { Layout.fillWidth: true }
                    Text { visible: list.count > 1; text: "Drag to reorder"; color: Theme.ink3; font.pixelSize: Theme.fontSmall }
                }
                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: items
                    boundsBehavior: Flickable.StopAtBounds
                    WheelScroll {}
                    ScrollBar.vertical: SScrollBar {}
                    moveDisplaced: Transition { NumberAnimation { properties: "y"; duration: Theme.fast } }
                    property int dragFrom: -1

                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property string file
                        required property string name
                        required property double start
                        required property double end
                        required property double length
                        required property int rating
                        required property int markerCount
                        required property bool included
                        width: list.width
                        height: 56
                        color: grip.pressed ? Theme.press : (hov.hovered ? Theme.raise : "transparent")
                        opacity: included ? 1 : 0.45
                        HoverHandler { id: hov }
                        Rectangle { anchors.bottom: parent.bottom; x: 16; width: parent.width - 32; height: 1; color: Theme.line; opacity: 0.6 }

                        Text { x: 6; width: 22; anchors.verticalCenter: parent.verticalCenter; horizontalAlignment: Text.AlignRight; text: row.index + 1; color: Theme.ink3; font.pixelSize: Theme.fontSmall }
                        // drag handle
                        Item {
                            x: 30; width: 22; height: parent.height
                            Icon { anchors.centerIn: parent; name: "grip"; size: 16; color: grip.containsMouse || grip.pressed ? Theme.ink : Theme.ink3 }
                            MouseArea {
                                id: grip
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                preventStealing: true
                                onPressed: list.dragFrom = row.index
                                onPositionChanged: (m) => {
                                    if (!pressed) return
                                    const y = mapToItem(list.contentItem, m.x, m.y).y
                                    const to = Math.max(0, Math.min(items.count - 1, Math.floor(y / row.height)))
                                    if (to !== row.index) items.move(row.index, to, 1)
                                }
                                onReleased: { if (list.dragFrom !== row.index) dlg.saveCustomOrder(); list.dragFrom = -1 }
                            }
                        }
                        Rectangle {
                            id: thumb
                            x: 56; anchors.verticalCenter: parent.verticalCenter
                            width: 64; height: 36; radius: 4; color: Theme.bg; clip: true
                            readonly property string src: { Media.queueRevision; return Media.thumbFor(row.file) }
                            Image { anchors.fill: parent; source: thumb.src; fillMode: Image.PreserveAspectCrop; visible: thumb.src !== ""; asynchronous: true }
                            Icon { anchors.centerIn: parent; visible: thumb.src === ""; name: "film"; size: 16; color: Theme.ink3 }
                        }
                        Column {
                            anchors.left: thumb.right; anchors.leftMargin: 10
                            anchors.right: lenText.left; anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Text { width: parent.width; text: row.name; color: Theme.ink; font.pixelSize: Theme.fontBody; elide: Text.ElideMiddle }
                            Text {
                                width: parent.width
                                text: (row.end < 0 ? "Whole clip" : App.formatClock(row.start) + " to " + App.formatClock(row.end))
                                      + (row.rating > 0 ? "  ·  " + "★".repeat(row.rating) : "")
                                      + (row.markerCount > 0 ? "  ·  " + row.markerCount + (row.markerCount === 1 ? " marker" : " markers") : "")
                                color: Theme.ink3; font.pixelSize: Theme.fontSmall; elide: Text.ElideRight
                            }
                        }
                        Text {
                            id: lenText
                            anchors.right: check.left; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                            text: row.length > 0 ? App.formatClock(row.length) : "–"
                            color: Theme.ink2; font.pixelSize: Theme.fontLabel
                        }
                        AbstractButton {
                            id: check
                            anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter
                            width: 20; height: 20
                            focusPolicy: Qt.NoFocus
                            hoverEnabled: true
                            onClicked: {
                                const ex = Object.assign({}, dlg.excluded)
                                const k = items.get(row.index).key
                                if (row.included) ex[k] = true; else delete ex[k]
                                dlg.excluded = ex
                                items.setProperty(row.index, "included", !row.included)
                                dlg.recount()
                            }
                            background: Rectangle { radius: 4; color: row.included ? Theme.accentFill : "transparent"; border.color: row.included ? Theme.accentFill : Theme.lineStrong; border.width: 1.5 }
                            contentItem: Item { Icon { anchors.centerIn: parent; visible: row.included; name: "check"; size: 14; color: "white" } }
                            Tip { visible: check.hovered; text: row.included ? "Leave this out" : "Put this back" }
                        }
                    }

                    Column {
                        anchors.centerIn: parent
                        width: parent.width - 80
                        spacing: 8
                        visible: items.count === 0
                        Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "select"; size: 30; color: Theme.ink3 }
                        Text {
                            width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                            text: dlg.use === "selects" ? "No selects in these clips yet. Press S at the start and end of each part worth keeping, or choose \"Selects, else the whole clip\"."
                                                        : "No clips match. Try another choice under Clips."
                            color: Theme.ink3; font.pixelSize: Theme.fontLabel
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }
        // footer
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            Layout.leftMargin: 20; Layout.rightMargin: 16
            spacing: 10
            Text {
                Layout.fillWidth: true
                visible: !ResolveExport.busy
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                text: dlg.output === "files" ? "Each clip is cut without re-encoding, so it's instant and keeps full quality."
                      : (dlg.markerStyle === "timeline" ? "In Resolve: File > Import > Timeline, then right-click the new timeline > Timelines > Import > Timeline Markers from EDL for the coloured markers."
                                                        : "In Resolve: File > Import > Timeline and pick the .fcpxml file. The clips link to your original footage.")
                color: Theme.ink3; font.pixelSize: Theme.fontSmall
            }
            RowLayout {
                visible: ResolveExport.busy
                Layout.fillWidth: true
                spacing: 10
                Text { text: ResolveExport.status; color: Theme.ink2; font.pixelSize: Theme.fontLabel; elide: Text.ElideRight; Layout.maximumWidth: 320 }
                Rectangle {
                    Layout.fillWidth: true; height: 4; radius: 2; color: Theme.press
                    Rectangle { width: parent.width * ResolveExport.progress; height: parent.height; radius: 2; color: Theme.accent }
                }
            }
            Btn { flat: false; text: ResolveExport.busy ? "Stop" : "Cancel"; onClicked: ResolveExport.busy ? ResolveExport.cancel() : dlg.close() }
            Btn {
                primary: true
                glyph: dlg.output === "files" ? "scissors" : "resolve"
                text: dlg.output === "files" ? "Export clips" : "Save timeline"
                enabled: dlg.includedCount > 0 && !ResolveExport.busy && (dlg.output !== "files" || !App.busy)
                onClicked: dlg.startExport()
            }
        }
    }
}
