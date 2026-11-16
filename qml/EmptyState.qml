import QtQuick
import QtQuick.Layouts
import Slate

// First screen: open buttons and recent files.
Item {
    id: es
    required property var win
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(560, parent.width - 48)
        spacing: 0
        Image { Layout.alignment: Qt.AlignHCenter; source: "qrc:/Slate/assets/slate.png"; sourceSize: Qt.size(72, 72); width: 72; height: 72 }
        Text { Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 16; text: "Open something to review"; color: Theme.ink; font.pixelSize: Theme.fontDisplay; font.weight: Font.DemiBold }
        Text { Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 6; text: "Drop a clip, audio file or a whole folder anywhere in this window."; color: Theme.ink3; font.pixelSize: Theme.fontBody }
        Row {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 24
            spacing: 8
            Btn { primary: true; glyph: "file"; text: "Open file"; keys: "Ctrl+O"; tip: "Open a video or audio file"; implicitHeight: 34; onClicked: es.win.openFileDialog() }
            Btn { flat: false; glyph: "open"; text: "Open folder"; keys: "Ctrl+Shift+O"; tip: "Review every clip in a folder"; implicitHeight: 34; onClicked: es.win.openFolderDialog() }
            Btn { flat: false; glyph: "download"; text: "Download video"; keys: "Ctrl+D"; tip: "Download from YouTube at the best quality"; implicitHeight: 34; onClicked: es.win.openDownloads() }
        }

        // the handful of keys that make reviewing fast
        Row {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 22
            visible: implicitWidth <= es.width - 48
            spacing: 16
            Repeater {
                model: [["Space", "Play"], [", .", "Step frames"], ["J K L", "Shuttle"], ["M", "Marker"], ["I O", "In and Out"]]
                delegate: Row {
                    required property var modelData
                    spacing: 6
                    KeyCap { text: modelData[0]; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: modelData[1]; color: Theme.ink3; font.pixelSize: Theme.fontLabel; anchors.verticalCenter: parent.verticalCenter }
                }
            }
        }

        RowLayout {
            visible: App.recentFiles.length > 0
            Layout.fillWidth: true
            Layout.topMargin: 36
            Text { text: "Recent"; color: Theme.ink2; font.pixelSize: Theme.fontLabel; font.weight: Font.Medium }
            Item { Layout.fillWidth: true }
            Text {
                text: "Clear"; color: clearMa.containsMouse ? Theme.ink : Theme.ink3; font.pixelSize: Theme.fontLabel
                MouseArea { id: clearMa; anchors.fill: parent; anchors.margins: -4; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: App.clearRecent() }
            }
        }
        Rectangle { visible: App.recentFiles.length > 0; Layout.fillWidth: true; Layout.topMargin: 8; height: 1; color: Theme.line }
        // A plain Column holds the list: rows coming and going straight inside a ColumnLayout can
        // crash Qt's layout code when the window resizes at the same moment (opening a file, then full screen).
        Column {
            Layout.fillWidth: true
        Repeater {
            model: es.visible ? App.recentFiles.slice(0, 7) : []
            delegate: Rectangle {
                id: rr
                required property string modelData
                readonly property string fname: modelData.split(/[\\/]/).pop()
                readonly property string folder: modelData.substring(0, modelData.length - fname.length - 1)
                width: parent ? parent.width : 0
                height: 44; radius: Theme.radius
                color: rh.hovered ? Theme.raise : "transparent"
                HoverHandler { id: rh }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: es.win.openMedia(rr.modelData) }
                Icon { x: 10; anchors.verticalCenter: parent.verticalCenter; name: /\.(mp3|wav|flac|m4a|aac|ogg|opus|aiff?|wma)$/i.test(rr.modelData) ? "music" : "film"; size: 18; color: Theme.ink3 }
                Column {
                    x: 40; anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 40 - 44
                    spacing: 2
                    Text { width: parent.width; text: rr.fname; elide: Text.ElideMiddle; color: Theme.ink; font.pixelSize: Theme.fontBody }
                    Text { width: parent.width; text: rr.folder; elide: Text.ElideMiddle; color: Theme.ink3; font.pixelSize: Theme.fontSmall }
                }
                Btn {
                    anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter
                    visible: rh.hovered
                    glyph: "close"; iconSize: 14; implicitWidth: 26; implicitHeight: 26
                    tip: "Remove from recent"
                    onClicked: App.removeRecent(rr.modelData)
                }
            }
        }
        }
    }
}
