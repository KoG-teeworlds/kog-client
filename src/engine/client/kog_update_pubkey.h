#ifndef ENGINE_CLIENT_KOG_UPDATE_PUBKEY_H
#define ENGINE_CLIENT_KOG_UPDATE_PUBKEY_H

// Hex public keys, "" = not set. The CI keys come from the infrastructure, the backup CI key and the owner keys
// (security keys, signature type sk-ssh-ed25519@openssh.com) are the owner's. Placeholder 1 = the channel does not update.
#define KOG_OWNER_KEY_APPLICATION "ssh:"

// BEGIN stable
#define KOG_UPDATE_KEYS_PLACEHOLDER_STABLE 1
static const char *const KOG_STABLE_CI_KEYS[] = {"", ""};
static const char *const KOG_STABLE_BACKUP_CI_KEYS[] = {""};
static const char *const KOG_STABLE_OWNER_KEYS[] = {"", ""};
// END stable

// BEGIN canary
#define KOG_UPDATE_KEYS_PLACEHOLDER_CANARY 1
static const char *const KOG_CANARY_CI_KEYS[] = {"", ""};
// END canary

// BEGIN dev
#define KOG_UPDATE_KEYS_PLACEHOLDER_DEV 1
static const char *const KOG_DEV_CI_KEYS[] = {"", ""};
// END dev

#endif
