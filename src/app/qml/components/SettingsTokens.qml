pragma Singleton

/**
 * The colour table the settings window reads: Tokens with the glass themes on.
 *
 * The theme itself is still the one stored in settings, forwarded from Tokens; only the glass differs.
 */
TokenTable {
    glassEnabled: true
    theme: Tokens.theme
    motion: Tokens.motion
}
