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
	MAX_CODE_LENGTH = 1024,
	SEED_SIZE = 32,
	PUBLIC_KEY_SIZE = 32,
	SIGNATURE_SIZE = 64,
};

constexpr const char *CLIENT_ID = "kog-client";
constexpr const char *SCOPE = "openid profile kog:login";
constexpr const char *CALLBACK_PATH = "/callback";
constexpr const char *CHALLENGE_PREFIX = "KoG-login-v1";

void Base64UrlEncode(char *pDst, size_t DstSize, const void *pData, size_t DataSize);
// Returns the number of bytes, or -1 if the input is not base64url without padding
int Base64UrlDecode(void *pDst, size_t DstSize, const char *pSrc);
void UrlEncode(char *pDst, size_t DstSize, const char *pSrc);
bool UrlDecode(char *pDst, size_t DstSize, const char *pSrc, size_t SrcLength);

// RFC 7636
void MakeVerifier(char (&aVerifier)[VERIFIER_LENGTH + 1]);
void MakeState(char (&aState)[STATE_LENGTH + 1]);
void CodeChallenge(const char *pVerifier, char (&aChallenge)[CHALLENGE_LENGTH + 1]);

// The host name cut to 40 bytes at a character boundary
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

// Bytes that are signed: "KoG-login-v1" || server host:port as dialed || nonce || device id, concatenated as they are
size_t BuildChallengeMessage(uint8_t *pOut, size_t OutSize, const char *pServerAddress, const uint8_t *pNonce, size_t NonceSize, const char *pDeviceId);
bool SignChallenge(const uint8_t (&aSeed)[SEED_SIZE], const char *pServerAddress, const uint8_t *pNonce, size_t NonceSize, const char *pDeviceId, uint8_t (&aSignature)[SIGNATURE_SIZE]);
bool VerifyChallenge(const uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE], const char *pServerAddress, const uint8_t *pNonce, size_t NonceSize, const char *pDeviceId, const uint8_t (&aSignature)[SIGNATURE_SIZE]);

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
};
// Whether this client may answer a login challenge of the server it is on. Vanilla and other servers: never.
bool MayAnswerChallenge(const SJoinDecision &Decision);

// Wait for one request on 127.0.0.1 (or ::1 if that does not work), accept the first one that matches, then close.
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
	NETSOCKET m_Conn = nullptr;
	int m_Port = 0;
	bool m_Ipv6 = false;
	EState m_State = EState::IDLE;
	int64_t m_Deadline = 0;
	int64_t m_ConnDeadline = 0;
	char m_aState[STATE_LENGTH + 1] = "";
	char m_aRequest[4096];
	size_t m_RequestLength = 0;
	char m_aCode[MAX_CODE_LENGTH + 1] = "";
	char m_aError[128] = "";

	void CloseConn();
	void Answer(const char *pStatus, const char *pBody);
	void HandleRequest();
};

} // namespace KogLogin

#endif
