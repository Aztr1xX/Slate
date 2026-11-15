import QtQuick
import Slate

// Short status message near the bottom of the picture, with an optional "Show in folder".
Rectangle {
    id: t
    property string revealPath: ""
    function show(msg, path) {
        label.text = msg
        revealPath = path || ""
        opacity = 1
        hideTimer.restart()
    }
    property real maxWidth: 640
    width: Math.min(maxWidth, row.implicitWidth + 28)
    height: 36
    radius: 18
    color: "#ee1f2226"
    border.color: Theme.lineStrong
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 160 } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 14
        Text {
            id: label
            width: Math.min(implicitWidth, t.maxWidth - 28 - (reveal.visible ? reveal.implicitWidth + row.spacing : 0))
            elide: Text.ElideMiddle
            color: Theme.ink; font.pixelSize: Theme.fontBody; anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: reveal
            visible: t.revealPath.length > 0
            text: App.isWindows ? "Show in Explorer" : "Show in folder"
            color: revealMouse.containsMouse ? Theme.accentHover : Theme.accent
            font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
            MouseArea { id: revealMouse; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: App.revealInFolder(t.revealPath) }
        }
    }
    HoverHandler { onHoveredChanged: hovered ? hideTimer.stop() : hideTimer.restart() }
    Timer { id: hideTimer; interval: 3000; onTriggered: t.opacity = 0 }
}
