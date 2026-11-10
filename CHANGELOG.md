# Changelog

## Unreleased

- Add a Valheim macOS file fix. It keeps the 480 launch path working past Steam's AppID/restart checks and logs in to PlayFab with a stable custom ID, so OnlineFix routing can reach the main menu. The fix modifies `assembly_valheim.dll` and `PlayFab.dll`, keeps backups, and is installed or restored from the Fix tab.

## v2.0.0

- CustomFix workflow for installing and managing game-specific fixes directly in Manager.
- Built-in Fix catalog organizing patch sources and supported games.
- Native Photon multiplayer compatibility patches for Some Game
- Dynamic Steam client build detection with versioned compatibility profile loading.

## v1.0.0

Initial release for Apple Silicon, macOS 15+.

- Native Manager with Steam game search, artwork, and per-game settings.
- Standalone OnlineFix dylib routing selected games to AppID 480.
- Persistent installation with Steam backup and guarded restoration.
- Separate compatibility patches for startup, overlay, Steam Input, and stats.
- Terraria server environment fix with automatic restoration on uninstall.
- Logs reset on each Steam launch.

Terraria now can host & play, (multiplayer not tested yet)
