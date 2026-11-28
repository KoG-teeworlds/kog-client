#ifndef ENGINE_SHARED_KOG_TRUST_H
#define ENGINE_SHARED_KOG_TRUST_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Does a server prove that KoG enrolled it? The certificate chain of the KoG bus (CA -> node -> game server, wire
// format kog-bus-cert-v1), the server's signature over the challenge, and the signed trust bundle with the
// revocation list. Pure logic, no network. Rules and attack table: docs/SERVER-CERTIFICATES.md.
namespace KogTrust {

enum
{
	KEY_SIZE = 32,
	SIGNATURE_SIZE = 64,
	MAX_ID_SIZE = 64,
	MAX_CERT_SIZE = 512,
	MAX_BUNDLE_SIZE = 64 * 1024,
	MAX_REVOKED = 1024,
	// a clock that is a day off still works; certificates live 30 days
	CLOCK_SKEW_SECONDS = 24 * 3600,
	// a bundle older than this is not used: the revocation list cannot be checked
	MAX_BUNDLE_AGE_SECONDS = 24 * 3600,
	// the client looks again when its copy is older than this, when it connects to a KoG server
	REFRESH_AFTER_SECONDS = 5 * 60,
};

constexpr const char *CERT_DOMAIN = "kog-bus-cert-v1";
constexpr const char *SERVER_DOMAIN = "KoG-server-v1";
constexpr const char *BUNDLE_DOMAIN = "kog-trust-bundle-v1";

struct SCert
{
	std::string m_Subject;
	uint8_t m_aPubkey[KEY_SIZE] = {};
	std::string m_Issuer;
	uint64_t m_NotAfter = 0;
	uint8_t m_aSignature[SIGNATURE_SIZE] = {};
};

// One certificate from the front of the data. False for anything that is not exactly well formed.
bool ParseCert(const uint8_t *pData, size_t Size, SCert *pCert, size_t *pUsed);
// Signature by the issuer key and not expired (with the clock skew)
bool VerifyCert(const SCert &Cert, const uint8_t (&aIssuerKey)[KEY_SIZE], uint64_t Now);

struct SAnchors
{
	std::vector<std::vector<uint8_t>> m_Keys; // 32 bytes each
};
// Pinned keys as hex, "" entries are skipped. Wrong sized or non-hex entries are skipped too.
SAnchors AnchorsFromHex(const char *const *ppHex, size_t Count);

enum class EChain
{
	OK,
	NO_ANCHOR, // nothing is pinned for this channel
	MALFORMED,
	NODE_NOT_BY_ANCHOR, // node certificate is not issued by "ca" under a pinned key
	GAME_NOT_BY_NODE,
	EXPIRED,
};
// Node certificate and game server certificate (wire form, one after the other: node first). On OK the
// subject and the key of the game server are set.
EChain VerifyChain(const SAnchors &Anchors, const uint8_t *pChain, size_t Size, uint64_t Now, SCert *pGameServer);

// "KoG-server-v1" 0x00 || u8 len(addr) || addr || nonce[32] || session_id[16] || u32 BE ttl_ms
size_t BuildServerSigned(uint8_t *pOut, size_t OutSize, const char *pAddress, const uint8_t *pNonce, size_t NonceSize, const uint8_t (&aSessionId)[16], uint32_t TtlMs);
bool VerifyServerSignature(const uint8_t (&aServerKey)[KEY_SIZE], const char *pAddress, const uint8_t *pNonce, size_t NonceSize, const uint8_t (&aSessionId)[16], uint32_t TtlMs, const uint8_t (&aSignature)[SIGNATURE_SIZE]);

struct SRevoked
{
	std::string m_Subject;
	std::string m_PubkeySha256Hex; // may be empty
};

struct SBundle
{
	std::string m_Environment;
	int64_t m_Seq = 0;
	int64_t m_IssuedAt = 0;
	int64_t m_ExpiresAt = 0;
	std::vector<SRevoked> m_Revoked;
};

enum class EBundle
{
	OK,
	NO_ANCHOR,
	MALFORMED,
	BAD_SIGNATURE, // not signed by a pinned key
	WRONG_ENVIRONMENT,
	EXPIRED,
	NOT_NEWER, // sequence below the one the client already holds
};
// The file as served, the same bytes from every host: {"v":1,"payload":base64,"sig":{"<label>":base64}}.
// The payload is signed with a pinned anchor ("kog-trust-bundle-v1" 0x00 || payload bytes), so the host that
// served it does not matter. pEnvironment: "production" or "staging" for this channel.
EBundle ParseBundle(const SAnchors &Anchors, const char *pEnvironment, const char *pFile, size_t Size, uint64_t Now, int64_t HeldSeq, SBundle *pBundle);

bool IsRevoked(const SBundle &Bundle, const SCert &GameServer);
// Whether the held bundle may still be used to answer a challenge
bool BundleUsable(const SBundle &Bundle, int64_t Fetched, uint64_t Now);

// Standard base64 with padding. -1 for anything else.
int Base64Decode(void *pDst, size_t DstSize, const char *pSrc, size_t SrcLength);

} // namespace KogTrust

#endif
