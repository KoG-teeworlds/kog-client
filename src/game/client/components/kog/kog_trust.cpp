#include "kog_trust.h"

#include <base/detect.h>
#include <base/log.h>
#include <base/io.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/client/kog_trust_pins.h>
#include <engine/shared/config.h>
#include <engine/shared/kog_attest.h>
#include <engine/shared/kog_login.h>
#include <engine/storage.h>

#include "kog_login.h"

#include <game/client/gameclient.h>
#include <game/kog_version.h>

namespace {

constexpr const char *CACHE_FILE = "kog_trust.json";
constexpr int64_t MIN_TRY_SECONDS = 5 * 60;

// The master server of this channel's environment serves the signed trust list next to the server list
const char *MasterHost()
{
	if(str_comp(KOG_CLIENT_CHANNEL, "canary") == 0)
		return "master.client.canary.kog.tw";
	if(str_comp(KOG_CLIENT_CHANNEL, "dev") == 0)
		return "master.client.dev.kog.tw";
	return "master.client.kog.tw";
}

const char *MirrorChannel()
{
	return str_comp(KOG_CLIENT_CHANNEL, "canary") == 0 || str_comp(KOG_CLIENT_CHANNEL, "dev") == 0 ? KOG_CLIENT_CHANNEL : "stable";
}

} // namespace

void CKogTrust::OnInit()
{
	if(str_comp(KOG_CLIENT_CHANNEL, "canary") == 0)
	{
		m_Anchors = KogTrust::AnchorsFromHex(KOG_TRUST_ANCHORS_CANARY, std::size(KOG_TRUST_ANCHORS_CANARY));
		m_pEnvironment = KOG_TRUST_ENVIRONMENT_CANARY;
	}
	else if(str_comp(KOG_CLIENT_CHANNEL, "dev") == 0)
	{
		m_Anchors = KogTrust::AnchorsFromHex(KOG_TRUST_ANCHORS_DEV, std::size(KOG_TRUST_ANCHORS_DEV));
		m_pEnvironment = KOG_TRUST_ENVIRONMENT_DEV;
	}
	else
	{
		m_Anchors = KogTrust::AnchorsFromHex(KOG_TRUST_ANCHORS_STABLE, std::size(KOG_TRUST_ANCHORS_STABLE));
		m_pEnvironment = KOG_TRUST_ENVIRONMENT_STABLE;
	}
	Load();
}

bool CKogTrust::Enabled() const
{
	return g_Config.m_ClKogServerCert != 0 && !m_Anchors.m_Keys.empty();
}

void CKogTrust::Load()
{
	void *pData;
	unsigned Size;
	if(!Storage()->ReadFile(CACHE_FILE, IStorage::TYPE_SAVE, &pData, &Size))
		return;
	// the first line is the time of the fetch, the rest is the file as served
	const char *pText = static_cast<const char *>(pData);
	const char *pNewline = static_cast<const char *>(memchr(pText, '\n', Size));
	int64_t Fetched = 0;
	if(pNewline != nullptr && pNewline - pText < 20)
	{
		char aTime[24];
		str_truncate(aTime, sizeof(aTime), pText, pNewline - pText);
		Fetched = str_toint64_base(aTime);
		KogTrust::SBundle Bundle;
		// the cache is checked again like a download: the file on disk is not trusted
		if(KogTrust::ParseBundle(m_Anchors, m_pEnvironment, pNewline + 1, Size - (pNewline + 1 - pText), time_timestamp(), 0, &Bundle) == KogTrust::EBundle::OK)
		{
			m_Bundle = Bundle;
			m_HasBundle = true;
			m_Fetched = Fetched;
		}
	}
	free(pData);
}

void CKogTrust::Save(const char *pFile, size_t Size)
{
	IOHANDLE File = Storage()->OpenFile(CACHE_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	char aTime[24];
	str_format(aTime, sizeof(aTime), "%" PRId64 "\n", m_Fetched);
	io_write(File, aTime, str_length(aTime));
	io_write(File, pFile, Size);
	io_close(File);
}

void CKogTrust::FormatAgent(char *pBuf, size_t Size) const
{
	str_format(pBuf, Size, "%s/%s (%s; %s)", KOG_CLIENT_EXECUTABLE, KOG_CLIENT_VERSION, CONF_PLATFORM_STRING, CONF_ARCH_STRING);
}

bool CKogTrust::SourceUrl(int Source, char *pBuf, size_t Size) const
{
	char aBase[300] = "";
	if(Source == 0 && g_Config.m_ClKogTrustUrl[0] != '\0')
		str_copy(aBase, g_Config.m_ClKogTrustUrl);
	else if(Source <= 1)
		str_format(aBase, sizeof(aBase), "https://%s/list/trust.json", MasterHost());
	else
		str_format(aBase, sizeof(aBase), "https://client.kog.tw/list/%s/trust.json", MirrorChannel());
	// the same rule as for the login: https and a kog.tw host
	if(!KogLogin::EndpointAllowed("https://kog.tw", aBase, ""))
		return false;
	str_copy(pBuf, aBase, Size);
	return true;
}

void CKogTrust::Start(int Source)
{
	char aUrl[300];
	m_Source = Source;
	// source 0 is the URL of the setting (when set), 1 the master server of the environment, 2 the mirror on client.kog.tw
	if(Source == 0 && g_Config.m_ClKogTrustUrl[0] == '\0')
		m_Source = Source = 1;
	if(!SourceUrl(Source, aUrl, sizeof(aUrl)))
	{
		if(Source < 2)
			Start(Source + 1);
		return;
	}
	std::shared_ptr<IHttpRequest> pRequest = CreateHttpRequest(aUrl);
	pRequest->Timeout(CTimeout{4000, 4000, 500, 5});
	pRequest->MaxResponseSize(KogTrust::MAX_BUNDLE_SIZE);
	pRequest->LogProgress(HTTPLOG::NONE);
	pRequest->FailOnErrorStatus(true);
	pRequest->FollowRedirects(true);
	pRequest->WriteToMemory();
	char aAgent[128];
	FormatAgent(aAgent, sizeof(aAgent));
	pRequest->HeaderString("User-Agent", aAgent);
	m_pRequest = pRequest;
	Http()->Run(pRequest);
}

void CKogTrust::OnRender()
{
	if(!m_pRequest || !m_pRequest->Done())
		return;
	std::shared_ptr<IHttpRequest> pRequest = std::move(m_pRequest);
	m_pRequest = nullptr;
	bool Good = false;
	if(pRequest->State() == EHttpState::DONE)
	{
		unsigned char *pResult;
		size_t Length;
		pRequest->Result(&pResult, &Length);
		KogTrust::SBundle Bundle;
		const KogTrust::EBundle Verdict = KogTrust::ParseBundle(m_Anchors, m_pEnvironment, reinterpret_cast<const char *>(pResult), Length, time_timestamp(), m_HasBundle ? m_Bundle.m_Seq : 0, &Bundle);
		Good = Verdict == KogTrust::EBundle::OK;
		if(Good)
		{
			m_Bundle = Bundle;
			m_HasBundle = true;
			m_Fetched = time_timestamp();
			Save(reinterpret_cast<const char *>(pResult), Length);
		}
	}
	// a host that is down or a file that is not signed by a pinned key: the next source
	if(!Good && m_Source < 2)
		Start(m_Source + 1);
}

void CKogTrust::RefreshSoon()
{
	const int64_t Now = time_timestamp();
	if(!Enabled() || m_pRequest || Now - m_LastTry < MIN_TRY_SECONDS)
		return;
	m_LastTry = Now;
	Start(0);
}

void CKogTrust::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_OFFLINE || NewState == IClient::STATE_CONNECTING)
	{
		m_LastVerdict = EVerdict::NOT_CHECKED;
		m_aLastSubject[0] = '\0';
		m_HasLeafHash = false;
	}
	// at the start of a connection to a KoG server, when a login is possible: look if the held list is old
	if(NewState == IClient::STATE_CONNECTING && Enabled() && g_Config.m_ClKogLogin && g_Config.m_ClKogAutoLogin &&
		(!m_HasBundle || time_timestamp() - m_Fetched > KogTrust::REFRESH_AFTER_SECONDS) &&
		CKogLogin::ServerIsKog(ServerBrowser(), Client()->ServerAddress()))
		RefreshSoon();
}

CKogTrust::EVerdict CKogTrust::Check(const char *pAddress, const KogLoginWire::SChallenge &Challenge)
{
	m_aLastSubject[0] = '\0';
	m_HasLeafHash = false;
	if(!Enabled())
		return m_LastVerdict = EVerdict::NOT_CHECKED;
	if(Challenge.m_Chain.empty())
		return m_LastVerdict = EVerdict::NO_CERT;

	const uint64_t Now = time_timestamp();
	KogTrust::SCert GameServer;
	if(KogTrust::VerifyChain(m_Anchors, Challenge.m_Chain.data(), Challenge.m_Chain.size(), Now, &GameServer) != KogTrust::EChain::OK)
	{
		RefreshSoon();
		return m_LastVerdict = EVerdict::INVALID;
	}
	uint8_t aSession[16];
	mem_copy(aSession, Challenge.m_aSessionId, sizeof(aSession));
	if(!KogTrust::VerifyServerSignature(GameServer.m_aPubkey, pAddress, Challenge.m_aNonce, sizeof(Challenge.m_aNonce), aSession, Challenge.m_TtlMs, Challenge.m_aServerSignature))
		return m_LastVerdict = EVerdict::INVALID;
	str_copy(m_aLastSubject, GameServer.m_Subject.c_str());
	// without a list that is recent enough the revocations cannot be checked
	if(!m_HasBundle || !KogTrust::BundleUsable(m_Bundle, m_Fetched, Now))
	{
		RefreshSoon();
		return m_LastVerdict = EVerdict::INVALID;
	}
	if(KogTrust::IsRevoked(m_Bundle, GameServer))
		return m_LastVerdict = EVerdict::REVOKED;
	const uint8_t *pLeaf = nullptr;
	size_t LeafSize = 0;
	if(KogTrust::LeafCert(Challenge.m_Chain.data(), Challenge.m_Chain.size(), &pLeaf, &LeafSize))
	{
		KogAttest::CertHash(pLeaf, LeafSize, m_aLeafHash);
		m_HasLeafHash = true;
	}
	return m_LastVerdict = EVerdict::OK;
}

bool CKogTrust::LeafHash(uint8_t (&aHash)[32]) const
{
	if(!m_HasLeafHash)
		return false;
	mem_copy(aHash, m_aLeafHash, sizeof(aHash));
	return true;
}
