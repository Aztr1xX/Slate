import QtQuick
import QtQuick.Controls
import Slate

// Thin scrollbar that only shows while scrolling or hovered.
ScrollBar {
    id: sb
    policy: ScrollBar.AsNeeded
    padding: 2
    contentItem: Rectangle {
        implicitWidth: sb.hovered || sb.pressed ? 6 : 4
        implicitHeight: implicitWidth
        radius: width / 2
        color: sb.pressed ? Theme.ink3 : (sb.hovered ? Theme.ink4 : Qt.rgba(1, 1, 1, 0.16))
        opacity: sb.active || sb.hovered ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.normal } }
        Behavior on implicitWidth { NumberAnimation { duration: Theme.fast } }
    }
    background: Item {}
}
