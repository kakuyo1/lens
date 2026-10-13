pragma ComponentBehavior: Bound

import QtQuick

/**
 * The glass a card is drawn on in the two glass themes (UI.md 3.1c and 3.1d, docs/adr/0039).
 *
 * GlassBackdrop takes the desktop under the window as a sheet a little larger than the window, and C++ blurs and
 * saturates it per request. The shader in shaders/glass.frag places the window within the sheet on every frame,
 * bends it through the displacement map for the liquid theme, and cuts the card's rounded corners. So the glass moves
 * with the window at once, and a drag takes no capture. While the window is kept out of captures the sheet is also
 * refreshed about fifteen times a second, so video behind the card shows through (docs/adr/0040). The fill and the rim
 * are laid over it here.
 */
Item {
    id: root

    /// Changes with every capture, so the image cache never serves an earlier sheet.
    required property int serial
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

    readonly property string sheetUrl: root.mode === ""
        ? ""
        : "image://glass/sheet?serial=" + root.serial + "&blur=" + root.blur
          + "&saturation=" + (root.mode === "frosted" ? root.saturation : 100)
    readonly property string mapUrl: root.mode === "liquid"
        ? "image://glass/map?w=" + root.width + "&h=" + root.height + "&r=" + root.radius
        : ""

    /// The window's place on the desktop, which is where the card sits within the sheet.
    readonly property vector2d origin: Qt.vector2d(
        (root.Window.window ? root.Window.window.x : 0) - root.shownSheet.x,
        (root.Window.window ? root.Window.window.y : 0) - root.shownSheet.y)

    /// Two slots take turns. A new sheet loads into the hidden one and is shown once it is ready, so the glass on screen
    /// never goes blank. Each slot keeps the rectangle its sheet was taken at, since that is where the shader reads it.
    property bool firstShown: true
    property rect firstSheet
    property rect secondSheet
    readonly property rect shownSheet: root.firstShown ? root.firstSheet : root.secondSheet
    readonly property Image shownSlot: root.firstShown ? first : second

    function loadSheet() {
        if (root.firstShown) {
            root.secondSheet = Controller.glassBackdrop.sheet;
            second.source = root.sheetUrl;
        } else {
            root.firstSheet = Controller.glassBackdrop.sheet;
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
        visible: root.mode !== "" && root.shownSheet.width > 0
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
        color: SettingsTokens.panel
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
