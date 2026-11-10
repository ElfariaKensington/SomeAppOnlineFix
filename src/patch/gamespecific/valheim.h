#pragma once
#include <stdbool.h>

// Patch or check the supported Valheim macOS build. Returns 0 or errno.
// The patch fixes the 480 launch path: the game uses its real AppID for the
// SteamManager checks and logs in to PlayFab with a stable custom ID.
int of_valheim_patch(const char *app, bool enabled);
int of_valheim_status(const char *app, bool *enabled);
