import QtQuick

/**
 * The colour table one surface window reads: the stored theme, with this window's glass on or off.
 *
 * Each surface window's root declares one as its `tokens`. The components inside a window read that table through
 * `Window.window.tokens`, so a glass window and a plain one can show different colours at the same moment, and each
 * glass window judges its own text tone (docs/adr/0041).
 */
TokenTable {
    theme: Tokens.theme
    liquidTextColor: Tokens.liquidTextColor
    motion: Tokens.motion
}
