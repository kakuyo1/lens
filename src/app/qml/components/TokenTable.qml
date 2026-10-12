import QtQuick

/**
 * The colour, type and motion table a window reads, from UI.md section 3 and its motion table.
 *
 * There are two instances, one per kind of window: Tokens, which every surface reads with the glass
 * themes off, and SettingsTokens, which the settings window reads with them on. Both are this file,
 * so a role means the same thing in each and the only difference between them is the glass.
 *
 * Built-in and custom themes use the same semantic roles. Custom values are encoded in the
 * existing theme setting so the settings document remains the single persistence boundary.
 */
QtObject {
    id: tokens

    /// "light", "dark", "frosted", "liquid", or a custom: JSON value. Main.qml keeps this in step with settings.
    property string theme: "light"
    /// Whether the glass themes apply here. Off in Tokens, so a glass theme reads as the light table elsewhere.
    property bool glassEnabled: false
    readonly property bool custom: theme.indexOf(customPrefix) === 0
    readonly property bool dark: theme === "dark" || (custom && customValues.mode === "dark")

    /// "frosted" or "liquid" while a glass theme applies here, empty otherwise. The glass surfaces key off this.
    readonly property string glassMode: (glassEnabled && (theme === "frosted" || theme === "liquid")) ? theme : ""
    /// The text colour of the liquid theme, from the settings document. Main.qml keeps this in step.
    property string liquidTextColor: "#101319"
    /// Whether the desktop under the glass calls for light text. The settings window sets this from the picture it took.
    property bool glassLightText: false
    /// Light text over glass: the dark-desktop roles replace the light ones.
    readonly property bool lightOnGlass: glassMode !== "" && glassLightText

    readonly property Light lightTheme: Light {}
    readonly property Dark darkTheme: Dark {}
    readonly property Frosted frostedTheme: Frosted {}
    readonly property Liquid liquidTheme: Liquid {}
    readonly property GlassOnDark glassOnDark: GlassOnDark {}
    readonly property var customValues: parseCustomTheme(theme)
    readonly property var builtin: builtinTheme(theme)
    /// Not readonly: SettingsTokens shares Tokens' instance, so a reduced-motion switch reaches both.
    property Motion motion: Motion {}

    readonly property color bg: tokenColor("bg", lightTheme.bg)
    readonly property color panel: tokenColor("panel", lightTheme.panel)
    readonly property color panel2: tokenColor("panel2", lightTheme.panel2)
    readonly property color line: tokenColor("line", lightTheme.line)
    readonly property color line2: tokenColor("line2", lightTheme.line2)
    readonly property color text: tokenColor("text", lightTheme.text)
    readonly property color muted: tokenColor("muted", lightTheme.muted)
    readonly property color faint: tokenColor("faint", lightTheme.faint)
    readonly property color ok: tokenColor("ok", lightTheme.ok)
    readonly property color okBg: tokenColor("okBg", lightTheme.okBg)
    readonly property color okText: tokenColor("okText", lightTheme.okText)
    readonly property color ink: tokenColor("ink", lightTheme.ink)
    readonly property color on: tokenColor("on", lightTheme.on)
    readonly property color danger: tokenColor("danger", lightTheme.danger)
    readonly property color bubbleBg: tokenColor("bubbleBg", lightTheme.bubbleBg)
    readonly property color bubbleBorder: tokenColor("bubbleBorder", lightTheme.bubbleBorder)
    /// The dropdown lists and other floating layers: the card fill, except under the glass themes,
    /// where it is the translucent popup layer (UI.md 3.1c).
    readonly property color popup: custom ? panel : tokenColor("popup", lightTheme.popup)

    /// The liquid theme's card is 22 px; the others keep 18 px (UI.md 3.1d and 4.3).
    readonly property int radiusCard: glassMode === "liquid" ? 22 : 18
    readonly property int radiusMenu: 13
    readonly property int radiusGroup: 11
    readonly property int radiusField: 9
    readonly property int radiusPill: 999
    readonly property string monoFamily: "Cascadia Code"
    readonly property string customPrefix: "custom:"

    function builtinTheme(value) {
        if (value === "dark")
            return darkTheme;
        if (glassEnabled && value === "frosted")
            return frostedTheme;
        if (glassEnabled && value === "liquid")
            return liquidTheme;
        return lightTheme;
    }

    /// The settings dropdown's value for a theme. Custom keeps 2 so the stored value stays readable.
    function themeIndex(value) {
        if (value === "dark")
            return 1;
        if (value === "frosted")
            return 3;
        if (value === "liquid")
            return 4;
        if (value.indexOf(customPrefix) === 0)
            return 2;
        return 0;
    }

    function themeName(index) {
        if (index === 1)
            return "dark";
        if (index === 3)
            return "frosted";
        if (index === 4)
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

    function parseCustomTheme(value) {
        if (typeof value !== "string" || value.indexOf(customPrefix) !== 0)
            return {};
        try {
            const parsed = JSON.parse(value.slice(customPrefix.length));
            return parsed && typeof parsed === "object" ? parsed : {};
        } catch (error) {
            return {};
        }
    }

    function tokenColor(role, fallback) {
        if (custom)
            return hex(customValues[role], hex(fallback, "#000000"));
        if (glassMode === "liquid" && role === "text" && !lightOnGlass)
            return hex(liquidTextColor, hex(fallback, "#000000"));
        const table = lightOnGlass && glassOnDark[role] !== undefined ? glassOnDark : builtin;
        return hex(table[role], hex(fallback, "#000000"));
    }

    function colorsForTheme(value) {
        const base = builtinTheme(value);
        const customValuesForTheme = parseCustomTheme(value);
        const result = {};
        const roles = ["bg", "panel", "panel2", "line", "line2", "text", "muted", "faint",
                       "ok", "okBg", "okText", "ink", "on", "danger", "bubbleBg", "bubbleBorder"];
        for (const role of roles)
            result[role] = hex(customValuesForTheme[role], hex(base[role], "#000000"));
        result.mode = customValuesForTheme.mode === "dark" || value === "dark"
                ? "dark" : "light";
        return result;
    }

    function customThemeValue(colors) {
        const defaults = colorsForTheme("light");
        const roles = ["bg", "panel", "panel2", "line", "line2", "text", "muted", "faint",
                       "ok", "okBg", "okText", "ink", "on", "danger", "bubbleBg", "bubbleBorder"];
        const table = {mode: colors.mode === "dark" ? "dark" : "light"};
        for (const role of roles)
            table[role] = hex(colors[role], defaults[role]);
        return customPrefix + JSON.stringify(table);
    }

    function relativeLuminance(value) {
        const color = Qt.color(value);
        function linear(component) {
            return component <= 0.04045 ? component / 12.92
                                         : Math.pow((component + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b);
    }

    function opaqueColor(value, background) {
        const foreground = Qt.color(value);
        const underlay = Qt.color(background);
        return Qt.rgba(foreground.r * foreground.a + underlay.r * (1 - foreground.a),
                       foreground.g * foreground.a + underlay.g * (1 - foreground.a),
                       foreground.b * foreground.a + underlay.b * (1 - foreground.a), 1);
    }

    function contrastRatio(foreground, background, underlay) {
        const base = underlay === undefined ? Qt.color(background) : opaqueColor(background, underlay);
        const face = opaqueColor(foreground, base);
        const first = relativeLuminance(face);
        const second = relativeLuminance(base);
        return (Math.max(first, second) + 0.05) / (Math.min(first, second) + 0.05);
    }

    function validateCustomTheme(colors) {
        const required = ["bg", "panel", "text", "muted", "faint", "ok"];
        let valid = true;
        for (const role of required)
            valid = valid && validHex(colors[role]);

        const bodyRatio = valid ? contrastRatio(colors.text, colors.panel, colors.bg) : 0;
        const mutedRatio = valid ? contrastRatio(colors.muted, colors.panel, colors.bg) : 0;
        const faintRatio = valid ? contrastRatio(colors.faint, colors.panel, colors.bg) : 0;
        return {
            valid: valid && bodyRatio >= 4.5 && mutedRatio >= 4.5 && faintRatio >= 4.5,
            bodyRatio: bodyRatio,
            mutedRatio: mutedRatio,
            faintRatio: faintRatio
        };
    }
}
