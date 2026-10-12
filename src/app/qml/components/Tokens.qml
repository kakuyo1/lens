pragma Singleton

/**
 * The colour table every surface reads except the settings window.
 *
 * The glass themes are off here, so each one reads as the light table: a surface outside settings is
 * never glass (UI.md 3.1c and 3.1d). The settings window reads SettingsTokens instead.
 */
TokenTable {}
