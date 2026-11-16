import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Slate

// Live peak meter for the audio playing now, left/right listening, the clip's loudness, and the
// music tools: tempo (detect, tap, type, halve / double), first beat, beat grid and beat markers.
// Kept apart from the scopes so it costs almost nothing while a clip plays.
Item {
    id: panel
    required property MpvObject mpv
    required property var win
    property bool shown: false
    readonly property bool hasTempo: Tempo.bpm > 0 && Tempo.file === App.currentFile
    property string every: App.setting("beatEvery", "bar")   // beat, bar, 2bars, 4bars
    readonly property int everyBeats: ({ beat: 1, bar: 1, "2bars": 2, "4bars": 4 })[every] * (every === "beat" ? 1 : Tempo.beatsPerBar)
    property bool inOutOnly: App.setting("beatInOut", false)
    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        WheelScroll {}
        ScrollBar.vertical: SScrollBar {}
    ColumnLayout {
        id: col
        width: parent.width
        spacing: 0
        // ---- audio meter -----------------------------------------------------------
        Item { Layout.preferredHeight: 12 }
        Segmented {
            visible: meter.channels >= 2
            Layout.fillWidth: true
            Layout.leftMargin: 10; Layout.rightMargin: 10; Layout.bottomMargin: 12
            options: [{ text: "Both channels" }, { text: "Left only" }, { text: "Right only" }]
            current: panel.mpv.listen
            onPicked: (i) => panel.mpv.listen = i
        }
        Item {
            id: meter
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14
            Layout.preferredHeight: channels >= 2 ? 50 : 34
            property var levels: []
            property var holds: [-120, -120]
            readonly property int channels: Math.min(2, (panel.mpv.info.channels || 0))
            readonly property real floorDb: -60
            function frac(db) { return Math.max(0, Math.min(1, (db - floorDb) / -floorDb)) }
            Timer {
                interval: 50; repeat: true
                running: panel.shown && panel.mpv.loaded && !panel.mpv.paused
                onTriggered: {
                    const l = panel.mpv.audioLevels()
                    const h = meter.holds.slice()
                    for (let i = 0; i < 2; ++i) {
                        const v = l.length > i ? l[i] : (l.length ? l[0] : -120)
                        h[i] = v > h[i] ? v : h[i] - 0.6
                    }
                    meter.levels = l; meter.holds = h
                }
                onRunningChanged: if (!running) { meter.levels = []; meter.holds = [-120, -120] }
            }
            Column {
                width: parent.width
                spacing: 6
                Repeater {
                    model: meter.channels >= 2 ? 2 : 1
                    delegate: Row {
                        required property int index
                        spacing: 8
                        Text { width: 10; text: meter.channels >= 2 ? (index === 0 ? "L" : "R") : "M"; color: Theme.ink3; font.pixelSize: 10; font.family: Theme.mono; anchors.verticalCenter: parent.verticalCenter }
                        Rectangle {
                            id: track
                            width: meter.width - 18; height: 8; radius: 4
                            color: Theme.press
                            readonly property real lv: meter.levels.length > index ? meter.levels[index] : (meter.levels.length ? meter.levels[0] : -120)
                            Rectangle {
                                width: parent.width * meter.frac(track.lv); height: parent.height; radius: 3
                                color: track.lv > -1 ? Theme.bad : (track.lv > -6 ? Theme.note : Theme.accent)
                                Behavior on width { NumberAnimation { duration: 60 } }
                            }
                            Rectangle {
                                visible: meter.holds[index] > meter.floorDb
                                x: parent.width * meter.frac(meter.holds[index]) - 1
                                width: 2; height: parent.height; color: Theme.ink2
                            }
                        }
                    }
                }
                // scale
                Item {
                    width: meter.width; height: 12
                    Repeater {
                        model: [-60, -40, -20, -12, -6, 0]
                        delegate: Text {
                            required property int modelData
                            x: 18 + (meter.width - 18) * meter.frac(modelData) - implicitWidth / 2
                            text: modelData; color: Theme.ink3; font.pixelSize: Theme.fontTiny
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14; Layout.topMargin: 10
            visible: meter.channels >= 2
            wrapMode: Text.WordWrap
            color: Theme.ink3; font.pixelSize: 11
            text: panel.mpv.listen === 0 ? "Peak level in dBFS. Listen to one side to check each mic on a two-channel recording."
                                         : "Playing only the " + (panel.mpv.listen === 1 ? "left" : "right") + " channel, through both speakers."
        }

        Rectangle { Layout.fillWidth: true; Layout.topMargin: 16; Layout.bottomMargin: 8; height: 1; color: Theme.line }
        Text { Layout.leftMargin: 14; text: "Whole clip"; color: Theme.ink2; font.pixelSize: Theme.fontLabel; font.weight: Font.Medium }
        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14; Layout.topMargin: 6
            columns: 2; rowSpacing: 6; columnSpacing: 10
            readonly property var lo: Media.loudness
            Text { text: "Loudness"; color: Theme.ink2; font.pixelSize: 12; Layout.preferredWidth: 96 }
            Text { text: parent.lo.integrated !== undefined ? parent.lo.integrated.toFixed(1) + " LUFS" : (parent.lo.measuring ? "Measuring…" : "–"); color: Theme.ink; font.pixelSize: 12 }
            Text { text: "True peak"; color: Theme.ink2; font.pixelSize: 12 }
            Text { text: parent.lo.truePeak !== undefined ? parent.lo.truePeak.toFixed(1) + " dBTP" : "–"; color: parent.lo.truePeak > -1 ? Theme.bad : Theme.ink; font.pixelSize: 12 }
            Text { text: "Range"; color: Theme.ink2; font.pixelSize: 12 }
            Text { text: parent.lo.range !== undefined ? parent.lo.range.toFixed(1) + " LU" : "–"; color: Theme.ink; font.pixelSize: 12 }
        }

        // ---- music -------------------------------------------------------------------------
        Rectangle { Layout.fillWidth: true; Layout.topMargin: 16; Layout.bottomMargin: 8; height: 1; color: Theme.line }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 10
            Text { Layout.fillWidth: true; text: "Music"; color: Theme.ink2; font.pixelSize: Theme.fontLabel; font.weight: Font.Medium }
            Btn {
                visible: panel.hasTempo
                glyph: "trash"; tip: "Forget this tempo"
                onClicked: Tempo.clear()
            }
        }
        // tempo readout
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14; Layout.topMargin: 4
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 112; Layout.preferredHeight: 56
                radius: Theme.radius
                color: Theme.raise; border.color: bpmField.activeFocus ? Theme.accent : Theme.line
                TextField {
                    id: bpmField
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 6
                    horizontalAlignment: Text.AlignHCenter
                    text: Tempo.busy ? "…" : (panel.hasTempo ? String(Math.round(Tempo.bpm * 10) / 10) : "–")
                    color: Theme.ink; font.pixelSize: 22; font.family: Theme.mono
                    background: null
                    selectByMouse: true
                    validator: DoubleValidator { bottom: 20; top: 400; decimals: 2 }
                    onAccepted: { Tempo.setBpm(Number(text), "typed"); focus = false }
                    Keys.onEscapePressed: { focus = false }
                    onActiveFocusChanged: if (activeFocus) selectAll()
                    Tip { visible: bpmField.hovered && !bpmField.activeFocus; text: "Click to type a BPM"; delay: 400 }
                }
                Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 6; text: "BPM"; color: Theme.ink3; font.pixelSize: Theme.fontTiny }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: Tempo.busy ? "Listening for the beat…"
                          : Tempo.error && !panel.hasTempo ? Tempo.error
                          : !panel.hasTempo ? "Find the tempo, tap along (B) or type it."
                          : Tempo.source === "detected" ? (Tempo.confidence > 0.6 ? "Detected. Steady beat" : (Tempo.confidence > 0.3 ? "Detected. Check it by ear" : "Detected, but the beat is loose"))
                          : Tempo.source === "tapped" ? "Tapped" : "Typed"
                    color: Tempo.error && !panel.hasTempo ? Theme.bad : Theme.ink3; font.pixelSize: 11
                }
                RowLayout {
                    spacing: 4
                    Btn {
                        text: panel.hasTempo ? "" : "Find tempo"
                        glyph: panel.hasTempo ? "loop" : "metronome"
                        tip: panel.hasTempo ? "Listen again" : ""
                        primary: !panel.hasTempo
                        flat: panel.hasTempo
                        enabled: panel.mpv.loaded && !Tempo.busy && App.currentFile !== ""
                        onClicked: Tempo.detect(App.currentFile)
                    }
                    Btn { visible: panel.hasTempo; text: "½"; tip: "Half the tempo"; onClicked: Tempo.scale(0.5) }
                    Btn { visible: panel.hasTempo; text: "×2"; tip: "Double the tempo"; onClicked: Tempo.scale(2) }
                    Btn { text: "Tap"; tip: "Tap along with the beat"; keys: "B"; flat: false; enabled: panel.mpv.loaded; onClicked: panel.win.tapTempo() }
                }
            }
        }
        // grid options
        ColumnLayout {
            visible: panel.hasTempo
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14; Layout.topMargin: 10
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: "First beat at " + panel.win.fmt(Tempo.firstBeat); color: Theme.ink2; font.pixelSize: 12; elide: Text.ElideRight }
                Btn { text: "Here"; glyph: "marker"; tip: "Put a beat where the playhead is"; onClicked: Tempo.setFirstBeat(panel.mpv.position) }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: "Beats in a bar"; color: Theme.ink2; font.pixelSize: 12 }
                Segmented {
                    itemWidth: 38
                    options: [{ text: "3" }, { text: "4" }, { text: "6" }]
                    current: Tempo.beatsPerBar === 3 ? 0 : (Tempo.beatsPerBar === 6 ? 2 : 1)
                    onPicked: i => Tempo.setBeatsPerBar([3, 4, 6][i])
                }
            }
            SSwitch {
                Layout.fillWidth: true; implicitHeight: 30; leftPadding: 0
                text: "Beat grid on the timeline"
                checked: panel.win.beatGridOn
                onToggled: panel.win.beatGridOn = checked
                Tip { visible: parent.hovered; text: "Lines on every beat. Markers, In, Out and select edges snap to them"; keys: "Shift+B"; delay: 400 }
            }
            Rectangle { Layout.fillWidth: true; Layout.topMargin: 6; Layout.bottomMargin: 6; height: 1; color: Theme.line; opacity: 0.6 }
            Choice {
                label: "Beat markers every"
                options: [{ value: "beat", text: "Beat" }, { value: "bar", text: "Bar" }, { value: "2bars", text: "2 bars" }, { value: "4bars", text: "4 bars" }]
                value: panel.every
                onPicked: v => { panel.every = v; App.setSetting("beatEvery", v) }
            }
            SSwitch {
                Layout.fillWidth: true; implicitHeight: 30; leftPadding: 0
                text: "Only between In and Out"
                enabled: App.inPoint >= 0 || App.outPoint >= 0
                checked: panel.inOutOnly
                onToggled: { panel.inOutOnly = checked; App.setSetting("beatInOut", checked) }
            }
            Btn {
                Layout.fillWidth: true
                text: "Add beat markers"; glyph: "plus"; flat: false
                onClicked: panel.win.addBeatMarkers(panel.everyBeats, panel.inOutOnly && (App.inPoint >= 0 || App.outPoint >= 0))
            }
        }
    }
    }
    Connections {
        target: panel.mpv
        function onVideoInfoChanged() { if ((panel.mpv.info.channels || 0) < 2 && panel.mpv.listen !== 0) panel.mpv.listen = 0 }
    }
}
