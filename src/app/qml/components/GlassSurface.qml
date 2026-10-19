pragma ComponentBehavior: Bound

import QtQuick

/**
 * The glass a card is drawn on in the two glass themes (UI.md 3.1c and 3.1d, docs/adr/0039, docs/adr/0041).
 *
 * The sheet is the window's own (GlassBackdrop.surface), taken as the desktop under the window a little larger than the
 * window, and C++ blurs and saturates it per request. The shader in shaders/glass.frag places the window within the sheet
 * on every frame, bends it through the displacement map for the liquid theme, and cuts the card's rounded corners. So the
 * glass moves with the window at once, and a drag takes no capture (GlassHost takes the others). The fill and the rim are
 * laid over it here.
 */
Item {
    id: root

    /// The surface's sheet, from GlassBackdrop.surface(name). Its name, serial and rectangle address the picture.
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
    /// no desktop read asked for. GlassHost still keeps one picture for the text tone, and nothing loads it here.
    readonly property bool passthrough: GlassRule.passthrough(root.mode, Controller.settings.frostedBlur,
                                                              Controller.settings.liquidBlur, Controller.settings.liquidRefraction)

    visible: mode !== ""

    readonly property string sheetUrl: root.mode === "" || root.passthrough
        ? ""
        : "image://glass/" + root.sheet.name + "/sheet?serial=" + root.sheet.serial + "&blur=" + root.blur
          + "&saturation=" + (root.mode === "frosted" ? root.saturation : 100)
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

    /// Two slots take turns. A new sheet loads into the hidden one and is shown once it is ready, so the glass on screen
    /// never goes blank. Each slot keeps the rectangle its sheet was taken at, since that is where the shader reads it.
    property bool firstShown: true
    property rect firstSheet
    property rect secondSheet
    readonly property rect shownSheet: root.firstShown ? root.firstSheet : root.secondSheet
    readonly property Image shownSlot: root.firstShown ? first : second

    function loadSheet() {
        if (root.firstShown) {
            root.secondSheet = root.sheet.sheet;
            second.source = root.sheetUrl;
        } else {
            root.firstSheet = root.sheet.sheet;
            first.source = root.sheetUrl;
        }
    }

    onSheetUrlChanged: root.loadSheet()
    Component.onCompleted: root.loadSheet()

    Image {
        id: first
        width: root.firstSheet.width
        height: root.firstSheet.height
        asynchronous: true
        onStatusChanged: {
            if (status === Image.Ready && !root.firstShown)
                root.firstShown = true;
        }
    }

    ShaderEffectSource {
        id: firstTexture
        sourceItem: first
        hideSource: true
    }

    Image {
        id: second
        width: root.secondSheet.width
        height: root.secondSheet.height
        asynchronous: true
        onStatusChanged: {
            if (status === Image.Ready && root.firstShown)
                root.firstShown = false;
        }
    }

    ShaderEffectSource {
        id: secondTexture
        sourceItem: second
        hideSource: true
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
        visible: root.mode !== "" && !root.passthrough && root.shownSheet.width > 0
        fragmentShader: "qrc:/shaders/glass.frag.qsb"
        property variant sheet: root.firstShown ? firstTexture : secondTexture
        property variant map: displacementTexture
        property real refraction: root.mode === "liquid" ? root.refraction : 0
        property real radius: root.radius
        property vector2d cardSize: Qt.vector2d(root.width, root.height)
        property vector2d origin: root.origin
        property vector2d sheetSize: Qt.vector2d(root.shownSheet.width, root.shownSheet.height)
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
