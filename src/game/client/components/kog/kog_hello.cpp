#include "kog_hello.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/packer.h>

#include <game/client/gameclient.h>
#include <game/kog_version.h>

void CKogHello::ConKogVersion(IConsole::IResult *pResult, void *pUserData)
{
	CKogHello *pThis = static_cast<CKogHello *>(pUserData);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s %s (based on TClient %s and DDNet %s)", KOG_CLIENT_NAME, KOG_CLIENT_VERSION, KOG_BASED_ON_TCLIENT, KOG_BASED_ON_DDNET);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "kog", aBuf);
}

void CKogHello::ConKogCaps(IConsole::IResult *pResult, void *pUserData)
{
	CKogHello *pThis = static_cast<CKogHello *>(pUserData);
	char aOffer[128];
	char aAgreed[128];
	KogHello::FormatMask(pThis->Offer(), aOffer, sizeof(aOffer));
	KogHello::FormatMask(pThis->m_Negotiated, aAgreed, sizeof(aAgreed));
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "kog_hello=%d, this client offers: %s; hello %s on this connection; agreed with the server: %s", g_Config.m_KogHello, aOffer, pThis->m_Sent ? (pThis->m_Answered ? "sent and answered" : "sent, no answer") : "not sent", aAgreed);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "kog", aBuf);
}

void CKogHello::OnConsoleInit()
{
	Console()->Register("kog_version", "", CFGFLAG_CLIENT, ConKogVersion, this, "Print the version of this client and what it is based on");
	Console()->Register("kog_caps", "", CFGFLAG_CLIENT, ConKogCaps, this, "Print which KoG features this client offers and which ones the server agreed to");
}

void CKogHello::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_OFFLINE || NewState == IClient::STATE_CONNECTING)
	{
		m_Offered = 0;
		m_Negotiated = 0;
		m_Sent = false;
		m_Answered = false;
	}
}

uint32_t CKogHello::Offer() const
{
	uint32_t Offer = 0;
	if(g_Config.m_ClKogLogin && g_Config.m_ClKogAutoLogin && GameClient()->m_KogLogin.HasDeviceKey())
		Offer |= KogHello::Bit(EKogCap::CLIENT_LOGIN);
	Offer |= KogHello::Bit(EKogCap::NOTICES);
	return Offer;
}

bool CKogHello::MakeHello(CAbstractPacker *pPacker)
{
	m_Offered = 0;
	m_Negotiated = 0;
	m_Sent = false;
	m_Answered = false;

	KogHello::SHelloDecision Decision;
	Decision.m_SwitchOn = g_Config.m_KogHello != 0;
	Decision.m_ServerIsKog = CKogLogin::ServerIsKog(ServerBrowser(), Client()->ServerAddress());
	Decision.m_MainConnection = true;
	Decision.m_Sixup = Client()->IsSixup();
	Decision.m_Offer = Offer();
	if(!KogHello::MaySendHello(Decision))
		return false;

	KogHello::PackClientHello(pPacker, Decision.m_Offer, KOG_CLIENT_VERSION, KOG_CLIENT_CHANNEL);
	m_Offered = Decision.m_Offer;
	m_Sent = true;
	return true;
}

void CKogHello::OnServerHello(CUnpacker *pUnpacker)
{
	if(!m_Sent || m_Answered)
		return;
	m_Answered = true;
	KogHello::SServerHello Hello;
	if(!KogHello::UnpackServerHello(pUnpacker, &Hello))
		return;
	m_Negotiated = KogHello::Agreed(m_Offered, Hello.m_Mask);
}
