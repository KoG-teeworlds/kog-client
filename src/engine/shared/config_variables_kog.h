// This file can be included several times.

#include <game/kog_version.h>

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Tcme, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Tcme, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Tcme, ScriptName, Len, Def, Save, Desc) ;
#endif

MACRO_CONFIG_INT(KogHello, kog_hello, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Announce this client's KoG capabilities to servers (not implemented yet)")
MACRO_CONFIG_INT(KogTelemetry, kog_telemetry, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Send client-side network statistics to servers that ask for them (opt-in, not implemented yet)")
MACRO_CONFIG_INT(KogCheckUpdates, kog_check_updates, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Look for a new version of this client on client.kog.tw (the manifest is signed; nothing is installed without asking)")
MACRO_CONFIG_STR(KogUpdateChannel, kog_update_channel, 16, KOG_CLIENT_CHANNEL, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Update channel: stable, canary or dev. Only set it here or in the settings, never from a server")
MACRO_CONFIG_STR(KogUpdateUrl, kog_update_url, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Override the host of the update manifest (https only; empty = the built-in KoG address)")
MACRO_CONFIG_STR(KogInstallId, kog_install_id, 40, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Random value of this install that picks its place in a staged rollout. Never sent anywhere")
