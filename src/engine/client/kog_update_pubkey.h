#ifndef ENGINE_CLIENT_KOG_UPDATE_PUBKEY_H
#define ENGINE_CLIENT_KOG_UPDATE_PUBKEY_H

// Hex public keys, "" = not set. The CI keys come from the infrastructure, the backup CI key and the owner keys
// (security keys, signature type sk-ssh-ed25519@openssh.com) are the owner's. Placeholder 1 = the channel does not update.
#define KOG_OWNER_KEY_APPLICATION "ssh:"

// BEGIN stable
#define KOG_UPDATE_KEYS_PLACEHOLDER_STABLE 1
static const char *const KOG_STABLE_CI_KEYS[] = {"b623296a1bff90cc7add0412a4dc2e37cb06423a3d09dee2278a85365033e18e", "b8a2ac4a3fb642b0551a5b3151aaed0033fcf06cd91a576f32824bb393f134bb"};
static const char *const KOG_STABLE_BACKUP_CI_KEYS[] = {""};
static const char *const KOG_STABLE_OWNER_KEYS[] = {"", ""};
// END stable

// BEGIN canary
#define KOG_UPDATE_KEYS_PLACEHOLDER_CANARY 0
static const char *const KOG_CANARY_CI_KEYS[] = {"1cb978aa618625990a5b08c55e93fe51b28952444177fb3d70b162dd014e3149", "bb24221ba315efbe33a0bd7658a4061bde6a52b1884e22f48fb82fc441cfbf0f"};
// END canary

// BEGIN dev
#define KOG_UPDATE_KEYS_PLACEHOLDER_DEV 0
static const char *const KOG_DEV_CI_KEYS[] = {"240fcdd532b56a392f1bd5e960adf2cd57011bf5cfd137b094d74fd247e908e0", "a759425be3670cb0fcbc0b07d627d3918bf5030232cde7593dcd6aeb966804af"};
// END dev

#endif
