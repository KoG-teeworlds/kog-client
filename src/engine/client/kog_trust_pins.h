#ifndef ENGINE_CLIENT_KOG_TRUST_PINS_H
#define ENGINE_CLIENT_KOG_TRUST_PINS_H

// Hex public keys of the KoG certificate authority (the trust anchor), per client channel. They are the public
// halves of the keys that infra-github publishes as DNS TXT records (_kogkey) and web2 signs the trust list with.
// "" = not set: without a pinned key the client does not check certificates and answers as before.
// Stable uses the production anchor; canary and dev use the staging anchor (one root for both).
// Two entries so a rotation overlaps: the new key is added one release before the old one is dropped.
// The values are the owner's to fill from cloudflare/zone/trust-anchors.yaml (docs/SERVER-CERTIFICATES.md).
// BEGIN stable
static const char *const KOG_TRUST_ANCHORS_STABLE[] = {"", ""};
#define KOG_TRUST_ENVIRONMENT_STABLE "production"
// END stable

// BEGIN canary
static const char *const KOG_TRUST_ANCHORS_CANARY[] = {"", ""};
#define KOG_TRUST_ENVIRONMENT_CANARY "staging"
// END canary

// BEGIN dev
static const char *const KOG_TRUST_ANCHORS_DEV[] = {"", ""};
#define KOG_TRUST_ENVIRONMENT_DEV "staging"
// END dev

#endif
