import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Slate

// Offline transcript. "This clip": click a line to jump there, search, export as subtitles or text.
// "All clips": transcribe the whole folder in the background, then search every clip at once.
// "Repeated lines" finds lines said more than once (retakes) and can mark the keeper of each.
Item {
    id: panel
    objectName: "transcriptPanel"
    required property var mpv
    required property var win
    property string query: ""
    property bool allClips: false
    property bool repeatsView: false
    readonly property bool multi: App.queue.length > 1
    readonly property bool wide: allClips && multi
    readonly property var scopeFiles: wide ? App.queue : (App.currentFile ? [App.currentFile] : [])
    readonly property int transcribed: { Transcript.revision; return Transcript.countWithTranscripts(App.queue) }

    // This clip: matching lines with their index
    readonly property var lines: {
        const q = query.toLowerCase().trim()
        const all = Transcript.segments
        const out = []
        for (let k = 0; k < all.length; ++k) if (!q || all[k].text.toLowerCase().indexOf(q) >= 0) out.push({ i: k, start: all[k].start, end: all[k].end, text: all[k].text, file: App.currentFile })
        return out
    }
    // All clips: hits across the folder
    readonly property var hits: { Transcript.revision; return wide && query.trim() ? Transcript.search(App.queue, query) : [] }
    property var groups: []
    function refreshRepeats() { groups = repeatsView ? Transcript.repeats(scopeFiles) : [] }
    onRepeatsViewChanged: refreshRepeats()
    onScopeFilesChanged: refreshRepeats()
    Connections { target: Transcript; function onTranscriptsChanged() { panel.refreshRepeats() } }

    readonly property int playing: Transcript.state === "ready" ? Transcript.indexAt(mpv.position) : -1
    readonly property string stateText: {
        switch (Transcript.state) {
        case "queued": return "Waiting for the clip before it…"
        case "downloading": return "Downloading the speech model (" + Transcript.modelSizeLabel(Transcript.model) + ", once)…"
        case "preparing": return "Reading audio…"
        case "transcribing": return "Transcribing on this computer…"
        default: return ""
        }
    }
    readonly property bool showList: wide || repeatsView || Transcript.state === "ready"

    function baseName(f) { const s = String(f).replace(/\\/g, "/"); return s.substring(s.lastIndexOf("/") + 1) }
    function jump(file, t) {
        if (file === App.currentFile) { list.follow = true; mpv.seekTo(t, true) }
        else win.openMediaAt(file, t)
    }
    function makeSelect(line) {
        App.addMarkersTo(line.file, [{ time: line.start, end: line.end, kind: 3, note: line.text.substring(0, 80) }], "Select from transcript")
        win.toast("Made a select from that line" + (line.file === App.currentFile ? "" : " in " + baseName(line.file)))
    }
    function snippet(t) { t = String(t); return t.length > 60 ? t.substring(0, 57) + "…" : t }
    // Good take on the keeper, Mistake on the other takes. Per clip, so each clip gets one undo step.
    function keepTakes(gs) {
        const per = {}
        let n = 0
        for (const g of gs) {
            for (const m of g.members) {
                if (!per[m.file]) per[m.file] = []
                per[m.file].push({ time: m.start, kind: m.keeper ? 1 : 2, note: (m.keeper ? "Best take: " : "Retake: ") + snippet(m.text) })
                ++n
            }
        }
        for (const f in per) App.addMarkersTo(f, per[f], gs.length === 1 ? "Mark takes" : "Mark every repeated line")
        win.toast(gs.length === 1 ? "Marked the best take and " + (n - 1) + " retake" + (n === 2 ? "" : "s") : "Marked " + gs.length + " repeated lines. Ctrl+Z undoes it")
    }
    function focusSearch() {
        repeatsView = false
        if (!showList && multi) allClips = true
        search.forceActiveFocus(); search.selectAll()
    }
    Connections { target: panel.win; function onTranscriptFocus() { panel.win.pendingTranscriptFocus = false; Qt.callLater(panel.focusSearch) } }
    Component.onCompleted: if (win.pendingTranscriptFocus) { win.pendingTranscriptFocus = false; Qt.callLater(focusSearch) }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10; Layout.rightMargin: 8
            Layout.preferredHeight: 44
            spacing: 6
            Segmented {
                visible: panel.multi
                options: [{ text: "This clip" }, { text: "All clips" }]
                current: panel.allClips ? 1 : 0
                onPicked: i => panel.allClips = i === 1
            }
            Text {
                visible: !panel.multi
                Layout.leftMargin: 4
                text: panel.repeatsView ? panel.groups.length + " repeated" : (Transcript.state === "ready" ? Transcript.segments.length + " lines" : "Transcript")
                color: Theme.ink3; font.pixelSize: Theme.fontLabel
            }
            Item { Layout.fillWidth: true }
            Btn {
                glyph: "repeat"; tip: "Repeated lines (retakes)"
                active: panel.repeatsView
                onClicked: panel.repeatsView = !panel.repeatsView
            }
            Btn {
                id: moreBtn
                visible: Transcript.state === "ready" && !panel.wide
                glyph: "export"; tip: "Export transcript"
                onClicked: tMenu.popup(moreBtn, moreBtn.width - tMenu.implicitWidth, moreBtn.height + 2)
                SMenu {
                    id: tMenu
                    implicitWidth: 250
                    SMenuItem { text: "Save as subtitles (SRT)"; glyph: "subtitles"; onTriggered: { const p = Transcript.exportAs("srt"); panel.win.toast(p ? "Saved subtitles next to the clip" : "Couldn't save", p) } }
                    SMenuItem { text: "Save as text (TXT)"; glyph: "file"; onTriggered: { const p = Transcript.exportAs("txt"); panel.win.toast(p ? "Saved transcript next to the clip" : "Couldn't save", p) } }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.line } }
                    SMenuItem { text: "Transcribe again"; glyph: "loop"; onTriggered: Transcript.start(App.currentFile) }
                }
            }
        }

        // Folder status: how many clips have transcripts, and the background queue
        Rectangle {
            visible: panel.wide
            Layout.fillWidth: true
            Layout.leftMargin: 10; Layout.rightMargin: 10; Layout.bottomMargin: 8
            implicitHeight: folderCol.implicitHeight + 20
            radius: Theme.radius
            color: Theme.raise
            border.color: Theme.line
            ColumnLayout {
                id: folderCol
                anchors.fill: parent; anchors.margins: 10
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Icon { name: "transcript"; size: 16; color: panel.transcribed === App.queue.length ? Theme.good : Theme.ink3 }
                    Text {
                        Layout.fillWidth: true
                        text: Transcript.batchTotal > 0 && Transcript.busy
                              ? "Transcribing " + Math.min(Transcript.batchDone + 1, Transcript.batchTotal) + " of " + Transcript.batchTotal
                              : panel.transcribed + " of " + App.queue.length + " clips transcribed"
                        color: Theme.ink; font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }
                    Btn {
                        visible: !Transcript.busy && panel.transcribed < App.queue.length && Transcript.available
                        primary: true
                        text: panel.transcribed === 0 ? "Transcribe all" : "Transcribe the rest"
                        onClicked: {
                            const n = Transcript.transcribeAll(App.queue)
                            panel.win.toast(n ? "Transcribing " + n + " clip" + (n === 1 ? "" : "s") + " in the background. Keep reviewing" : "Every clip already has a transcript")
                        }
                    }
                    Btn { visible: Transcript.busy; text: "Cancel"; onClicked: Transcript.cancel() }
                }
                Text {
                    visible: Transcript.busy
                    Layout.fillWidth: true
                    text: panel.baseName(Transcript.jobFile)
                    color: Theme.ink3; font.pixelSize: 11
                    elide: Text.ElideMiddle
                }
                Rectangle {
                    visible: Transcript.busy
                    Layout.fillWidth: true
                    implicitHeight: 4; radius: 2; color: Theme.press
                    Rectangle {
                        readonly property double whole: Transcript.batchTotal > 0 ? (Transcript.batchDone + Transcript.jobProgress) / Transcript.batchTotal : Transcript.jobProgress
                        width: parent.width * Math.min(1, whole); height: parent.height; radius: 2; color: Theme.accent
                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }
            }
        }

        // search
        TextField {
            id: search
            objectName: "transcriptSearch"
            visible: panel.showList && !panel.repeatsView
            Layout.fillWidth: true
            Layout.leftMargin: 10; Layout.rightMargin: 10; Layout.bottomMargin: 6
            implicitHeight: 32
            leftPadding: 32
            placeholderText: panel.wide ? "Search every clip" : "Search the transcript"
            placeholderTextColor: Theme.ink3
            color: Theme.ink
            font.pixelSize: Theme.fontBody
            selectByMouse: true
            selectionColor: Theme.accent
            onTextChanged: panel.query = text
            Keys.onEscapePressed: { text = ""; focus = false }
            background: Rectangle { radius: Theme.radius; color: Theme.well; border.color: parent.activeFocus ? Theme.accent : Theme.line }
            Icon { x: 9; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 15; color: Theme.ink3 }
        }

        // One transcript line (this clip or a search hit)
        component Line: Rectangle {
            id: line
            required property var item
            property bool now: false
            property bool showFile: false
            width: ListView.view ? ListView.view.width : 0
            height: txt.implicitHeight + (showFile ? 30 : 16)
            color: now ? Theme.accentSoft : (lh.hovered ? Theme.raise : "transparent")
            HoverHandler { id: lh }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                onClicked: e => {
                    if (e.button === Qt.RightButton) lineMenu.popup()
                    else panel.jump(line.item.file, line.item.start)
                }
            }
            SMenu {
                id: lineMenu
                implicitWidth: 240
                SMenuItem { text: "Make a select from this line"; glyph: "select"; onTriggered: panel.makeSelect(line.item) }
                SMenuItem { text: "Copy text"; glyph: "copy"; onTriggered: { App.copyText(line.item.text); panel.win.toast("Copied") } }
            }
            Text {
                visible: line.showFile
                x: 14; y: 7
                width: parent.width - 28
                text: panel.baseName(line.item.file)
                color: Theme.ink3; font.pixelSize: 11
                elide: Text.ElideMiddle
            }
            Text {
                x: 14; y: line.showFile ? 22 : 8
                text: App.formatClock(line.item.start)
                color: line.now ? Theme.accent : Theme.ink3
                font.pixelSize: 11; font.family: Theme.mono
            }
            Text {
                id: txt
                x: 62; y: line.showFile ? 21 : 7
                width: parent.width - 62 - 14
                text: line.item.text
                color: line.now ? Theme.ink : Theme.ink2
                font.pixelSize: Theme.fontBody
                wrapMode: Text.Wrap
                lineHeight: 1.15
            }
        }

        ListView {
            id: list
            visible: panel.showList && !panel.repeatsView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.wide ? panel.hits : panel.lines
            spacing: 0
            boundsBehavior: Flickable.StopAtBounds
            WheelScroll {}
            ScrollBar.vertical: SScrollBar {}
            property bool follow: true
            onMovementStarted: follow = false
            Connections {
                target: panel
                function onPlayingChanged() {
                    if (!list.follow || panel.query || panel.wide) return
                    list.positionViewAtIndex(Math.max(0, panel.playing), ListView.Contain)
                }
            }
            delegate: Line {
                required property var modelData
                item: modelData
                now: !panel.wide && modelData.i === panel.playing
                showFile: panel.wide
            }
            Column {
                anchors.centerIn: parent
                width: parent.width - 48
                spacing: 6
                visible: list.count === 0
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    text: panel.wide
                          ? (panel.query.trim() ? "No clip says “" + panel.query.trim() + "”" : (panel.transcribed ? "Type to search what's said in " + panel.transcribed + " clip" + (panel.transcribed === 1 ? "" : "s") : "Transcribe the clips first, then search all of them at once"))
                          : (panel.query ? "No lines contain “" + panel.query + "”" : "No speech found")
                    color: Theme.ink3; font.pixelSize: 12
                }
            }
        }

        // Repeated lines
        ListView {
            id: reps
            visible: panel.repeatsView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.groups
            spacing: 8
            boundsBehavior: Flickable.StopAtBounds
            WheelScroll {}
            ScrollBar.vertical: SScrollBar {}
            header: Item {
                width: reps.width
                height: panel.groups.length ? 40 : 0
                visible: panel.groups.length > 0
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 10; anchors.bottomMargin: 8
                    Text { Layout.fillWidth: true; text: panel.groups.length + " line" + (panel.groups.length === 1 ? "" : "s") + " said more than once"; color: Theme.ink3; font.pixelSize: 11; elide: Text.ElideRight }
                    Btn { text: "Mark every keeper"; glyph: "sparkle"; onClicked: panel.keepTakes(panel.groups) }
                }
            }
            delegate: Rectangle {
                id: grp
                required property var modelData
                x: 10
                width: reps.width - 20
                height: gcol.implicitHeight + 16
                radius: Theme.radius
                color: Theme.raise
                border.color: Theme.line
                Column {
                    id: gcol
                    x: 0; y: 8
                    width: parent.width
                    spacing: 2
                    RowLayout {
                        x: 12; width: parent.width - 20
                        Text {
                            Layout.fillWidth: true
                            text: "“" + grp.modelData.text + "”"
                            color: Theme.ink; font.pixelSize: Theme.fontBody
                            wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                        }
                        Btn { glyph: "check"; tip: "Mark the best take good and the others as retakes"; onClicked: panel.keepTakes([grp.modelData]) }
                    }
                    Repeater {
                        model: grp.modelData.members
                        delegate: Rectangle {
                            id: take
                            required property var modelData
                            required property int index
                            width: gcol.width
                            height: 26
                            color: th.hovered ? Theme.hover : "transparent"
                            HoverHandler { id: th }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: panel.jump(take.modelData.file, take.modelData.start) }
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 10
                                spacing: 8
                                Rectangle { width: 6; height: 6; radius: 3; color: take.modelData.keeper ? Theme.good : (take.modelData.complete ? Theme.ink3 : Theme.bad) }
                                Text { text: "Take " + (take.index + 1); color: take.modelData.keeper ? Theme.ink : Theme.ink2; font.pixelSize: 12 }
                                Text {
                                    Layout.fillWidth: true
                                    text: (panel.wide ? panel.baseName(take.modelData.file) + "  " : "") + (take.modelData.keeper ? "best take" : (take.modelData.complete ? "" : "stopped part-way"))
                                    color: take.modelData.keeper ? Theme.good : Theme.ink3; font.pixelSize: 11
                                    elide: Text.ElideMiddle
                                }
                                Text { text: App.formatClock(take.modelData.start); color: Theme.ink3; font.pixelSize: 11; font.family: Theme.mono }
                            }
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 48
                visible: reps.count === 0
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                text: (panel.wide ? panel.transcribed : (Transcript.state === "ready" ? 1 : 0)) === 0
                      ? "Transcribe first. Then Slate finds the lines you said more than once, so you can keep the best take."
                      : "No repeated lines. Every line was said once."
                color: Theme.ink3; font.pixelSize: 12
            }
        }

        // not transcribed / busy / error (this clip)
        Item {
            visible: !panel.showList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Column {
                anchors.centerIn: parent
                width: parent.width - 48
                spacing: 10
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "transcript"; size: 28; color: Theme.ink3 }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    text: Transcript.state === "error" ? Transcript.error
                          : (panel.stateText || (Transcript.available ? "Turn speech into searchable, clickable text. It runs on this computer; nothing is uploaded." : "Transcription isn't included in this build."))
                    color: Transcript.state === "error" ? Theme.bad : Theme.ink2
                    font.pixelSize: 12
                }
                Rectangle {
                    visible: panel.stateText !== "" && Transcript.state !== "queued"
                    width: parent.width; height: 4; radius: 2; color: Theme.press
                    Rectangle { width: parent.width * Transcript.progress; height: parent.height; radius: 2; color: Theme.accent; Behavior on width { NumberAnimation { duration: 200 } } }
                }
                Btn {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: panel.stateText === "" && Transcript.available
                    primary: true
                    text: Transcript.state === "error" ? "Try again" : "Transcribe"
                    enabled: panel.mpv.loaded && App.currentFile !== ""
                    onClicked: Transcript.start(App.currentFile)
                }
                Btn {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: panel.stateText === "" && Transcript.available && panel.multi && panel.transcribed < App.queue.length
                    text: "Transcribe every clip in this folder"
                    onClicked: { panel.allClips = true; Transcript.transcribeAll(App.queue) }
                }
                Btn { anchors.horizontalCenter: parent.horizontalCenter; visible: panel.stateText !== ""; text: "Cancel"; onClicked: Transcript.cancel() }
                Text {
                    visible: panel.stateText === "" && Transcript.available && !Transcript.modelReady
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    text: "The first time, Slate downloads a " + Transcript.modelSizeLabel(Transcript.model) + " speech model."
                    color: Theme.ink3; font.pixelSize: 11
                }
            }
        }
    }
}
