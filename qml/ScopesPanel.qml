import QtQuick
import QtQuick.Layouts
import Slate

// Waveform, RGB parade, histogram and vectorscope for checking exposure and colour.
Item {
    id: panel
    required property MpvObject mpv
    property bool shown: false
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Item { Layout.preferredHeight: 12 }
        Segmented {
            id: seg
            Layout.fillWidth: true
            Layout.leftMargin: 12; Layout.rightMargin: 12
            options: [{ text: "Waveform" }, { text: "Parade" }, { text: "Histogram" }, { text: "Vector" }]
            current: App.setting("scopeMode", 0)
            onPicked: (i) => App.setSetting("scopeMode", i)
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: width * (seg.current === 3 ? 1 : 0.62)
            Layout.margins: 12
            radius: Theme.radius
            color: Theme.well
            clip: true
            ScopeItem {
                anchors.fill: parent; anchors.margins: 1
                source: panel.mpv
                mode: seg.current
                active: panel.shown && panel.mpv.hasVideo
            }
            Text { anchors.centerIn: parent; visible: !panel.mpv.hasVideo; text: "No video"; color: Theme.ink3; font.pixelSize: 12 }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 14; Layout.rightMargin: 14
            wrapMode: Text.WordWrap
            color: Theme.ink3; font.pixelSize: 11
            text: [
                "Brightness across the picture. Detail near 100 is clipping to white; near 0, crushed to black.",
                "Red, green and blue levels side by side. Matching heights on something white or grey means neutral white balance.",
                "How many pixels sit at each level. A pile at either edge means lost highlight or shadow detail.",
                "Colour saturation and hue. Skin tones of every complexion fall near the orange line."
            ][seg.current]
        }

        Item { Layout.fillHeight: true }
    }
}
