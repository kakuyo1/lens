pragma ComponentBehavior: Bound

import QtQuick

/**
 * The landing particles of a scene entry (ADR 0101): a handful of canvas-pixel droplets for a splash, or dust for
 * a box or a hole. The controller owns the timing; this item only draws `progress` over a fixed burst. QML draws
 * these rectangles rather than sprites, so no scene or pet art has to carry an effect sheet.
 */
Item {
    id: root

    /// The controller's `impact` map: `{kind, at, progress}`, or empty when nothing is landing.
    property var impact: ({})

    readonly property real progress: impact && impact.progress !== undefined ? impact.progress : 0
    readonly property point at: impact && impact.at !== undefined ? impact.at : Qt.point(0, 0)
    readonly property bool on: impact && impact.kind !== undefined && impact.kind !== "" && progress < 1

    /// One entry per droplet: `dx` fans it out, `spread` lifts it, `size` is in canvas pixels, `color` is the pixel.
    readonly property var splashParticles: [
        { dx: -18, spread: 2, size: 1, color: "#d9ecec" },
        { dx: -16, spread: 0, size: 2, color: "#f7fbf8" },
        { dx: -13, spread: 1, size: 1, color: "#9cc8d4" },
        { dx: -11, spread: 2, size: 1, color: "#f7fbf8" },
        { dx: -9, spread: 0, size: 2, color: "#d9ecec" },
        { dx: -7, spread: 1, size: 1, color: "#9cc8d4" },
        { dx: -5, spread: 2, size: 1, color: "#f7fbf8" },
        { dx: -3, spread: 0, size: 2, color: "#d9ecec" },
        { dx: 0, spread: 1, size: 1, color: "#f7fbf8" },
        { dx: 2, spread: 2, size: 2, color: "#9cc8d4" },
        { dx: 4, spread: 0, size: 1, color: "#d9ecec" },
        { dx: 6, spread: 1, size: 1, color: "#f7fbf8" },
        { dx: 8, spread: 2, size: 2, color: "#d9ecec" },
        { dx: 10, spread: 0, size: 1, color: "#9cc8d4" },
        { dx: 12, spread: 1, size: 1, color: "#f7fbf8" },
        { dx: 14, spread: 2, size: 2, color: "#d9ecec" },
        { dx: 16, spread: 0, size: 1, color: "#f7fbf8" },
        { dx: 18, spread: 1, size: 1, color: "#9cc8d4" }
    ]
    readonly property var dustParticles: [
        { dx: -16, spread: 1, size: 1, color: "#d9cfc0" },
        { dx: -13, spread: 0, size: 2, color: "#b9a98f" },
        { dx: -10, spread: 1, size: 1, color: "#d9cfc0" },
        { dx: -7, spread: 0, size: 2, color: "#b9a98f" },
        { dx: -4, spread: 1, size: 1, color: "#d9cfc0" },
        { dx: 0, spread: 0, size: 2, color: "#e7ddca" },
        { dx: 4, spread: 1, size: 1, color: "#b9a98f" },
        { dx: 7, spread: 0, size: 2, color: "#d9cfc0" },
        { dx: 10, spread: 1, size: 1, color: "#b9a98f" },
        { dx: 13, spread: 0, size: 2, color: "#d9cfc0" },
        { dx: 16, spread: 1, size: 1, color: "#b9a98f" }
    ]

    Repeater {
        model: root.splashParticles

        Rectangle {
            required property var modelData

            readonly property real p: root.progress

            visible: root.on && root.impact.kind === "splash"
            x: Math.round(root.at.x + modelData.dx * (0.35 + 0.8 * p))
            y: Math.round(root.at.y + modelData.spread * (1 - p) - 24 * p + 42 * p * p)
            width: modelData.size
            height: modelData.size
            color: modelData.color
            opacity: 1 - p
            antialiasing: false
        }
    }

    Repeater {
        model: root.dustParticles

        Rectangle {
            required property var modelData

            readonly property real p: root.progress

            visible: root.on && root.impact.kind === "dust"
            x: Math.round(root.at.x + modelData.dx * (0.35 + 0.8 * p))
            y: Math.round(root.at.y + modelData.spread * (1 - p) - 9 * p + 18 * p * p)
            width: modelData.size
            height: modelData.size
            color: modelData.color
            opacity: 1 - p
            antialiasing: false
        }
    }

    /// The splash's foam line on the rim; it breaks up as the droplets fall, rather than sitting as a solid bar.
    Repeater {
        model: 13

        Rectangle {
            required property int index

            visible: root.on && root.impact.kind === "splash" && root.progress < 0.7
            x: Math.round(root.at.x - 12 + index * 2)
            y: Math.round(root.at.y - (index % 3 === 0 ? 1 : 0))
            width: 1
            height: 1
            color: "#f7fbf8"
            opacity: 1 - root.progress
            antialiasing: false
        }
    }
}
