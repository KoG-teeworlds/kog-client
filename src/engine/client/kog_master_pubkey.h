#ifndef ENGINE_CLIENT_KOG_MASTER_PUBKEY_H
#define ENGINE_CLIENT_KOG_MASTER_PUBKEY_H

// Hex public keys of the roots that sign the delegation of a channel's master (a and b, so one can be replaced while
// the other still works). "" = not set. With no key set the channel has no master list and the client does not ask for one.
// These are not the update keys: a master root never signs a release.
//
// The list URLs are the primary master of the channel and the mirror that is tried after it. The signed list may name more.

// BEGIN stable
static const char *const KOG_MASTER_STABLE_ROOT_KEYS[] = {"", ""};
#define KOG_MASTER_STABLE_URL "https://master.client.kog.tw/v1/stable/servers"
#define KOG_MASTER_STABLE_MIRROR_URL "https://client.kog.tw/list/stable/servers.json"
// END stable

// BEGIN canary
static const char *const KOG_MASTER_CANARY_ROOT_KEYS[] = {"", ""};
#define KOG_MASTER_CANARY_URL "https://master.client.canary.kog.tw/v1/canary/servers"
#define KOG_MASTER_CANARY_MIRROR_URL "https://client.kog.tw/list/canary/servers.json"
// END canary

// BEGIN dev
static const char *const KOG_MASTER_DEV_ROOT_KEYS[] = {"", ""};
#define KOG_MASTER_DEV_URL "https://master.client.dev.kog.tw/v1/dev/servers"
#define KOG_MASTER_DEV_MIRROR_URL "https://client.kog.tw/list/dev/servers.json"
// END dev

#endif
