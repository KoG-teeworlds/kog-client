// This file can be included several times.

#include <game/kog_version.h>

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Tcme, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Tcme, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Tcme, ScriptName, Len, Def, Save, Desc) ;
#endif

MACRO_CONFIG_INT(KogHello, kog_hello, KOG_HELLO_DEFAULT, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Tell KoG servers (only those listed for the KoG community) which KoG features this client supports, and use only what both sides have. Off: nothing is sent; stock behaviour")
MACRO_CONFIG_INT(KogTelemetry, kog_telemetry, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Send client-side network statistics to servers that ask for them (opt-in, not implemented yet)")
MACRO_CONFIG_INT(KogCheckUpdates, kog_check_updates, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Look for a new version of this client on client.kog.tw (the manifest is signed; nothing is installed without asking)")
MACRO_CONFIG_STR(KogUpdateChannel, kog_update_channel, 16, KOG_CLIENT_CHANNEL, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Update channel: stable, canary or dev. Only set it here or in the settings, never from a server")
MACRO_CONFIG_STR(KogUpdateUrl, kog_update_url, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Override the host of the update manifest (https only; empty = the built-in KoG address)")
MACRO_CONFIG_INT(ClKogLogin, cl_kog_login, KOG_LOGIN_DEFAULT, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Switch for Log in with KoG (browser login, device key). Off: no browser flow and no automatic login; the /login code still works")
MACRO_CONFIG_INT(ClKogAutoLogin, cl_kog_auto_login, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Prove this device to KoG servers when joining (needs the login protocol on the server; off until it exists)")
MACRO_CONFIG_STR(ClKogAccountUrl, cl_kog_account_url, 128, "https://kog.tw", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Base URL of the KoG account server (https), for staging")
MACRO_CONFIG_STR(ClKogAccountExtraOrigins, cl_kog_account_extra_origins, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Origins besides the account URL's own and *.kog.tw that its login description may name (https://host[:port], separated by spaces)")
MACRO_CONFIG_STR(ClKogDeviceName, cl_kog_device_name, 161, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Name of this device on your account, at most 40 characters (empty = KoG Client plus the system)")
MACRO_CONFIG_STR(ClKogAccountName, cl_kog_account_name, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Account name shown in the settings after Log in with KoG (not a secret)")
MACRO_CONFIG_STR(KogInstallId, kog_install_id, 40, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Random value of this install that picks its place in a staged rollout. Never sent anywhere")
MACRO_CONFIG_INT(ClLocalizationValidate, cl_localization_validate, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ignore translations whose printf format specifiers differ from the English string (0 = old behaviour, for debugging only)")
