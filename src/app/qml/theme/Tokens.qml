pragma Singleton

/**
 * The stored theme and the motion table. Every surface window forwards them into its own SurfaceTokens
 * (docs/adr/0041); nothing reads colours from here directly.
 *
 * The glass themes are off here, so read directly this is always the light table (DESIGN.md 3.1c and 3.1d).
 */
TokenTable {}
