import QtQuick

// Mouse wheel scrolling at a normal speed. Qt's Flickable turns each wheel notch into a small,
// slow flick on Windows; this moves a fixed distance per notch instead (touchpads keep their own
// pixel-precise deltas). Put one inside any Flickable or ListView.
WheelHandler {
    id: w
    // the Flickable this sits in (handlers inside a Flickable end up on its contentItem)
    readonly property Flickable flick: {
        let p = w.parent
        while (p && !(p instanceof Flickable)) p = p.parent
        return p
    }
    property real step: 96                 // pixels per wheel notch
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    target: null
    onWheel: (event) => {
        const f = w.flick
        if (!f) return
        const max = Math.max(0, f.contentHeight - f.height)
        if (max <= 0) return
        const dy = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 120 * w.step
        f.contentY = Math.max(f.originY, Math.min(f.originY + max, f.contentY - dy))
        event.accepted = true
    }
}
