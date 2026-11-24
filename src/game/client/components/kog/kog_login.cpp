#include "kog_login.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/client/kog_keystore.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

namespace {

constexpr int64_t BROWSER_TIMEOUT_MS = 3 * 60 * 1000;
constexpr const char *KOG_COMMUNITY_ID = "kog";

class CStubTransport : public IKogLoginTransport
{
public:
	bool SendProof(const char *pDeviceId, const uint8_t (&aSignature)[KogLogin::SIGNATURE_SIZE]) override { return false; }
};

} // namespace

void CKogLogin::OnInit()
{
	m_pTransport = std::make_unique<CStubTransport>();
	KogLogin::SDeviceKey Key;
	m_HasKey = LoadKey(&Key);
	KogLogin::Wipe(&Key, sizeof(Key));
	if(!m_HasKey)
		g_Config.m_ClKogAccountName[0] = '\0';
}

void CKogLogin::OnShutdown()
{
	Reset();
}

bool CKogLogin::LoadKey(KogLogin::SDeviceKey *pKey)
{
	char aBlob[256];
	const bool Ok = KogKeyStore::Load(Storage(), aBlob, sizeof(aBlob)) && KogLogin::DecodeKey(aBlob, pKey);
	KogLogin::Wipe(aBlob, sizeof(aBlob));
	return Ok;
}

void CKogLogin::Reset()
{
	if(m_pRequest)
		m_pRequest->Abort();
	ReleaseRequest();
	m_Listener.Stop();
	m_Listener.WipeCode();
	KogLogin::Wipe(m_aVerifier, sizeof(m_aVerifier));
	KogLogin::Wipe(m_aAccessToken, sizeof(m_aAccessToken));
	KogLogin::Wipe(&m_PendingKey, sizeof(m_PendingKey));
}

void CKogLogin::Fail(const char *pMessage)
{
	Reset();
	str_copy(m_aMessage, pMessage);
	m_Step = EStep::FAILED;
}

void CKogLogin::Succeed(const char *pMessage)
{
	Reset();
	str_copy(m_aMessage, pMessage);
	m_Step = EStep::DONE;
}

void CKogLogin::Cancel()
{
	if(!Busy())
		return;
	Reset();
	str_copy(m_aMessage, Localize("Cancelled."));
	m_Step = EStep::FAILED;
}

bool CKogLogin::BaseUrl(char *pBuf, size_t Size) const
{
	str_copy(pBuf, g_Config.m_ClKogAccountUrl, Size);
	size_t Length = str_length(pBuf);
	while(Length > 0 && pBuf[Length - 1] == '/')
		pBuf[--Length] = '\0';
	return str_startswith(pBuf, "https://") != nullptr || (g_Config.m_HttpAllowInsecure && str_startswith(pBuf, "http://") != nullptr);
}

void CKogLogin::ReleaseRequest()
{
	if(m_pRequest && m_pRequest->Done())
		m_pRequest->WipeSecrets();
	m_pRequest = nullptr;
}

void CKogLogin::Request(std::shared_ptr<IHttpRequest> pRequest, bool Secret)
{
	if(Secret)
	{
		pRequest->FollowRedirects(false);
		pRequest->Sensitive(true);
	}
	pRequest->LogProgress(HTTPLOG::NONE);
	pRequest->FailOnErrorStatus(false);
	pRequest->MaxResponseSize(256 * 1024);
	pRequest->Timeout(CTimeout{5000, 20000, 0, 0});
	m_pRequest = pRequest;
	Http()->Run(pRequest);
}

void CKogLogin::SetBearer(IHttpRequest *pRequest)
{
	char aHeader[2048 + 32];
	str_format(aHeader, sizeof(aHeader), "Authorization: Bearer %s", m_aAccessToken);
	pRequest->Header(aHeader);
	KogLogin::Wipe(aHeader, sizeof(aHeader));
}

void CKogLogin::FailHttp(const char *pWhat)
{
	char aMessage[256];
	if(m_pRequest->State() != EHttpState::DONE)
		str_format(aMessage, sizeof(aMessage), "%s: %s", pWhat, Localize("network problem, could not reach the KoG account server."));
	else
		str_format(aMessage, sizeof(aMessage), "%s: %s (%d)", pWhat, Localize("the KoG account server refused it, status"), m_pRequest->StatusCode());
	Fail(aMessage);
}

void CKogLogin::StartLogin()
{
	if(!g_Config.m_ClKogLogin || Busy())
		return;
	char aBase[256];
	if(!BaseUrl(aBase, sizeof(aBase)))
	{
		Fail(Localize("The account address must start with https://."));
		return;
	}
	Reset();
	m_Purpose = EPurpose::LOGIN;
	if(g_Config.m_ClKogDeviceName[0] != '\0')
		KogLogin::CutDeviceName(m_aDeviceName, sizeof(m_aDeviceName), g_Config.m_ClKogDeviceName);
	else
		KogLogin::DefaultDeviceName(m_aDeviceName, sizeof(m_aDeviceName));

	char aUrl[300];
	str_format(aUrl, sizeof(aUrl), "%s/.well-known/openid-configuration", aBase);
	str_copy(m_aMessage, Localize("Contacting the KoG account server…"));
	m_Step = EStep::DISCOVERY;
	Request(HttpGet(aUrl), false);
}

void CKogLogin::StartLogout()
{
	if(Busy())
		return;
	KogLogin::SDeviceKey Key;
	const bool Had = LoadKey(&Key);
	str_copy(m_aRevokeId, Key.m_aId);
	KogLogin::Wipe(&Key, sizeof(Key));
	KogKeyStore::Remove(Storage());
	m_HasKey = false;
	g_Config.m_ClKogAccountName[0] = '\0';
	Reset();
	if(!Had || !g_Config.m_ClKogLogin)
	{
		Succeed(Localize("Logged out on this device."));
		return;
	}
	char aBase[256];
	if(!BaseUrl(aBase, sizeof(aBase)))
	{
		Succeed(Localize("Logged out on this device."));
		return;
	}
	m_Purpose = EPurpose::REVOKE;
	char aUrl[300];
	str_format(aUrl, sizeof(aUrl), "%s/.well-known/openid-configuration", aBase);
	str_copy(m_aMessage, Localize("Logged out on this device. Removing it from your account…"));
	m_Step = EStep::DISCOVERY;
	Request(HttpGet(aUrl), false);
}

void CKogLogin::OnRender()
{
	switch(m_Step)
	{
	case EStep::DISCOVERY:
		StepDiscovery();
		break;
	case EStep::BROWSER:
	{
		const KogLogin::CLoopbackListener::EState State = m_Listener.Poll();
		if(State == KogLogin::CLoopbackListener::EState::GOT_CODE)
		{
			char aCode[KogLogin::MAX_CODE_LENGTH + 1];
			str_copy(aCode, m_Listener.Code());
			m_Listener.WipeCode();
			BeginToken(aCode);
			KogLogin::Wipe(aCode, sizeof(aCode));
		}
		else if(State == KogLogin::CLoopbackListener::EState::SERVER_ERROR)
		{
			if(str_comp(m_Listener.Error(), "access_denied") == 0)
				Fail(Localize("The login was refused in the browser."));
			else
				Fail(Localize("The KoG account server reported an error. Try again."));
		}
		else if(State == KogLogin::CLoopbackListener::EState::TIMED_OUT)
			Fail(Localize("Timed out: no answer from the browser within 3 minutes."));
		else if(State == KogLogin::CLoopbackListener::EState::CLOSED)
			Fail(Localize("The local login port was closed. Try again."));
		break;
	}
	case EStep::TOKEN_EXCHANGE:
		StepToken();
		break;
	case EStep::USERINFO:
		StepUserinfo();
		break;
	case EStep::DEVICE:
		StepDevice();
		break;
	case EStep::REVOKE:
		StepRevoke();
		break;
	default:
		break;
	}
}

void CKogLogin::StepDiscovery()
{
	if(!m_pRequest->Done())
		return;
	if(m_pRequest->State() != EHttpState::DONE || m_pRequest->StatusCode() != 200)
	{
		FailHttp(Localize("Login"));
		return;
	}
	json_value *pJson = m_pRequest->ResultJson();
	const json_value *pAuthorize = pJson ? json_object_get(pJson, "authorization_endpoint") : nullptr;
	const json_value *pToken = pJson ? json_object_get(pJson, "token_endpoint") : nullptr;
	const json_value *pUserinfo = pJson ? json_object_get(pJson, "userinfo_endpoint") : nullptr;
	const bool Valid = pAuthorize && pAuthorize->type == json_string && pToken && pToken->type == json_string;
	if(Valid)
	{
		str_copy(m_aAuthorizeEndpoint, pAuthorize->u.string.ptr);
		str_copy(m_aTokenEndpoint, pToken->u.string.ptr);
		str_copy(m_aUserinfoEndpoint, pUserinfo && pUserinfo->type == json_string ? pUserinfo->u.string.ptr : "");
	}
	if(pJson)
		json_value_free(pJson);
	ReleaseRequest();
	char aBase[256];
	const char *pExtra = g_Config.m_ClKogAccountExtraOrigins;
	if(!Valid || !BaseUrl(aBase, sizeof(aBase)) || !KogLogin::EndpointAllowed(aBase, m_aAuthorizeEndpoint, pExtra) || !KogLogin::EndpointAllowed(aBase, m_aTokenEndpoint, pExtra) ||
		(m_aUserinfoEndpoint[0] != '\0' && !KogLogin::EndpointAllowed(aBase, m_aUserinfoEndpoint, pExtra)))
	{
		Fail(Localize("The KoG account server sent an unusable login description."));
		return;
	}
	BeginBrowser();
}

void CKogLogin::BeginBrowser()
{
	char aState[KogLogin::STATE_LENGTH + 1];
	char aChallenge[KogLogin::CHALLENGE_LENGTH + 1];
	KogLogin::MakeVerifier(m_aVerifier);
	KogLogin::MakeState(aState);
	KogLogin::CodeChallenge(m_aVerifier, aChallenge);
	if(m_Purpose == EPurpose::REVOKE)
		str_copy(m_aDeviceName, "KoG Client");

	if(!m_Listener.Start(aState, BROWSER_TIMEOUT_MS))
	{
		Fail(Localize("Could not open a local port for the login. A firewall may be blocking it."));
		return;
	}
	m_Listener.RedirectUri(m_aRedirectUri, sizeof(m_aRedirectUri));
	char aUrl[2048];
	if(!KogLogin::BuildAuthorizeUrl(aUrl, sizeof(aUrl), m_aAuthorizeEndpoint, m_aRedirectUri, aState, aChallenge, m_aDeviceName) || !Client()->ViewLink(aUrl))
	{
		Fail(Localize("Could not open the browser."));
		return;
	}
	str_copy(m_aMessage, Localize("Finish the login in your browser. This waits for 3 minutes."));
	m_Step = EStep::BROWSER;
}

void CKogLogin::BeginToken(const char *pCode)
{
	char aCode[KogLogin::MAX_CODE_LENGTH * 3 + 1];
	char aRedirect[256];
	char aVerifier[KogLogin::VERIFIER_LENGTH * 3 + 1];
	KogLogin::UrlEncode(aCode, sizeof(aCode), pCode);
	KogLogin::UrlEncode(aRedirect, sizeof(aRedirect), m_aRedirectUri);
	KogLogin::UrlEncode(aVerifier, sizeof(aVerifier), m_aVerifier);
	char aBody[KogLogin::MAX_CODE_LENGTH * 3 + 512];
	str_format(aBody, sizeof(aBody), "grant_type=authorization_code&client_id=%s&code=%s&redirect_uri=%s&code_verifier=%s", KogLogin::CLIENT_ID, aCode, aRedirect, aVerifier);
	std::shared_ptr<IHttpRequest> pRequest = CreateHttpRequest(m_aTokenEndpoint);
	pRequest->PostForm(aBody);
	KogLogin::Wipe(aBody, sizeof(aBody));
	KogLogin::Wipe(aCode, sizeof(aCode));
	KogLogin::Wipe(aVerifier, sizeof(aVerifier));
	KogLogin::Wipe(m_aVerifier, sizeof(m_aVerifier));
	str_copy(m_aMessage, Localize("Checking the login…"));
	m_Step = EStep::TOKEN_EXCHANGE;
	Request(pRequest, true);
}

void CKogLogin::StepToken()
{
	if(!m_pRequest->Done())
		return;
	if(m_pRequest->State() != EHttpState::DONE || m_pRequest->StatusCode() != 200)
	{
		FailHttp(Localize("Login"));
		return;
	}
	unsigned char *pBody = nullptr;
	size_t Length = 0;
	m_pRequest->Result(&pBody, &Length);
	json_value *pJson = JsonParse(reinterpret_cast<const char *>(pBody), Length);
	const json_value *pAccess = pJson ? json_object_get(pJson, "access_token") : nullptr;
	const bool Valid = pAccess && pAccess->type == json_string && (size_t)str_length(pAccess->u.string.ptr) < sizeof(m_aAccessToken);
	if(Valid)
		str_copy(m_aAccessToken, pAccess->u.string.ptr);
	if(pJson)
		json_value_free(pJson);
	KogLogin::Wipe(pBody, Length);
	ReleaseRequest();
	if(!Valid)
	{
		Fail(Localize("The KoG account server did not return a login."));
		return;
	}
	if(m_Purpose == EPurpose::REVOKE)
		BeginRevoke();
	else if(m_aUserinfoEndpoint[0] != '\0')
		BeginUserinfo();
	else
		BeginDevice();
}

void CKogLogin::BeginUserinfo()
{
	std::shared_ptr<IHttpRequest> pRequest = CreateHttpRequest(m_aUserinfoEndpoint);
	SetBearer(pRequest.get());
	m_Step = EStep::USERINFO;
	Request(pRequest, true);
}

void CKogLogin::StepUserinfo()
{
	if(!m_pRequest->Done())
		return;
	m_aAccountName[0] = '\0';
	if(m_pRequest->State() == EHttpState::DONE && m_pRequest->StatusCode() == 200)
	{
		json_value *pJson = m_pRequest->ResultJson();
		for(const char *pKey : {"preferred_username", "name"})
		{
			const json_value *pName = pJson ? json_object_get(pJson, pKey) : nullptr;
			if(pName && pName->type == json_string && pName->u.string.ptr[0] != '\0')
			{
				str_copy(m_aAccountName, pName->u.string.ptr);
				str_sanitize_cc(m_aAccountName);
				break;
			}
		}
		if(pJson)
			json_value_free(pJson);
	}
	ReleaseRequest();
	BeginDevice();
}

void CKogLogin::BeginDevice()
{
	KogLogin::GenerateSeed(m_PendingKey.m_aSeed);
	uint8_t aPublicKey[KogLogin::PUBLIC_KEY_SIZE];
	KogLogin::PublicKeyFromSeed(m_PendingKey.m_aSeed, aPublicKey);
	char aPublic[64];
	KogLogin::Base64UrlEncode(aPublic, sizeof(aPublic), aPublicKey, sizeof(aPublicKey));
	char aName[512];
	EscapeJson(aName, sizeof(aName), m_aDeviceName);
	char aBody[1024];
	str_format(aBody, sizeof(aBody), "{\"device_name\":\"%s\",\"public_key\":\"%s\"}", aName, aPublic);

	char aBase[256], aUrl[300];
	BaseUrl(aBase, sizeof(aBase));
	str_format(aUrl, sizeof(aUrl), "%s/api/client/device", aBase);
	std::shared_ptr<IHttpRequest> pRequest = CreateHttpRequest(aUrl);
	pRequest->PostJson(aBody);
	SetBearer(pRequest.get());
	str_copy(m_aMessage, Localize("Registering this device…"));
	m_Step = EStep::DEVICE;
	Request(pRequest, true);
}

void CKogLogin::StepDevice()
{
	if(!m_pRequest->Done())
		return;
	if(m_pRequest->State() != EHttpState::DONE || m_pRequest->StatusCode() != 201)
	{
		FailHttp(Localize("Registering this device"));
		return;
	}
	json_value *pJson = m_pRequest->ResultJson();
	const json_value *pId = pJson ? json_object_get(pJson, "id") : nullptr;
	const bool Valid = pId && pId->type == json_string && pId->u.string.ptr[0] != '\0' && (size_t)str_length(pId->u.string.ptr) < sizeof(m_PendingKey.m_aId);
	if(Valid)
		str_copy(m_PendingKey.m_aId, pId->u.string.ptr);
	if(pJson)
		json_value_free(pJson);
	ReleaseRequest();
	if(!Valid)
	{
		Fail(Localize("The KoG account server did not return a device."));
		return;
	}
	char aBlob[256];
	const bool Stored = KogLogin::EncodeKey(m_PendingKey, aBlob, sizeof(aBlob)) && KogKeyStore::Save(Storage(), aBlob);
	KogLogin::Wipe(aBlob, sizeof(aBlob));
	if(!Stored)
	{
		Fail(Localize("Could not store the device key in the system keychain."));
		return;
	}
	m_HasKey = true;
	str_copy(g_Config.m_ClKogAccountName, m_aAccountName);
	char aMessage[256];
	if(m_aAccountName[0] != '\0')
		str_format(aMessage, sizeof(aMessage), "%s %s.", Localize("Logged in as"), m_aAccountName);
	else
		str_copy(aMessage, Localize("Logged in."));
	Succeed(aMessage);
}

void CKogLogin::BeginRevoke()
{
	char aBase[256], aUrl[400];
	BaseUrl(aBase, sizeof(aBase));
	char aId[192];
	KogLogin::UrlEncode(aId, sizeof(aId), m_aRevokeId);
	str_format(aUrl, sizeof(aUrl), "%s/api/client/device/%s", aBase, aId);
	std::shared_ptr<IHttpRequest> pRequest = CreateHttpRequest(aUrl);
	pRequest->Delete();
	SetBearer(pRequest.get());
	m_Step = EStep::REVOKE;
	Request(pRequest, true);
}

void CKogLogin::StepRevoke()
{
	if(!m_pRequest->Done())
		return;
	const int Status = m_pRequest->StatusCode();
	const bool Reached = m_pRequest->State() == EHttpState::DONE;
	ReleaseRequest();
	if(Reached && (Status == 200 || Status == 204))
		Succeed(Localize("Logged out. The device is removed from your account."));
	else if(Reached && Status == 404)
		Fail(Localize("Logged out on this device. It was not found on the account you signed in with in the browser: remove it on kog.tw."));
	else
		Fail(Localize("Logged out on this device, but it could not be removed from your account. Remove it on kog.tw."));
}

void CKogLogin::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE)
	{
		m_ManualLogin = false;
		m_AnsweredThisJoin = false;
	}
}

bool CKogLogin::ServerIsKog(IServerBrowser *pBrowser, const NETADDR &Address)
{
	const IServerBrowser::CServerEntry *pEntry = pBrowser->Find(Address);
	return pEntry != nullptr && str_comp(pEntry->m_Info.m_aCommunityId, KOG_COMMUNITY_ID) == 0;
}

bool CKogLogin::OnChallenge(const char *pServerAddress, const uint8_t *pNonce, size_t NonceSize, const uint8_t (&aSessionId)[KogLogin::UUID_SIZE])
{
	KogLogin::SJoinDecision Decision;
	Decision.m_SwitchOn = g_Config.m_ClKogLogin != 0;
	Decision.m_AutoLoginOn = g_Config.m_ClKogAutoLogin != 0;
	Decision.m_HasDeviceKey = m_HasKey;
	Decision.m_ServerIsKog = ServerIsKog(ServerBrowser(), Client()->ServerAddress());
	Decision.m_AlreadyLoggedIn = m_ManualLogin;
	Decision.m_AlreadyAnsweredThisJoin = m_AnsweredThisJoin;
	if(!KogLogin::MayAnswerChallenge(Decision))
		return false;

	KogLogin::SDeviceKey Key;
	uint8_t aSignature[KogLogin::SIGNATURE_SIZE];
	bool Sent = false;
	if(LoadKey(&Key) && KogLogin::SignChallenge(Key.m_aSeed, pServerAddress, pNonce, NonceSize, Key.m_aId, aSessionId, aSignature))
	{
		m_AnsweredThisJoin = true;
		Sent = m_pTransport->SendProof(Key.m_aId, aSignature);
	}
	KogLogin::Wipe(&Key, sizeof(Key));
	return Sent;
}
