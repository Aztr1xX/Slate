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
        Text { text: tip.text; color: Theme.ink; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
        KeyCap { text: tip.keys; anchors.verticalCenter: parent.verticalCenter }
    }
    background: Rectangle { color: Theme.popup; radius: 6; border.color: Theme.lineStrong }
}
