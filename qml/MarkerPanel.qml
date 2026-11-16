import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Slate

// Markers for the current clip, plus every export.
Item {
    id: panel
    required property var mpv
    required property var win
    function fmt(t) { return mpv.hasVideo && mpv.fps > 0 ? App.formatTimecode(t, mpv.fps) : App.formatClock(t) }
    property int filter: 0   // 0 everything, 1 selects, 2 point markers
    function shows(isRange) { return filter === 0 || (filter === 1 && isRange) || (filter === 2 && !isRange) }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 8
            Layout.preferredHeight: 44
            Segmented {
                visible: App.markers.selectCount > 0 && App.markers.count > App.markers.selectCount
                implicitWidth: 196
                options: [{ text: "All " + App.markers.count }, { text: "Selects " + App.markers.selectCount }, { text: "Markers" }]
                current: panel.filter
                onPicked: (i) => panel.filter = i
            }
            Text {
                visible: !(App.markers.selectCount > 0 && App.markers.count > App.markers.selectCount)
                text: {
                    const sel = App.markers.selectCount, pts = App.markers.count - sel
                    const parts = []
                    if (pts || !sel) parts.push(pts === 1 ? "1 marker" : pts + " markers")
                    if (sel) parts.push(sel === 1 ? "1 select" : sel + " selects")
                    return parts.join("  ·  ")
                }
                color: Theme.ink3; font.pixelSize: Theme.fontLabel
            }
            Item { Layout.fillWidth: true }
            Btn { glyph: "select"; tip: panel.win.selectStart >= 0 ? "End the select here" : (App.inPoint >= 0 && App.outPoint > App.inPoint ? "Make a select from In to Out" : "Start a select at the playhead"); keys: "S"; active: panel.win.selectStart >= 0; enabled: panel.mpv.loaded; onClicked: panel.win.toggleSelect() }
            Btn { glyph: "plus"; tip: "Add marker at playhead"; keys: "M"; enabled: panel.mpv.loaded; onClicked: panel.win.addMarker() }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: App.markers
            boundsBehavior: Flickable.StopAtBounds
            WheelScroll {}
            ScrollBar.vertical: SScrollBar {}
            readonly property int playingRow: { App.markers.count; return App.markers.rowNear(panel.mpv.position, panel.mpv.fps > 0 ? 0.5 / panel.mpv.fps : 0.02) }

            delegate: Rectangle {
                id: row
                required property int index
                required property double time
                required property string note
                required property int kind
                required property double end
                required property bool isRange
                width: list.width
                height: panel.shows(isRange) ? 52 : 0
                visible: height > 0
                color: index === list.playingRow ? Theme.accentSoft : (rowHover.hovered ? Theme.raise : "transparent")
                Behavior on color { ColorAnimation { duration: Theme.fast } }
                HoverHandler { id: rowHover }
                Rectangle { visible: row.index === list.playingRow; width: 2; height: parent.height; color: Theme.accent }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (m) => {
                        if (m.button === Qt.RightButton) { rowMenu.popup(); return }
                        if (row.isRange && panel.mpv.loopA >= 0) panel.mpv.setLoop(row.time, row.end)
                        panel.mpv.paused = true; panel.mpv.seekTo(row.time, true)
                    }
                    onDoubleClicked: (m) => { if (m.button === Qt.LeftButton) row.edit() }
                }
                function edit() { editor.text = row.note; editor.visible = true; editor.forceActiveFocus(); editor.selectAll() }

                // kind
                AbstractButton {
                    id: kindBtn
                    x: 12; anchors.verticalCenter: parent.verticalCenter
                    width: 20; height: 20
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: true
                    onClicked: kindMenu.popup(kindBtn, 0, kindBtn.height + 2)
                    contentItem: Item { Rectangle { anchors.centerIn: parent; width: row.isRange ? 14 : 10; height: row.isRange ? 8 : 10; radius: row.isRange ? 2 : 5; color: Theme.kindColor(row.kind); border.width: kindBtn.hovered ? 2 : 0; border.color: Qt.lighter(Theme.kindColor(row.kind), 1.3) } }
                    background: Item {}
                    Tip { visible: kindBtn.hovered; text: Theme.kindName(row.kind) + ". Click to change" }
                    SMenu {
                        id: kindMenu
                        implicitWidth: 170
                        Repeater {
                            model: 4
                            delegate: SMenuItem {
                                required property int index
                                text: Theme.kindName(index); checkable: true; checked: row.kind === index
                                onTriggered: App.markers.setKind(row.index, index)
                            }
                        }
                    }
                }
                Column {
                    x: 40; anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 40 - 40
                    spacing: 3
                    Text {
                        visible: !editor.visible
                        width: parent.width
                        text: row.note || "Untitled"
                        color: row.note ? Theme.ink : Theme.ink3
                        font.pixelSize: Theme.fontBody; elide: Text.ElideRight
                    }
                    TextField {
                        id: editor
                        visible: false
                        width: parent.width
                        height: 24
                        leftPadding: 6; rightPadding: 6; topPadding: 2; bottomPadding: 2
                        font.pixelSize: Theme.fontBody
                        color: Theme.ink
                        selectByMouse: true
                        selectionColor: Theme.accentFill; selectedTextColor: "#ffffff"
                        background: Rectangle { color: Theme.well; border.color: Theme.accent; radius: 4 }
                        // Hide and give the keyboard back to the window, or shortcuts would keep typing into the hidden field.
                        function done(save) {
                            if (!visible) return
                            if (save) App.markers.setNote(row.index, text)
                            const had = activeFocus
                            visible = false   // re-entrant calls stop at the check above
                            focus = false     // or the focus scope would hand focus straight back
                            if (had) panel.win.releaseKeyboard()
                        }
                        onAccepted: done(true)
                        onActiveFocusChanged: if (!activeFocus) done(true)
                        Keys.onEscapePressed: done(false)
                    }
                    Row {
                        spacing: 8
                        Text { text: row.isRange ? panel.fmt(row.time) + " to " + panel.fmt(row.end) : panel.fmt(row.time); color: Theme.ink3; font.pixelSize: Theme.fontSmall }
                        Text { visible: row.isRange; text: App.formatClock(row.end - row.time) + (row.end - row.time < 60 ? " (" + (row.end - row.time).toFixed(1) + " s)" : ""); color: Theme.ink2; font.pixelSize: Theme.fontSmall }
                        Text { visible: row.kind !== 0 && !(row.isRange && row.kind === 3); text: Theme.kindName(row.kind); color: Theme.kindColor(row.kind); font.pixelSize: Theme.fontSmall; font.weight: Font.Medium }
                    }
                }
                Btn {
                    anchors.right: parent.right; anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: "more"; iconSize: 16; implicitWidth: 28; implicitHeight: 28
                    visible: rowHover.hovered || rowMenu.opened
                    tip: "More"
                    onClicked: rowMenu.popup(this, 0, height)
                }
                SMenu {
                    id: rowMenu
                    implicitWidth: 200
                    SMenuItem { text: "Edit note"; glyph: "transcript"; onTriggered: row.edit() }
                    SMenuItem { visible: row.isRange; height: visible ? implicitHeight : 0; text: "Play this select on a loop"; glyph: "loop"; onTriggered: { panel.mpv.setLoop(row.time, row.end); panel.mpv.seekTo(row.time, true); panel.mpv.paused = false } }
                    SMenuItem { visible: row.isRange; height: visible ? implicitHeight : 0; text: "Set In and Out to this select"; glyph: "in"; onTriggered: { App.inPoint = row.time; App.outPoint = row.end } }
                    SMenuItem { visible: row.isRange; height: visible ? implicitHeight : 0; text: "Export this select as a clip"; glyph: "scissors"; enabled: !App.busy; onTriggered: App.exportRanges([{ start: row.time, end: row.end, note: row.note }], true) }
                    SMenuItem { visible: !row.isRange; height: visible ? implicitHeight : 0; text: "Set In here"; glyph: "in"; onTriggered: App.inPoint = row.time }
                    SMenuItem { visible: !row.isRange; height: visible ? implicitHeight : 0; text: "Set Out here"; glyph: "out"; onTriggered: App.outPoint = row.time }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.line } }
                    SMenuItem { text: "Delete"; glyph: "trash"; onTriggered: App.markers.remove(row.index) }
                }
            }

            // empty state
            Column {
                anchors.centerIn: parent
                width: parent.width - 48
                spacing: 8
                visible: App.markers.count === 0
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "marker"; size: 28; color: Theme.ink3 }
                Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: "No markers"; color: Theme.ink2; font.pixelSize: Theme.fontBody }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    text: "Press M while reviewing to drop a note. Shift+M marks a good take, Alt+M a mistake. Press S at the start and end of a part worth keeping to make a select, then Ctrl+R sends your selects to DaVinci Resolve as a timeline."
                    color: Theme.ink3; font.pixelSize: Theme.fontLabel; lineHeight: 1.15
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

        // In / Out range
        Item {
            visible: App.inPoint >= 0 || App.outPoint >= 0
            Layout.fillWidth: true
            Layout.leftMargin: 2; Layout.rightMargin: 6; Layout.topMargin: 8
            implicitHeight: 40
            readonly property bool both: App.inPoint >= 0 && App.outPoint > App.inPoint
            Column {
                x: 12; anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    text: (App.inPoint >= 0 ? panel.fmt(App.inPoint) : "Start") + "  –  " + (App.outPoint >= 0 ? panel.fmt(App.outPoint) : "End")
                    color: Theme.ink; font.pixelSize: Theme.fontBody
                }
                Text {
                    text: parent.parent.both ? "In to Out  ·  " + App.formatClock(App.outPoint - App.inPoint) + " (" + (App.outPoint - App.inPoint).toFixed(2) + " s)"
                                             : (App.inPoint >= 0 ? "Press O to set the Out point" : "Press I to set the In point")
                    color: Theme.ink3; font.pixelSize: Theme.fontSmall
                }
            }
            Btn {
                anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter
                glyph: "close"; iconSize: 14; implicitWidth: 28; implicitHeight: 28
                tip: "Clear In and Out"; keys: "X"
                onClicked: panel.win.clearInOut()
            }
        }

        // export progress
        RowLayout {
            visible: App.busy
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 8; Layout.topMargin: 8
            spacing: 8
            Text { text: App.busyText || "Exporting"; color: Theme.ink2; font.pixelSize: 12 }
            Rectangle {
                Layout.fillWidth: true; height: 4; radius: 2; color: Theme.press
                Rectangle { width: parent.width * App.busyProgress; height: parent.height; radius: 2; color: Theme.accent }
            }
            Text { text: Math.round(App.busyProgress * 100) + "%"; color: Theme.ink2; font.pixelSize: Theme.fontLabel }
            Btn { glyph: "close"; iconSize: 15; implicitWidth: 26; implicitHeight: 26; tip: "Cancel export"; onClicked: App.cancelJob() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            Btn {
                Layout.fillWidth: true
                primary: true
                glyph: "resolve"
                text: "Send to Resolve"
                tip: "Build a DaVinci Resolve timeline from your selects, markers and ratings"
                keys: "Ctrl+R"
                onClicked: panel.win.openResolve()
            }
            Btn {
                id: exportMore
                flat: false
                glyph: "export"
                text: "Export"
                tip: "More exports"
                onClicked: exportMenu.popup(exportMore, exportMore.width - exportMenu.implicitWidth, -exportMenu.implicitHeight - 4)
                SMenu {
                    id: exportMenu
                    implicitWidth: 290
                    SMenuItem { text: "Clip from In to Out (lossless)"; glyph: "scissors"; keys: "Ctrl+E"; enabled: !App.busy && App.inPoint >= 0 && App.outPoint > App.inPoint; onTriggered: App.exportClip(true) }
                    SMenuItem { text: "Clip, frame-exact (re-encode)"; glyph: "scissors"; keys: "Ctrl+Shift+E"; enabled: !App.busy && App.inPoint >= 0 && App.outPoint > App.inPoint; onTriggered: App.exportClip(false) }
                    SMenuItem { text: "Every select as its own clip (lossless)"; glyph: "select"; enabled: !App.busy && App.markers.selectCount > 0; onTriggered: App.exportRanges(App.markers.selects(), true) }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.line } }
                    SMenuItem { text: "Timeline for DaVinci Resolve…"; glyph: "resolve"; keys: "Ctrl+R"; onTriggered: panel.win.openResolve() }
                    SMenuItem { text: "Markers for DaVinci Resolve (EDL)"; glyph: "film"; enabled: App.markers.count > 0; onTriggered: App.exportMarkersEdl(panel.mpv.fps) }
                    SMenuItem { text: "Markers as spreadsheet (CSV)"; glyph: "list"; enabled: App.markers.count > 0; onTriggered: App.exportMarkersCsv(panel.mpv.fps) }
                    SMenuItem { text: "Copy as YouTube chapters"; glyph: "copy"; enabled: App.markers.count > 0; onTriggered: App.copyYouTubeChapters(panel.mpv.duration) }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.line } }
                    SMenuItem { text: "Save current frame (PNG)"; glyph: "camera"; keys: "Ctrl+Shift+S"; enabled: panel.mpv.hasVideo; onTriggered: panel.win.grabFrame() }
                    SMenuItem { text: "Copy current frame"; glyph: "copy-frame"; keys: "Ctrl+C"; enabled: panel.mpv.hasVideo; onTriggered: panel.win.toast(panel.mpv.copyFrame() ? "Frame copied. Paste it anywhere" : "Couldn't copy this frame") }
                    SMenuItem { text: "Find thumbnail frames…"; glyph: "image"; keys: "Ctrl+T"; enabled: panel.mpv.hasVideo; onTriggered: panel.win.openThumbnails() }
                }
            }
        }
    }
}
