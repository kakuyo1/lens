import QtQuick

/**
 * The colour, type and motion table a window reads, from DESIGN.md's Colors and Typography and UI-SPEC.md's motion table.
 *
 * Each surface window owns one instance, a SurfaceTokens that forwards the stored theme from Tokens; the glass is on
 * only in the windows that set glassEnabled. Every instance is this file, so a role means the same thing in each and
 * the only difference between them is the glass (docs/adr/0041).
 *
 * A change of theme is a cross-fade, not a swap: the colours keep coming from the table they were showing while they
 * move to the new one over `themeFade` (docs/adr/0196). The value tables themselves only carry endpoints.
 */
QtObject {
    id: tokens

    /// "light", "dark", "frosted" or "liquid". Main.qml keeps this in step with settings.
    property string theme: "light"
    /// Whether the glass themes apply here. Off in Tokens, so a glass theme reads as the light table elsewhere.
    property bool glassEnabled: false
    readonly property bool dark: theme === "dark"

    /// "frosted" or "liquid" while a glass theme applies here, empty otherwise. The glass surfaces key off this.
    readonly property string glassMode: (glassEnabled && (theme === "frosted" || theme === "liquid")) ? theme : ""
    /// Whether the desktop under the glass calls for light text. The settings window sets this from the picture it took.
    property bool glassLightText: false
    /// Light text over glass: the dark-desktop roles replace the light ones.
    readonly property bool lightOnGlass: glassMode !== "" && glassLightText

    readonly property Light lightTheme: Light {}
    readonly property Dark darkTheme: Dark {}
    readonly property Frosted frostedTheme: Frosted {}
    readonly property Liquid liquidTheme: Liquid {}
    readonly property GlassOnDark glassOnDark: GlassOnDark {}
    readonly property var builtin: builtinTheme(theme)
    /// Not readonly: each SurfaceTokens takes Tokens' instance, so a reduced-motion switch reaches every window.
    property Motion motion: Motion {}
    /// How long a colour takes to cross to another theme's table. The skin previews turn it off, so
    /// hovering down the theme list compares each card at once.
    property int themeFade: motion.theme

    /// The theme whose values the colours are showing. Empty until creation settles, and read then
    /// as the light table, the default.
    property string shownTheme: ""
    /// The table the running fade is leaving; null when no fade is running.
    property var fadeFrom: null
    /// How far the colours have come from `fadeFrom` to `builtin`: 0 is the old theme, 1 the new.
    property real mix: 1
    /// False until creation finishes, so a table built under another theme adopts it instead of
    /// fading from the light one.
    property bool created: false

    Component.onCompleted: tokens.created = true

    onThemeChanged: {
        if (!created) {
            shownTheme = theme;
            return;
        }
        const from = builtinTheme(shownTheme);
        const to = builtinTheme(theme);
        shownTheme = theme;
        if (from === to || themeFade <= 0) {
            fadeAnim.stop();
            fadeFrom = null;
            mix = 1;
            return;
        }
        fadeFrom = from;
        mix = 0;
        fadeAnim.restart();
    }

    /// The one animation a theme change runs: `mix` from 0 to 1 over `themeFade`.
    property NumberAnimation fadeAnim: NumberAnimation {
        target: tokens
        property: "mix"
        from: 0
        to: 1
        duration: tokens.themeFade
        easing.type: tokens.motion.easing
        onFinished: tokens.fadeFrom = null
    }

    readonly property color bg: tokenColor("bg", lightTheme.bg)
    readonly property color panel: tokenColor("panel", lightTheme.panel)
    readonly property color panel2: tokenColor("panel2", lightTheme.panel2)
    readonly property color line: tokenColor("line", lightTheme.line)
    readonly property color line2: tokenColor("line2", lightTheme.line2)
    readonly property color text: tokenColor("text", lightTheme.text)
    readonly property color muted: tokenColor("muted", lightTheme.muted)
    readonly property color faint: tokenColor("faint", lightTheme.faint)
    /// The colour an explanation's body is read in. On the solid themes it is the second text
    /// step; on glass it is the first, because the second step is not AA over a 40% white card
    /// (docs/adr/0106). Hierarchy there comes from size, weight and position.
    readonly property color body: glassMode === "" ? muted : text
    readonly property color ok: tokenColor("ok", lightTheme.ok)
    readonly property color okBg: tokenColor("okBg", lightTheme.okBg)
    readonly property color okText: tokenColor("okText", lightTheme.okText)
    readonly property color ink: tokenColor("ink", lightTheme.ink)
    readonly property color on: tokenColor("on", lightTheme.on)
    readonly property color danger: tokenColor("danger", lightTheme.danger)
    /// The budget-exhausted state: the tray icon draws it natively, and the tray menu's state
    /// line tints its mark with the same token (UI-SPEC.md 4.2).
    readonly property color warn: tokenColor("warn", lightTheme.warn)
    readonly property color bubbleBg: tokenColor("bubbleBg", lightTheme.bubbleBg)
    readonly property color bubbleBorder: tokenColor("bubbleBorder", lightTheme.bubbleBorder)
    /// The breakdown roles (DESIGN.md 3.1e). They mark grammar only: never a state, a button or a surface.
    readonly property color roleS: tokenColor("roleS", lightTheme.roleS)
    readonly property color roleV: tokenColor("roleV", lightTheme.roleV)
    readonly property color roleC: tokenColor("roleC", lightTheme.roleC)
    readonly property color roleA: tokenColor("roleA", lightTheme.roleA)
    /// The dropdown lists and other floating layers: the card fill, except under the glass themes,
    /// where it is the translucent popup layer (DESIGN.md 3.1c).
    readonly property color popup: tokenColor("popup", lightTheme.popup)
    /// The scrim under the full-screen report card (UI-SPEC.md 4.7c). The glass themes take the light value.
    readonly property color scrim: tokenColor("scrim", lightTheme.scrim)

    /// The liquid theme's card is 22 px; the others keep 18 px (DESIGN.md 3.1d and UI-SPEC.md 4.3).
    readonly property int radiusCard: glassMode === "liquid" ? 22 : 18
    readonly property int radiusMenu: 13
    readonly property int radiusGroup: 11
    readonly property int radiusField: 9
    readonly property int radiusPill: 999
    readonly property string monoFamily: "Cascadia Code"

    function builtinTheme(value) {
        if (value === "dark")
            return darkTheme;
        if (glassEnabled && value === "frosted")
            return frostedTheme;
        if (glassEnabled && value === "liquid")
            return liquidTheme;
        return lightTheme;
    }

    /// The settings dropdown's value for a theme, and its inverse.
    function themeIndex(value) {
        if (value === "dark")
            return 1;
        if (value === "frosted")
            return 2;
        if (value === "liquid")
            return 3;
        return 0;
    }

    function themeName(index) {
        if (index === 1)
            return "dark";
        if (index === 2)
            return "frosted";
        if (index === 3)
            return "liquid";
        return "light";
    }

    /// Accept RGB and Qt's alpha-first ARGB hex notation.
    function validHex(value) {
        return typeof value === "string" && /^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/.test(value);
    }

    function channel(value) {
        return Math.round(Math.max(0, Math.min(1, value)) * 255).toString(16).padStart(2, "0");
    }

    /// @return A stable hex representation without dropping a color's alpha channel.
    function hex(value, fallback) {
        const text = String(value);
        if (validHex(text))
            return text.toLowerCase();
        if (value && value.r !== undefined) {
            const rgb = channel(value.r) + channel(value.g) + channel(value.b);
            const alpha = value.a === undefined ? 1 : value.a;
            return alpha < 0.999999 ? "#" + channel(alpha) + rgb : "#" + rgb;
        }
        return fallback;
    }

    function tokenColor(role, fallback) {
        const table = lightOnGlass && glassOnDark[role] !== undefined ? glassOnDark : builtin;
        const to = hex(table[role], hex(fallback, "#000000"));
        if (!fadeFrom || mix >= 1)
            return to;
        const fromTable = lightOnGlass && glassOnDark[role] !== undefined ? glassOnDark : fadeFrom;
        return blend(hex(fromTable[role], hex(fallback, "#000000")), to, mix);
    }

    /// @return @p from moved toward @p to by @p t, channel by channel.
    function blend(from, to, t) {
        const a = Qt.color(from);
        const b = Qt.color(to);
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                       a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
    }
}
