import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Slate

// A labelled drop-down: "Order        Best rated first ▾". options: [{ value, text, note? }]
Item {
    id: c
    property string label: ""
    property var options: []
    property var value
    property string tip: ""
    signal picked(var value)
    implicitHeight: 32
    implicitWidth: 260
    Layout.fillWidth: true
    readonly property var current: { for (const o of options) if (o.value === value) return o; return options.length ? options[0] : null }

    Text {
        id: lab
        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, parent.width * 0.45)
        text: c.label; color: Theme.ink2; font.pixelSize: Theme.fontLabel; elide: Text.ElideRight
    }
    AbstractButton {
        id: btn
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        width: Math.min(parent.width - lab.width - 10, Math.max(120, row.implicitWidth + 22))
        height: 28
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        onClicked: menu.popup(btn, btn.width - menu.implicitWidth, btn.height + 2)
        background: Rectangle {
            radius: Theme.radius
            color: btn.down || menu.opened ? Theme.press : (btn.hovered ? Theme.hover : Theme.raise)
            border.color: menu.opened ? Theme.accent : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.fast } }
        }
        contentItem: Item {
            Row {
                id: row
                anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Text {
                    width: Math.min(implicitWidth, btn.width - 32)
                    text: c.current ? c.current.text : ""
                    color: Theme.ink; font.pixelSize: Theme.fontLabel; font.weight: Font.Medium
                    elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter
                }
                Icon { name: "chevron-down"; size: 14; color: Theme.ink2; anchors.verticalCenter: parent.verticalCenter }
            }
        }
        Tip { visible: btn.hovered && c.tip !== "" && !menu.opened; text: c.tip }
    }
    SMenu {
        id: menu
        implicitWidth: Math.max(btn.width, 230)
        Repeater {
            model: c.options
            delegate: SMenuItem {
                required property var modelData
                text: modelData.text
                checkable: true
                checked: modelData.value === c.value
                onTriggered: { c.value = modelData.value; c.picked(modelData.value) }
            }
        }
    }
}
