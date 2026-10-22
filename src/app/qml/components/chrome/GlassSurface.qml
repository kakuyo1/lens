pragma ComponentBehavior: Bound

import QtQuick

/**
 * The glass a card is drawn on in the two glass themes (UI.md 3.1c and 3.1d, docs/adr/0039, docs/adr/0041, docs/adr/0070,
 * docs/adr/0071).
 *
 * The sheet is the window's own (GlassBackdrop.surface), taken as the desktop under the window a little larger than the
 * window, at the screen's resolution. GlassSheetItem carries each picture into one live texture, and the two shader
 * passes in shaders/glass_hblur.frag and shaders/glass.frag blur it along x and then along y, saturate it, place the
 * window within it on every frame, bend it through the displacement map for the liquid theme, and cut the card's
 * rounded corners. So the glass moves with the window at once, a drag takes no capture (GlassHost takes the others), a
 * slider move changes a uniform, and a publish replaces a texture rather than reloading a picture. The fill and the rim
 * are laid over it here.
 */
Item {
    id: root

    /// The surface's sheet, from GlassBackdrop.surface(name). Its name and rectangle address the picture.
    required property var sheet
    /// The colour table of the window this sits in, which decides the glass drawn and the fill's colour.
    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property
    /// "frosted" or "liquid": the glass drawn. Empty draws nothing.
    property string mode: ""
    property real radius: root.tokens.radiusCard
    /// Blur, in percent of the full scale: the theme's own setting.
    property real blur: root.mode === "liquid" ? Controller.settings.liquidBlur : Controller.settings.frostedBlur
    /// Frosted only: saturation in percent, 100 being untouched.
    property real saturation: Controller.settings.frostedSaturation
    /// Rim highlight, in percent of white opacity: the theme's own setting.
    property real rim: root.mode === "liquid" ? Controller.settings.liquidRim : Controller.settings.frostedRim
    /// Liquid only: refraction in px.
    property real refraction: Controller.settings.liquidRefraction
    /// Whether the card draws no backdrop: blur at 0 with nothing to bend (docs/adr/0069). The window is per-pixel
    /// transparent, so the live desktop shows through it — sharper than any sheet, at the screen's own rate, and with
    /// no desktop read asked for. GlassHost still keeps one picture for the text tone, and nothing uploads it here.
    readonly property bool passthrough: GlassRule.passthrough(root.mode, Controller.settings.frostedBlur,
                                                              Controller.settings.liquidBlur, Controller.settings.liquidRefraction)
    /// Whether the card draws the sheet. False with the glass off and in passthrough, so the live texture is released.
    readonly property bool drawsBackdrop: root.mode !== "" && !root.passthrough
    /// The rectangle the picture in force covers, in device-independent pixels. The sheet moves it on a capture.
    readonly property rect shownSheet: root.sheet.sheet
    /// The live texture, for the tests that check a publish replaces a texture rather than reloading a picture.
    readonly property alias sheetItem: sheetItem

    visible: mode !== ""

    readonly property string mapUrl: root.mode === "liquid" && !root.passthrough
        ? "image://glass/" + root.sheet.name + "/map?w=" + root.width + "&h=" + root.height + "&r=" + root.radius
        : ""

    /// The position of an item within its window, from the positions of it and its ancestors. Transforms are left out: a
    /// card lifts while it fades, and the desktop behind it does not move.
    function placeInWindow(item) {
        let x = 0;
        let y = 0;
        for (let it = item; it && it.parent; it = it.parent) {
            x += it.x;
            y += it.y;
        }
        return Qt.point(x, y);
    }

    /// Where the card's top-left corner is within the sheet: the window's place on the desktop, plus the card's place in
    /// the window (a ShadowCard insets its card by its shadow margin), less the sheet's place.
    readonly property vector2d origin: Qt.vector2d(
        (root.Window.window ? root.Window.window.x : 0) + root.placeInWindow(root).x - root.shownSheet.x,
        (root.Window.window ? root.Window.window.y : 0) + root.placeInWindow(root).y - root.shownSheet.y)

    /// One texture, replaced in place: a publish uploads the new picture on the render thread and the old one stays
    /// until it is ready, so there is no blank frame to cover with a second slot (docs/adr/0071).
    GlassSheetItem {
        id: sheetItem
        sheet: root.sheet
        active: root.drawsBackdrop
        width: root.shownSheet.width
        height: root.shownSheet.height
    }

    ShaderEffectSource {
        id: sheetTexture
        sourceItem: sheetItem
        hideSource: true
        // The blur reads a mip whose texel is one tap wide, so the picture needs its mip chain (docs/adr/0070).
        mipmap: true
    }

    /// The sheet blurred along x (docs/adr/0081). It covers the whole sheet rather than the card, so the vertical pass
    /// below still has the sheet's margin to read when its outer taps and the refraction reach past the card's edge.
    ShaderEffect {
        id: horizontalBlur
        visible: root.drawsBackdrop && root.shownSheet.width > 0
        width: root.shownSheet.width
        height: root.shownSheet.height
        fragmentShader: "qrc:/shaders/glass_hblur.frag.qsb"
        property variant sheet: sheetTexture
        property real blur: root.blur
        property vector2d sheetSize: Qt.vector2d(root.shownSheet.width, root.shownSheet.height)
        property vector2d sheetPixels: Qt.vector2d(root.shownSheet.width * Screen.devicePixelRatio,
                                                   root.shownSheet.height * Screen.devicePixelRatio)
    }

    ShaderEffectSource {
        id: blurredTexture
        sourceItem: horizontalBlur
        hideSource: true
        // The vertical taps stand on this layer's mip chain the same way (docs/adr/0081).
        mipmap: true
    }

    Image {
        id: displacement
        anchors.fill: parent
        asynchronous: true
        source: root.mapUrl
    }

    ShaderEffectSource {
        id: displacementTexture
        sourceItem: displacement
        hideSource: true
    }

    ShaderEffect {
        anchors.fill: parent
        visible: root.drawsBackdrop && root.shownSheet.width > 0
        fragmentShader: "qrc:/shaders/glass.frag.qsb"
        property variant blurred: blurredTexture
        property variant map: displacementTexture
        property real refraction: root.mode === "liquid" ? root.refraction : 0
        property real radius: root.radius
        // Blur and saturation are uniforms, not part of the picture's URL: a slider move repaints the card and
        // reloads nothing (docs/adr/0070).
        property real blur: root.blur
        property real saturation: root.mode === "frosted" ? root.saturation : 100
        property vector2d cardSize: Qt.vector2d(root.width, root.height)
        property vector2d origin: root.origin
        property vector2d sheetSize: Qt.vector2d(root.shownSheet.width, root.shownSheet.height)
        // The sheet is drawn into a texture at the window's device ratio, so that is the pixel size the blur reads
        // its texels from, whatever the captured picture's own ratio was.
        property vector2d sheetPixels: Qt.vector2d(root.shownSheet.width * Screen.devicePixelRatio,
                                                   root.shownSheet.height * Screen.devicePixelRatio)
    }

    Rectangle {
        visible: root.mode === "frosted"
        anchors.fill: parent
        radius: root.radius
        color: root.tokens.panel
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, root.rim / 100)
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
