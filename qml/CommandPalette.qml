import QtQuick
import QtQuick.Controls
import Slate

// Ctrl+K: find and run any action by typing part of its name.
Popup {
    id: pal
    property var actions: []      // [{ name, keys, run, category }]
    property var shown: []
    property int sel: 0

    parent: Overlay.overlay
    modal: true
    focus: true
    dim: true
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.12)
    width: Math.min(600, parent.width - 40)
    height: 62 + Math.max(40, Math.min(10, pal.shown.length) * 36) + 8
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Theme.scrim }
    enter: Transition { ParallelAnimation { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Theme.normal; easing.type: Easing.OutCubic } } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast } }

    // Recently run actions come first; then names that start with what you typed.
    property var recent: String(App.setting("paletteRecent", "")).split("|").filter(x => x)
    function refilter() {
        const words = input.text.toLowerCase().trim().split(/\s+/).filter(w => w.length)
        const scored = []
        for (let i = 0; i < actions.length; ++i) {
            const a = actions[i]
            const name = a.name.toLowerCase()
            const hay = name + " " + (a.category || "").toLowerCase() + " " + (a.keys || "").toLowerCase()
            if (!words.every(w => hay.indexOf(w) >= 0)) continue
            const r = recent.indexOf(a.id)
            let score = r >= 0 ? 100 - r : 0
            if (words.length) {
                if (name.startsWith(words[0])) score += 50
                else if (name.indexOf(" " + words[0]) >= 0) score += 20
            }
            scored.push({ a: a, score: score, order: i })
        }
        scored.sort((x, y) => y.score - x.score || x.order - y.order)
        shown = scored.map(x => x.a)
        sel = 0
    }
    function runSelected() {
        const a = shown[sel]
        close()
        if (!a) return
        recent = [a.id].concat(recent.filter(x => x !== a.id)).slice(0, 6)
        App.setSetting("paletteRecent", recent.join("|"))
        Qt.callLater(a.run)
    }
    onOpened: { input.text = ""; refilter(); input.forceActiveFocus() }

    background: Rectangle { color: Theme.popup; border.color: Theme.lineStrong; radius: Theme.radiusLg }

    Icon { x: 16; y: 16; name: "search"; size: 18; color: Theme.ink3 }
    TextField {
        id: input
        x: 40; y: 0; width: parent.width - 40; height: 50
        leftPadding: 6
        placeholderText: "Search actions"
        placeholderTextColor: Theme.ink3
        color: Theme.ink
        font.pixelSize: Theme.fontTitle
        selectByMouse: true
        selectionColor: Theme.accent
        background: Item {}
        onTextChanged: pal.refilter()
        Keys.onDownPressed: pal.sel = Math.min(pal.shown.length - 1, pal.sel + 1)
        Keys.onUpPressed: pal.sel = Math.max(0, pal.sel - 1)
        Keys.onReturnPressed: pal.runSelected()
        Keys.onEnterPressed: pal.runSelected()
    }
    Rectangle { y: 50; width: parent.width; height: 1; color: Theme.line }

    ListView {
        id: list
        x: 6; y: 56
        width: parent.width - 12
        height: parent.height - 62
        clip: true
        model: pal.shown
        currentIndex: pal.sel
        boundsBehavior: Flickable.StopAtBounds
        WheelScroll {}
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        delegate: Rectangle {
            required property var modelData
            required property int index
            width: list.width; height: 36; radius: Theme.radius
            color: index === pal.sel ? Theme.hover : "transparent"
            Row {
                x: 12; anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Text { text: modelData.name; color: Theme.ink; font.pixelSize: Theme.fontBody; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    visible: input.text === "" && pal.recent.indexOf(modelData.id) >= 0
                    text: "Recent"; color: Theme.ink3; font.pixelSize: Theme.fontTiny; anchors.verticalCenter: parent.verticalCenter
                }
            }
            Row {
                anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Text { text: modelData.category || ""; color: Theme.ink3; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                KeyCap { text: modelData.keys; anchors.verticalCenter: parent.verticalCenter; implicitHeight: 20 }
            }
            MouseArea { anchors.fill: parent; hoverEnabled: true; onPositionChanged: pal.sel = index; onClicked: { pal.sel = index; pal.runSelected() } }
        }
        Text { visible: pal.shown.length === 0; x: 12; y: 10; text: "No matching action"; color: Theme.ink3; font.pixelSize: Theme.fontBody }
    }
}
