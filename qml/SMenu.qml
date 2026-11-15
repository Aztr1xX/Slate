import QtQuick
import QtQuick.Controls
import Slate

// Dark context / drop-down menu.
Menu {
    id: m
    padding: 5
    implicitWidth: 260
    delegate: SMenuItem {}
    background: Rectangle { implicitWidth: 220; color: Theme.popup; radius: Theme.radiusMd; border.color: Theme.lineStrong }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.normal; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.fast } }
}
