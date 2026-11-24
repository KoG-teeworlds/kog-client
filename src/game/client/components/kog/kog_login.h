#ifndef GAME_CLIENT_COMPONENTS_KOG_KOG_LOGIN_H
#define GAME_CLIENT_COMPONENTS_KOG_KOG_LOGIN_H

#include <engine/http.h>
#include <engine/shared/kog_login.h>

#include <game/client/component.h>

#include <memory>

// Where a login proof goes. The protocol is not defined yet; the stub sends nothing.
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
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;

	void StartLogin();
	// Wipes the private key at once, then asks the account server (through a browser step) to remove the device
	void StartLogout();
	void Cancel();

	bool Busy() const { return m_Step != EStep::IDLE && m_Step != EStep::DONE && m_Step != EStep::FAILED; }
	bool HasDeviceKey() const { return m_HasKey; }
	EStep Step() const { return m_Step; }
	// Plain words for the settings box
	const char *Message() const { return m_aMessage; }
	bool MessageIsError() const { return m_Step == EStep::FAILED; }

	// To be called by the handler of the server's challenge once the protocol exists
	bool OnChallenge(const char *pServerAddress, const uint8_t *pNonce, size_t NonceSize, const uint8_t (&aSessionId)[KogLogin::UUID_SIZE]);
	void NoteManualLogin() { m_ManualLogin = true; }
	static bool ServerIsKog(class IServerBrowser *pBrowser, const NETADDR &Address);

private:
	enum class EPurpose
	{
		LOGIN,
		REVOKE,
	};

	EStep m_Step = EStep::IDLE;
	EPurpose m_Purpose = EPurpose::LOGIN;
	std::shared_ptr<IHttpRequest> m_pRequest;
	KogLogin::CLoopbackListener m_Listener;
	std::unique_ptr<IKogLoginTransport> m_pTransport;

	bool m_HasKey = false;
	bool m_ManualLogin = false;
	bool m_AnsweredThisJoin = false;
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

	bool LoadKey(KogLogin::SDeviceKey *pKey);
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
