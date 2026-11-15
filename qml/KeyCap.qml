import QtQuick
import Slate

// A keyboard shortcut drawn as a small key: "Ctrl K".
Rectangle {
    id: kc
    property string text: ""
    property bool strong: false
    visible: text.length > 0
    implicitWidth: kt.implicitWidth + 10
    implicitHeight: 18
    radius: 4
    color: Theme.bg
    border.color: Theme.lineStrong
    Text {
        id: kt
        anchors.centerIn: parent
        text: kc.text
        color: kc.strong ? Theme.ink : Theme.ink2
        font.pixelSize: Theme.fontTiny + 1
    }
}
