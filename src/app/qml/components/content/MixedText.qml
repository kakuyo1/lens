pragma ComponentBehavior: Bound

import QtQuick

/**
 * A single-line value whose CJK glyphs use the UI face while Latin, digits and punctuation keep
 * the numeric face. This avoids letting one explicit family decide the fallback for the whole run.
 */
Item {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property string value: ""
    property int pixelSize: 13
    property int weight: Font.Normal
    property color color: "black"
    readonly property var runs: splitRuns(value)

    /// The line every run sits on. The runs' own ascents differ (the UI face's CJK glyphs and the numeric face's
    /// digits are not the same height), so runs that are only top-aligned put the CJK glyphs lower than the digits.
    readonly property real sharedBaseline: {
        let line = 0;
        for (let i = 0; i < runRepeater.count; ++i) {
            const run = runRepeater.itemAt(i);
            if (run !== null)
                line = Math.max(line, run.baselineOffset);
        }
        return line;
    }
    /// How far the deepest run reaches below the shared line.
    readonly property real belowBaseline: {
        let below = 0;
        for (let i = 0; i < runRepeater.count; ++i) {
            const run = runRepeater.itemAt(i);
            if (run !== null)
                below = Math.max(below, run.height - run.baselineOffset);
        }
        return below;
    }

    /// @return The x where run @p index starts: the widths of the runs before it, laid end to end.
    function offsetOf(index) {
        let x = 0;
        for (let i = 0; i < index; ++i) {
            const run = runRepeater.itemAt(i);
            if (run !== null)
                x += run.implicitWidth;
        }
        return x;
    }

    implicitWidth: offsetOf(runRepeater.count)
    implicitHeight: sharedBaseline + belowBaseline
    width: implicitWidth
    height: implicitHeight

    function splitRuns(source) {
        const result = [];
        let current = "";
        let cjk = false;
        for (let i = 0; i < source.length; ++i) {
            const character = source.charAt(i);
            // Code units, not a regex: a regex literal inside a loop body makes qmllint
            // 6.9.0 consume memory until the machine dies, whatever the pattern says.
            const code = character.charCodeAt(0);
            const nextCjk = code >= 0x3400 && code <= 0x9fff;
            if (current !== "" && nextCjk !== cjk) {
                result.push({ text: current, cjk: cjk });
                current = "";
            }
            current += character;
            cjk = nextCjk;
        }
        if (current !== "")
            result.push({ text: current, cjk: cjk });
        return result;
    }

    Repeater {
        id: runRepeater
        model: root.runs

        Text {
            required property var modelData
            required property int index
            x: root.offsetOf(index)
            y: root.sharedBaseline - baselineOffset
            text: modelData.text
            color: root.color
            font.pixelSize: root.pixelSize
            font.weight: root.weight
            font.family: modelData.cjk ? "" : root.tokens.monoFamily
        }
    }
}
