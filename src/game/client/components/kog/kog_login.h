#ifndef GAME_CLIENT_COMPONENTS_KOG_KOG_LOGIN_H
#define GAME_CLIENT_COMPONENTS_KOG_KOG_LOGIN_H

#include <engine/console.h>
#include <engine/http.h>
#include <engine/shared/kog_login.h>

#include <game/client/component.h>

#include <memory>

// Where a login proof goes: the main connection of the client, see docs/LOGIN.md
class IKogLoginTransport
{
public:
	virtual ~IKogLoginTransport() = default;
	virtual bool SendProof(const char *pDeviceId, const uint8_t (&aSignature)[KogLogin::SIGNATURE_SIZE]) = 0;
};

class CKogLogin : public CComponent
{
public:
	enum class EStep
	{
		IDLE,
		DISCOVERY,
		BROWSER,
		TOKEN_EXCHANGE,
		USERINFO,
		DEVICE,
		REVOKE,
		DONE,
		FAILED,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;

	void StartLogin();
	// Wipes the private key at once, then asks the account server (through a browser step) to remove the device
	void StartLogout();
	void Cancel();

	bool Busy() const { return m_Step != EStep::IDLE && m_Step != EStep::DONE && m_Step != EStep::FAILED; }
	bool HasDeviceKey() const { return m_KeyState != EKeyState::NONE && m_KeyState != EKeyState::DAMAGED; }
	// A locked key cannot sign until Unlock succeeded
	bool KeyUsable() const { return m_KeyState == EKeyState::PLAIN || m_KeyState == EKeyState::UNLOCKED; }
	// The seed (private key) of the device key, for the attestation. False: missing, locked or damaged. The caller wipes it.
	bool LoadSeed(uint8_t (&aSeed)[KogLogin::SEED_SIZE]);
	bool KeyLocked() const { return m_KeyState == EKeyState::LOCKED; }
	bool KeyDamaged() const { return m_KeyState == EKeyState::DAMAGED; }
	bool KeyHasPassphrase() const { return m_KeyState == EKeyState::LOCKED || m_KeyState == EKeyState::UNLOCKED; }
	// The passphrase of the key file. They only ever change the file when it can be opened, and they set the message
	void Unlock(const char *pPassphrase);
	void SetPassphrase(const char *pNew, const char *pCurrent);
	void RemovePassphrase(const char *pCurrent);
	EStep Step() const { return m_Step; }
	// Plain words for the settings box
	const char *Message() const { return m_aMessage; }
	bool MessageIsError() const { return m_Step == EStep::FAILED; }

	// What happened on this server: nothing, the proof is with the server, or the server answered
	enum class EJoinState
	{
		NONE,
		WAITING,
		LOGGED_IN,
		REFUSED,
	};
	EJoinState JoinState() const { return m_JoinState; }
	const char *JoinAccountName() const { return m_aJoinAccountName; }
	// The messages kog-login-challenge and kog-login-result of the main connection
	void OnLoginChallenge(class CUnpacker *pUnpacker);
	void OnLoginResult(class CUnpacker *pUnpacker);

	// The challenge of the server, already read. True if it was answered.
	bool OnChallenge(const char *pServerAddress, const uint8_t *pNonce, size_t NonceSize, const uint8_t (&aSessionId)[KogLogin::UUID_SIZE], bool CertVerified, bool CertBad);
	void NoteManualLogin() { m_ManualLogin = true; }
	static bool ServerIsKog(class IServerBrowser *pBrowser, const NETADDR &Address);

private:
	enum class EPurpose
	{
		LOGIN,
		REVOKE,
	};

	enum class EKeyState
	{
		NONE,
		PLAIN,
		LOCKED,
		UNLOCKED,
		DAMAGED, // there is a file, and it can be read as neither a plain nor a locked key
	};

	EStep m_Step = EStep::IDLE;
	EPurpose m_Purpose = EPurpose::LOGIN;
	std::shared_ptr<IHttpRequest> m_pRequest;
	KogLogin::CLoopbackListener m_Listener;
	std::unique_ptr<IKogLoginTransport> m_pTransport;

	EKeyState m_KeyState = EKeyState::NONE;
	KogLogin::SDeviceKey m_UnlockedKey;
	bool m_ManualLogin = false;
	bool m_AnsweredThisJoin = false;
	EJoinState m_JoinState = EJoinState::NONE;
	char m_aJoinAccountName[64] = "";
	char m_aMessage[256] = "";

	char m_aAuthorizeEndpoint[256] = "";
	char m_aTokenEndpoint[256] = "";
	char m_aUserinfoEndpoint[256] = "";
	char m_aVerifier[KogLogin::VERIFIER_LENGTH + 1] = "";
	char m_aRedirectUri[64] = "";
	char m_aAccessToken[2048] = "";
	char m_aDeviceName[161] = "";
	char m_aAccountName[64] = "";
	KogLogin::SDeviceKey m_PendingKey;
	char m_aRevokeId[64] = "";

	static void ConKogKeyPassphrase(IConsole::IResult *pResult, void *pUserData);
	bool LoadKey(KogLogin::SDeviceKey *pKey);
	void RefreshKeyState();
	void WipeUnlocked();
	void SaveWithPassphrase(const char *pNew, const char *pCurrent);
	void Fail(const char *pMessage);
	void Succeed(const char *pMessage);
	void Reset();
	bool BaseUrl(char *pBuf, size_t Size) const;
	// Secret: no redirects, and the body and headers are overwritten when done
	void Request(std::shared_ptr<IHttpRequest> pRequest, bool Secret);
	void ReleaseRequest();
	void SetBearer(IHttpRequest *pRequest);
	void FailHttp(const char *pWhat);

	void BeginBrowser();
	void BeginToken(const char *pCode);
	void BeginUserinfo();
	void BeginDevice();
	void BeginRevoke();
	void StepDiscovery();
	void StepToken();
	void StepUserinfo();
	void StepDevice();
	void StepRevoke();
};

#endif
