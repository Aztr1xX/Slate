import QtQuick
import Slate

// Brief feedback over the picture for things that have no other visible result:
// volume, speed, jumps, In and Out, ratings. Replaces itself when a new one arrives.
Rectangle {
    id: hud
    property string glyph: ""
    property string text: ""
    property real level: -1            // 0..1 draws a bar (volume); -1 hides it
    function show(icon, msg, lv) {
        glyph = icon || ""
        text = msg
        level = lv === undefined ? -1 : lv
        opacity = 1
        hideTimer.restart()
    }
    width: Math.max(row.implicitWidth + 28, level >= 0 ? 180 : 0)
    height: level >= 0 ? 52 : 38
    radius: 12
    color: "#e61b1d21"
    border.color: Qt.rgba(1, 1, 1, 0.08)
    opacity: 0
    visible: opacity > 0.01
    Behavior on opacity { NumberAnimation { duration: hud.opacity > 0.5 ? 220 : 90 } }

    Row {
        id: row
        anchors.horizontalCenter: parent.horizontalCenter
        y: hud.level >= 0 ? 9 : (parent.height - height) / 2
        spacing: 9
        Icon { visible: hud.glyph !== ""; name: hud.glyph; size: 18; color: Theme.ink; anchors.verticalCenter: parent.verticalCenter }
        Text { text: hud.text; color: Theme.ink; font.pixelSize: Theme.fontBody + 1; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
    }
    Rectangle {
        visible: hud.level >= 0
        x: 14; y: parent.height - 15
        width: parent.width - 28; height: 4; radius: 2
        color: Qt.rgba(1, 1, 1, 0.14)
        Rectangle { width: parent.width * Math.max(0, Math.min(1, hud.level)); height: parent.height; radius: 2; color: Theme.ink }
    }
    Timer { id: hideTimer; interval: 900; onTriggered: hud.opacity = 0 }
}
