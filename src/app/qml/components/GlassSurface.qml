pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

/**
 * The glass a card is drawn on in the two glass themes (UI.md 3.1c and 3.1d).
 *
 * The desktop picture is `picture`, an `image://glass/backdrop` URL the controller re-serves on each
 * capture. The frosted theme blurs and saturates it in one MultiEffect, cut to the card's corners, and
 * lays the fixed white fill over it. The liquid theme blurs it first and then refracts the blurred
 * picture through the displacement map in a shader, in that order: the prototype's filter chain blurs
 * before it bends, and bending first leaves the edges sharp again.
 *
 * The picture and the map are Images that are never drawn on their own: every read of them goes through
 * a ShaderEffectSource with hideSource, so the only thing on screen is the effect at the end of a chain.
 * The effects take their size from their source, the way docs/QML.md section 3 asks, so nothing here is
 * anchored to the window: the owner places this item on the card.
 */
Item {
    id: root

    /// The desktop picture under the card, as an `image://glass/backdrop` URL.
    required property string picture
    /// "frosted" or "liquid": the glass drawn. Empty draws nothing.
    property string mode: ""
    property real radius: SettingsTokens.radiusCard
    /// Blur, in percent of the full scale.
    property real blur: 10
    /// Frosted only: saturation in percent, 100 being untouched.
    property real saturation: 120
    /// Rim highlight, in percent of white opacity.
    property real rim: 20
    /// Liquid only: refraction in px.
    property real refraction: 20

    visible: mode !== ""

    // Loaded only while a glass theme is on, so the other themes never ask the provider for a picture.
    Image {
        id: backdrop
        anchors.fill: parent
        source: root.mode === "" ? "" : root.picture
        cache: false
        asynchronous: false
    }

    // Frosted: the blurred picture, masked to the card's rounded rectangle, then the fill and the rim.
    Item {
        id: cardMask
        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            radius: root.radius
            color: "white"
        }
    }

    MultiEffect {
        id: frosted
        visible: root.mode === "frosted"
        source: ShaderEffectSource {
            sourceItem: backdrop
            hideSource: true
        }
        blurEnabled: root.blur > 0
        blur: root.blur / 100
        blurMax: 60
        saturation: Math.max(-1, Math.min(1, root.saturation / 100 - 1))
        maskEnabled: true
        maskSource: ShaderEffectSource {
            sourceItem: cardMask
            hideSource: true
        }
    }

    Rectangle {
        visible: root.mode === "frosted"
        anchors.fill: parent
        radius: root.radius
        color: SettingsTokens.panel
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, root.rim / 100)
    }

    // Liquid: the blur, rendered first, is the texture the shader refracts. The map holds the offsets the
    // shader reads, generated in C++ at the card's size.
    MultiEffect {
        id: liquidBlur
        source: ShaderEffectSource {
            sourceItem: backdrop
            hideSource: true
        }
        blurEnabled: root.blur > 0
        blur: root.blur / 100
        blurMax: 60
    }

    // The map is built for the card's size, and only once the card has one: a window still being laid out
    // reports zero or negative sizes, and there is no map to ask for then.
    Image {
        id: displacement
        anchors.fill: parent
        source: root.mode === "liquid" && root.width > 0 && root.height > 0
                ? "image://glass/map?w=" + root.width + "&h=" + root.height + "&r=" + root.radius + "&b=18"
                : ""
        cache: false
        asynchronous: false
    }

    ShaderEffect {
        id: refractor
        visible: root.mode === "liquid"
        anchors.fill: parent
        fragmentShader: "qrc:/shaders/liquid.frag.qsb"

        property variant source: ShaderEffectSource {
            sourceItem: liquidBlur
            hideSource: true
        }
        property variant map: ShaderEffectSource {
            sourceItem: displacement
            hideSource: true
        }
        property real refraction: root.refraction
        property real radius: root.radius
        property vector2d cardSize: Qt.vector2d(root.width, root.height)
    }

    Rectangle {
        visible: root.mode === "liquid"
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, root.rim / 100)
    }
}
