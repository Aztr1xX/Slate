import QtQuick
import QtQuick.Controls
import Slate

// Tooltip with an optional shortcut badge.
ToolTip {
    id: tip
    property string keys: ""
    delay: 450
    timeout: 6000
    padding: 0
    contentItem: Row {
        spacing: 8
        leftPadding: 9; rightPadding: 9; topPadding: 5; bottomPadding: 5
        Text { text: tip.text; color: Theme.ink; font.pixelSize: Theme.fontLabel; anchors.verticalCenter: parent.verticalCenter }
        KeyCap { text: tip.keys; anchors.verticalCenter: parent.verticalCenter }
    }
    background: Rectangle { color: Theme.popup; radius: Theme.radius; border.color: Theme.lineStrong }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast } }
}
