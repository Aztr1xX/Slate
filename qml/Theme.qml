pragma Singleton
import QtQuick
import Slate

// One place for colours, sizes, type and motion.
// Cool slate greys taken from the app icon, with one indigo accent from the icon's blue-to-violet,
// so the picture and the marker colours are always the most colourful things on screen.
QtObject {
    // surfaces, darkest to lightest
    readonly property color bg: "#0b0c0f"        // around the picture
    readonly property color chrome: "#121419"    // header, side panel, timeline and transport: one surface
    readonly property color panel: "#121419"
    readonly property color well: "#0e1014"      // sunken areas: timeline track, scopes, segmented controls
    readonly property color raise: "#191c22"     // inputs, cards
    readonly property color hover: "#1f232a"
    readonly property color press: "#262a33"
    readonly property color line: "#20242b"      // hairlines between areas
    readonly property color lineStrong: "#2e333e"
    readonly property color popup: "#181b21"     // menus, tooltips, palette, dialogs
    readonly property color scrim: Qt.rgba(0.04, 0.05, 0.06, 0.62)   // dims the window behind dialogs, chips over the picture
    readonly property color shadow: Qt.rgba(0, 0, 0, 0.45)

    // text
    readonly property color ink: "#e7e9ee"
    readonly property color ink2: "#a6acb8"
    readonly property color ink3: "#8b919d"      // secondary text: at least 4.5:1 on every surface
    readonly property color ink4: "#4a505c"      // decoration only (empty stars, idle ticks), never text

    // accent: solid, never a gradient
    readonly property color accent: "#6b7bff"      // lines, selection edges, focus, active tab
    readonly property color accentHover: "#8290ff"
    readonly property color accentFill: "#5a66f0"  // filled buttons, so white text stays readable
    readonly property color accentFillHover: "#6672f5"
    readonly property color accentText: "#9aa5ff"  // accent-coloured text on dark surfaces
    readonly property color accentInk: "#ffffff"
    readonly property color accentSoft: Qt.rgba(0.42, 0.48, 1, 0.15)

    // meaning
    readonly property color good: "#3ec27e"
    readonly property color bad: "#f0575c"
    readonly property color note: "#e8b54a"
    readonly property color warn: "#e8b54a"

    // type: Slate Inter everywhere, figures are tabular so numbers line up
    readonly property string mono: Qt.application.font.family
    readonly property int fontTiny: 10           // the smallest text anywhere
    readonly property int fontSmall: 11
    readonly property int fontLabel: 12
    readonly property int fontBody: 13
    readonly property int fontTitle: 15
    readonly property int fontDisplay: 20

    // shape and space
    readonly property int radius: 6              // controls
    readonly property int radiusMd: 8            // menus, cards
    readonly property int radiusLg: 10           // dialogs
    readonly property int gap: 8
    readonly property int pad: 12
    readonly property int padLg: 16

    // motion; Windows' "Animation effects" switch turns it all off
    readonly property bool motion: App.animationsEnabled
    readonly property int fast: motion ? 110 : 0
    readonly property int normal: motion ? 160 : 0

    function kindColor(k) { return k === 1 ? good : (k === 2 ? bad : note) }
    function kindName(k) { return k === 1 ? "Good take" : (k === 2 ? "Mistake" : "Note") }
}
