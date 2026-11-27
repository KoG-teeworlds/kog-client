#ifndef ENGINE_SHARED_KOG_ATTEST_H
#define ENGINE_SHARED_KOG_ATTEST_H

#include <cstddef>
#include <cstdint>

class CAbstractPacker;
class CUnpacker;

// The install key answers a fresh challenge of a KoG server: kog-attest-challenge (server to client) and
// kog-attest-response (client to server). Never used unless the server agreed to client_attest in the KoG hello.
// Layouts and the signed bytes: docs/CLIENT-ATTEST.md, and docs/CLIENT-ATTESTATION.md section 10 of kog-2.0-server.
namespace KogAttest {

enum
{
	REVISION = 1,
	NONCE_SIZE = 32,
	SESSION_ID_SIZE = 16,
	KEY_ID_SIZE = 16,
	HASH_SIZE = 32,
	MEASUREMENT_SIZE = 32,
	PUBLIC_KEY_SIZE = 32,
	SEED_SIZE = 32,
	SIGNATURE_SIZE = 64,
	MAX_RANGES = 4,
	// the server sends 30 s; a challenge that asks for a much longer or shorter time is not one
	MIN_TTL_MS = 1000,
	MAX_TTL_MS = 60 * 1000,
	// the server asks at most four times per session and not twice within 30 minutes
	MAX_ANSWERS_PER_JOIN = 4,
	MIN_SPACING_MS = 60 * 1000,
	MAX_ADDRESS_SIZE = 255,
	MAX_SIGNED_SIZE = 14 + 1 + MAX_ADDRESS_SIZE + NONCE_SIZE + SESSION_ID_SIZE + HASH_SIZE + HASH_SIZE + KEY_ID_SIZE + MEASUREMENT_SIZE,
};

constexpr const char *SIGN_DOMAIN = "KoG-attest-v1";

// The answer to a challenge. Only OK is ever sent: a client that has no key, or does not want to answer, says nothing
enum class EStatus
{
	OK = 0,
	NO_KEY = 1,
	DECLINED = 2,
};

struct SRange
{
	int m_Module = 0;
	int m_Offset = 0;
	int m_Length = 0;
};

struct SChallenge
{
	int m_Revision = 0;
	uint8_t m_aNonce[NONCE_SIZE] = {};
	uint8_t m_aSessionId[SESSION_ID_SIZE] = {};
	int m_TtlMs = 0;
	// the optional measurement; this client does not make one and answers zeros
	int m_NumRanges = 0;
	SRange m_aRanges[MAX_RANGES];
	// revision 2: the hash of the server certificate the server says it showed
	bool m_HasCertHash = false;
	uint8_t m_aCertHash[HASH_SIZE] = {};
};

// Reads the fields after the message id. False: not understood, nothing is signed.
bool UnpackChallenge(CUnpacker *pUnpacker, SChallenge *pChallenge);

// revision, key id (16 bytes), status, measurement (32 bytes), signature (64 bytes)
void PackResponse(CAbstractPacker *pPacker, const uint8_t (&aKeyId)[KEY_ID_SIZE], EStatus Status, const uint8_t (&aMeasurement)[MEASUREMENT_SIZE], const uint8_t (&aSignature)[SIGNATURE_SIZE]);

// The first 16 bytes of SHA-256 of the public key
void KeyId(const uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE], uint8_t (&aKeyId)[KEY_ID_SIZE]);
// SHA-256 of the fields of the kog-client-hello this connection sent (after the message id)
void HelloHash(const void *pHello, size_t Size, uint8_t (&aHash)[HASH_SIZE]);
// SHA-256 of the server certificate as it came in the login challenge
void CertHash(const void *pCert, size_t Size, uint8_t (&aHash)[HASH_SIZE]);

// Bytes that are signed:
// "KoG-attest-v1" 0x00 || u8 len(addr) || addr || nonce[32] || session_id[16] || server_cert_hash[32]
//   || SHA-256(client hello)[32] || key_id[16] || measurement[32].
// addr is the address this client dialled, never one from a message of the server. Returns 0 if the address is empty
// or longer than 255 bytes, or the output is too small.
size_t BuildSignedBytes(uint8_t *pOut, size_t OutSize, const char *pAddress, const uint8_t (&aNonce)[NONCE_SIZE], const uint8_t (&aSessionId)[SESSION_ID_SIZE], const uint8_t (&aCertHash)[HASH_SIZE], const uint8_t (&aHelloHash)[HASH_SIZE], const uint8_t (&aKeyId)[KEY_ID_SIZE], const uint8_t (&aMeasurement)[MEASUREMENT_SIZE]);

struct SInputs
{
	const char *m_pAddress = ""; // as dialled, see KogLogin::CanonicalAddress
	uint8_t m_aCertHash[HASH_SIZE] = {}; // of the leaf certificate this client verified in this join, zeros when none
	uint8_t m_aHelloHash[HASH_SIZE] = {};
	uint8_t m_aMeasurement[MEASUREMENT_SIZE] = {};
};

// Signs with the install key. Fills the key id and the signature. False if the signed bytes cannot be built.
bool Sign(const uint8_t (&aSeed)[SEED_SIZE], const SInputs &Inputs, const SChallenge &Challenge, uint8_t (&aKeyId)[KEY_ID_SIZE], uint8_t (&aSignature)[SIGNATURE_SIZE]);
bool Verify(const uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE], const SInputs &Inputs, const SChallenge &Challenge, const uint8_t (&aKeyId)[KEY_ID_SIZE], const uint8_t (&aSignature)[SIGNATURE_SIZE]);

enum class EDecision
{
	ANSWER,
	SWITCH_OFF, // cl_kog_login or cl_kog_auto_login is off
	NOT_KOG, // the address is not listed for the KoG community
	NO_CAPABILITY, // the server did not agree to client_attest in the hello
	NO_KEY, // no enrolled key, or it is locked or damaged: nothing is said
	CERT_BAD, // the server presented a certificate and it is wrong or revoked
	CERT_REQUIRED, // cl_kog_require_server_cert and the server proved nothing
	CERT_MISMATCH, // the challenge names another certificate than the one this client verified
	REPLAY, // this nonce was answered already
	TOO_MANY, // four answers in this join
	TOO_SOON, // less than a minute after the last answer
};

struct SDecision
{
	bool m_SwitchOn; // cl_kog_login
	bool m_ProofOn; // cl_kog_auto_login: the player let this client prove the device when joining
	bool m_HasKey;
	bool m_ServerIsKog;
	bool m_CapabilityNegotiated;
	bool m_CertVerified;
	bool m_CertBad;
	bool m_CertRequired;
};

// What one join has already answered. Reset at every new connection.
class CJoin
{
public:
	void Reset();
	// Whether this challenge may be answered. Vanilla and other servers, a client that is not enrolled, a replayed
	// nonce, too many or too quick challenges: never. NowMs is a monotonic clock.
	EDecision Decide(const SDecision &Decision, const SChallenge &Challenge, const SInputs &Inputs, int64_t NowMs) const;
	// Remember that this challenge is being answered. Call before signing: a failure is not retried.
	void Commit(const SChallenge &Challenge, int64_t NowMs);
	int Answers() const { return m_Answers; }

private:
	uint8_t m_aNonces[MAX_ANSWERS_PER_JOIN][NONCE_SIZE] = {};
	int m_Answers = 0;
	int64_t m_LastMs = 0;
};

const char *DecisionLabel(EDecision Decision);

}

#endif
