#ifndef ENGINE_SHARED_KOG_LOGIN_H
#define ENGINE_SHARED_KOG_LOGIN_H

#include <base/net.h>

#include <cstddef>
#include <cstdint>

// Log in with KoG: OAuth2 code flow with PKCE in the system browser, then a device key pair.
// Nothing here talks to the network except the loopback listener; see docs/LOGIN.md.
namespace KogLogin {

enum
{
	VERIFIER_LENGTH = 64,
	STATE_LENGTH = 32,
	CHALLENGE_LENGTH = 43,
	MAX_DEVICE_NAME = 40,
	MAX_CODE_LENGTH = 4096,
	MAX_REQUEST_SIZE = 8192,
	MAX_CONNECTIONS = 4,
	UUID_SIZE = 16,
	NONCE_SIZE = 32,
	SEED_SIZE = 32,
	PUBLIC_KEY_SIZE = 32,
	SIGNATURE_SIZE = 64,
	// base64url without padding of a signature
	PROOF_LENGTH = 86,
	// the first 16 hex characters of SHA-256 of the public key, as the account page of kog.tw shows it
	FINGERPRINT_LENGTH = 16,
	MAX_RETRY_MS = 5 * 60 * 1000,
};

constexpr const char *CLIENT_ID = "kog-client";
constexpr const char *SCOPE = "openid profile kog:login";
constexpr const char *CALLBACK_PATH = "/callback";
constexpr const char *CHALLENGE_PREFIX = "KoG-login-v1";
constexpr const char *REGISTER_PREFIX = "KoG-register-v1";

void Base64UrlEncode(char *pDst, size_t DstSize, const void *pData, size_t DataSize);
// Returns the number of bytes, or -1 if the input is not base64url without padding
int Base64UrlDecode(void *pDst, size_t DstSize, const char *pSrc);
void UrlEncode(char *pDst, size_t DstSize, const char *pSrc);
bool UrlDecode(char *pDst, size_t DstSize, const char *pSrc, size_t SrcLength);

// RFC 7636
void MakeVerifier(char (&aVerifier)[VERIFIER_LENGTH + 1]);
void MakeState(char (&aState)[STATE_LENGTH + 1]);
void CodeChallenge(const char *pVerifier, char (&aChallenge)[CHALLENGE_LENGTH + 1]);

// A neutral name for the platform, never the host name
void DefaultDeviceName(char *pBuf, size_t Size);
void CutDeviceName(char *pDst, size_t DstSize, const char *pSrc);

bool BuildAuthorizeUrl(char *pBuf, size_t Size, const char *pAuthorizeEndpoint, const char *pRedirectUri, const char *pState, const char *pChallenge, const char *pDeviceName);

enum class ECallback
{
	CODE,
	WRONG_PATH,
	WRONG_METHOD,
	MALFORMED,
	BAD_STATE,
	MISSING_CODE,
	SERVER_ERROR, // the account server answered with error=..., e.g. the user said no
};

// Looks only at the request line. The state is compared in constant time.
ECallback ParseCallback(const char *pRequest, size_t Length, const char *pExpectedState, char *pCode, size_t CodeSize, char *pError, size_t ErrorSize);

// The key pair of this device. The seed is the private key and must never leave the keystore.
struct SDeviceKey
{
	uint8_t m_aSeed[SEED_SIZE];
	char m_aId[64];
};
void GenerateSeed(uint8_t (&aSeed)[SEED_SIZE]);
void PublicKeyFromSeed(const uint8_t (&aSeed)[SEED_SIZE], uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE]);
void Wipe(void *pData, size_t Size);

// "8-4-4-4-12" hex to 16 bytes. False for anything else
bool ParseUuid(const char *pText, uint8_t (&aUuid)[UUID_SIZE]);

// Bytes that are signed: "KoG-login-v1" 0x00 || u8 len(addr) || addr || nonce[32] || device_id[16] || session_id[16].
// Length prefixed and fixed width, so two different tuples never give the same bytes. Returns 0 if it does not fit,
// the address is empty or longer than 255 bytes, or the nonce is not 32 bytes.
size_t BuildChallengeMessage(uint8_t *pOut, size_t OutSize, const char *pServerAddress, const uint8_t *pNonce, size_t NonceSize, const uint8_t (&aDeviceId)[UUID_SIZE], const uint8_t (&aSessionId)[UUID_SIZE]);
// pDeviceId is the UUID text of the device as the account server issued it
bool SignChallenge(const uint8_t (&aSeed)[SEED_SIZE], const char *pServerAddress, const uint8_t *pNonce, size_t NonceSize, const char *pDeviceId, const uint8_t (&aSessionId)[UUID_SIZE], uint8_t (&aSignature)[SIGNATURE_SIZE]);
bool VerifyChallenge(const uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE], const char *pServerAddress, const uint8_t *pNonce, size_t NonceSize, const char *pDeviceId, const uint8_t (&aSessionId)[UUID_SIZE], const uint8_t (&aSignature)[SIGNATURE_SIZE]);

// The proof of possession that comes with the registration of a device: the new key signs
// "KoG-register-v1" 0x00 || device_name (the bytes of the name as sent), so the account server knows the client holds the
// private key of the public key it registers. Returns 0 for an empty name or one that does not fit.
size_t BuildRegisterMessage(uint8_t *pOut, size_t OutSize, const char *pDeviceName);
// base64url of the signature, 86 characters
bool SignRegistration(const uint8_t (&aSeed)[SEED_SIZE], const char *pDeviceName, char (&aProof)[PROOF_LENGTH + 1]);
bool VerifyRegistration(const uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE], const char *pDeviceName, const char *pProof);
void Fingerprint(const uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE], char (&aFingerprint)[FINGERPRINT_LENGTH + 1]);

// How the account server answered the registration of a device (POST /api/client/device), by status and the `error` word
// of its JSON body
enum class ERegisterError
{
	NONE, // registered
	DEVICE_LIMIT, // the account has as many devices as it may: remove one on the website
	KEY_REGISTERED, // the public key is registered already
	INVALID_PROOF, // the proof of possession was not accepted
	RATE_LIMITED,
	UNAUTHORIZED, // the token was refused
	OTHER,
};
ERegisterError ClassifyRegisterAnswer(int Status, const char *pErrorWord);
// How long to wait before the next login after this many failures in a row: none for 0, then 5 s, doubling to 5 minutes,
// and at least a minute after a rate limit. There is no retry on a timer: this only gates the next click.
int64_t RetryDelayMs(int Failures, ERegisterError Error);

// Whether an endpoint named by the discovery document may be used. It must be https (http only if the base is http and
// the same origin) and have the origin of pBaseUrl, or a host ending in ".kog.tw" on the default https port, or an origin
// listed in pExtraOrigins (separated by spaces or commas, each "https://host[:port]").
bool EndpointAllowed(const char *pBaseUrl, const char *pEndpoint, const char *pExtraOrigins);

// The blob that the keystore holds: version, device id, seed
bool EncodeKey(const SDeviceKey &Key, char *pBuf, size_t Size);
bool DecodeKey(const char *pBlob, SDeviceKey *pKey);

struct SJoinDecision
{
	bool m_SwitchOn; // cl_kog_login
	bool m_AutoLoginOn; // cl_kog_auto_login
	bool m_HasDeviceKey;
	bool m_ServerIsKog; // the address is listed for the KoG community in the server list
	bool m_AlreadyLoggedIn;
	bool m_AlreadyAnsweredThisJoin;
	bool m_CapabilityNegotiated; // the server agreed to client_login in the KoG hello
	bool m_CertVerified; // the server proved with a valid, not revoked KoG certificate that KoG enrolled it
	bool m_CertBad; // the server presented a certificate and it is wrong, revoked, or the list to check it is missing
	bool m_CertRequired; // cl_kog_require_server_cert
};
// Whether this client may answer a login challenge of the server it is on. Vanilla and other servers: never.
bool MayAnswerChallenge(const SJoinDecision &Decision);

// The address of the server as the proof signs it: ip:port of the peer of the game connection, IPv4 as a.b.c.d:port,
// an IPv4-mapped IPv6 address as IPv4, other IPv6 compressed and in brackets. It is taken from the socket and never
// from a message of the server. False for a type that has no such form or for port 0.
bool CanonicalAddress(const NETADDR &Address, char *pBuf, size_t Size);

// Wait on 127.0.0.1 (or ::1 if that does not work) for the request GET /callback with the right state, then close.
// Up to MAX_CONNECTIONS connections are open at once, so a connection that only sits there (a browser preconnect, a
// foreign process) does not use up the single chance. Every connection has 5 seconds to send its request line.
class CLoopbackListener
{
public:
	enum class EState
	{
		IDLE,
		LISTENING,
		GOT_CODE,
		SERVER_ERROR,
		TIMED_OUT,
		CLOSED,
	};

	CLoopbackListener() = default;
	~CLoopbackListener() { Stop(); }
	CLoopbackListener(const CLoopbackListener &) = delete;
	CLoopbackListener &operator=(const CLoopbackListener &) = delete;

	bool Start(const char *pState, int64_t TimeoutMs);
	// Call once per frame. Returns the state after handling what has arrived.
	EState Poll();
	void Stop();

	EState State() const { return m_State; }
	int Port() const { return m_Port; }
	void RedirectUri(char *pBuf, size_t Size) const;
	const char *Code() const { return m_aCode; }
	const char *Error() const { return m_aError; }
	void WipeCode() { Wipe(m_aCode, sizeof(m_aCode)); }

private:
	NETSOCKET m_Listen = nullptr;
	struct SConn
	{
		NETSOCKET m_Socket = nullptr;
		int64_t m_Deadline = 0;
		size_t m_Length = 0;
		char m_aRequest[MAX_REQUEST_SIZE];
	};
	SConn m_aConns[MAX_CONNECTIONS];
	int m_Port = 0;
	bool m_Ipv6 = false;
	EState m_State = EState::IDLE;
	int64_t m_Deadline = 0;
	char m_aState[STATE_LENGTH + 1] = "";
	char m_aCode[MAX_CODE_LENGTH + 1] = "";
	char m_aError[128] = "";

	void CloseConn(SConn &Conn);
	void Answer(SConn &Conn, const char *pStatus, const char *pBody);
	void HandleRequest(SConn &Conn);
};

} // namespace KogLogin

#endif
