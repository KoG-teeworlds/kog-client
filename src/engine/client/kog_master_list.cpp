#include "kog_master_list.h"

#include "kog_master_pubkey.h"

#include <base/log.h>
#include <base/io.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/kog_version.h>

#include <algorithm>
#include <cstring>

static constexpr const char *STATE_FILE = "kog_master_state.txt";
static constexpr const char *CACHE_FILE = "kog_master_list.json";
static constexpr int64_t MIN_SECONDS_BETWEEN_FETCHES = 30;
static constexpr size_t MAX_ATTEMPTS = 4;

#ifdef KOG_MASTER_DEV_ROOT
static constexpr bool DEV_OVERRIDE = true;
#else
static constexpr bool DEV_OVERRIDE = false;
#endif

struct SChannelMaster
{
	const char *m_pChannel;
	const char *const *m_ppKeys;
	int m_NumKeys;
	const char *m_pUrl;
	const char *m_pMirror;
};

static const SChannelMaster *ThisChannel()
{
	static const SChannelMaster s_aChannels[] = {
		{"stable", KOG_MASTER_STABLE_ROOT_KEYS, (int)std::size(KOG_MASTER_STABLE_ROOT_KEYS), KOG_MASTER_STABLE_URL, KOG_MASTER_STABLE_MIRROR_URL},
		{"canary", KOG_MASTER_CANARY_ROOT_KEYS, (int)std::size(KOG_MASTER_CANARY_ROOT_KEYS), KOG_MASTER_CANARY_URL, KOG_MASTER_CANARY_MIRROR_URL},
		{"dev", KOG_MASTER_DEV_ROOT_KEYS, (int)std::size(KOG_MASTER_DEV_ROOT_KEYS), KOG_MASTER_DEV_URL, KOG_MASTER_DEV_MIRROR_URL},
	};
	for(const SChannelMaster &Channel : s_aChannels)
	{
		if(str_comp(Channel.m_pChannel, KOG_CLIENT_CHANNEL) == 0)
			return &Channel;
	}
	return nullptr;
}

int CKogMasterList::RootKeys(std::vector<KogUpdate::SPublicKey> *pOut) const
{
	pOut->clear();
	const SChannelMaster *pChannel = ThisChannel();
	std::vector<const char *> vHex;
	if(pChannel)
	{
		for(int i = 0; i < pChannel->m_NumKeys; i++)
			vHex.push_back(pChannel->m_ppKeys[i]);
	}
#ifdef KOG_MASTER_DEV_ROOT
	vHex.push_back(KOG_MASTER_DEV_ROOT);
#endif
	for(const char *pHex : vHex)
	{
		KogUpdate::SPublicKey Key;
		if(str_length(pHex) == 64 && str_hex_decode(Key.m_aKey, sizeof(Key.m_aKey), pHex) == 0)
			pOut->push_back(Key);
	}
	return (int)pOut->size();
}

bool CKogMasterList::Enabled() const
{
	std::vector<KogUpdate::SPublicKey> vKeys;
	return g_Config.m_KogMaster != 0 && g_Config.m_KogMasterUrl[0] != '\0' && RootKeys(&vKeys) > 0;
}

void CKogMasterList::Init(IStorage *pStorage, IHttp *pHttp)
{
	m_pStorage = pStorage;
	m_pHttp = pHttp;
	if(!Enabled())
		return;
	char *pText = m_pStorage->ReadFileStr(STATE_FILE, IStorage::TYPE_SAVE);
	m_State = KogMaster::ParseState(pText, KOG_CLIENT_CHANNEL);
	free(pText);
	LoadCache();
}

void CKogMasterList::Shutdown()
{
	if(m_pRequest)
	{
		m_pRequest->Abort();
		m_pRequest = nullptr;
	}
}

bool CKogMasterList::TakeChanged()
{
	const bool Changed = m_Changed;
	m_Changed = false;
	return Changed;
}

void CKogMasterList::BuildUrls()
{
	m_vUrls.clear();
	const SChannelMaster *pChannel = ThisChannel();
	const char *pDevBase = DEV_OVERRIDE ? g_Config.m_KogMasterUrl : "";
	const char *pPrimary = g_Config.m_KogMasterUrl;
	auto Add = [&](const char *pUrl) {
		if(pUrl[0] != '\0' && KogMaster::UrlAllowed(pUrl, pDevBase) && std::find(m_vUrls.begin(), m_vUrls.end(), pUrl) == m_vUrls.end())
			m_vUrls.emplace_back(pUrl);
	};
	Add(pPrimary);
	// The mirror belongs to the official address: a URL set by hand is not followed by it
	if(pChannel && str_comp(pPrimary, pChannel->m_pUrl) == 0)
		Add(pChannel->m_pMirror);
	for(const std::string &Master : m_vLearnedMasters)
		Add(Master.c_str());
	if(m_vUrls.size() > MAX_ATTEMPTS)
		m_vUrls.resize(MAX_ATTEMPTS);
}

void CKogMasterList::Refresh()
{
	if(m_pRequest || !Enabled())
		return;
	const int64_t Now = time_timestamp();
	if(m_LastStart != 0 && Now - m_LastStart < MIN_SECONDS_BETWEEN_FETCHES)
		return;
	m_LastStart = Now;
	BuildUrls();
	m_Attempt = 0;
	StartAttempt();
}

void CKogMasterList::StartAttempt()
{
	if(m_Attempt >= m_vUrls.size())
	{
		m_pRequest = nullptr;
		return;
	}
	m_pRequest = HttpGet(m_vUrls[m_Attempt].c_str());
	m_pRequest->KogUserAgent();
	m_pRequest->WriteToMemory();
	m_pRequest->FollowRedirects(false);
	m_pRequest->MaxResponseSize((int64_t)KogMaster::MAX_DOCUMENT_SIZE);
	m_pRequest->Timeout(CTimeout{10000, 0, 500, 10});
	m_pHttp->Run(m_pRequest);
}

void CKogMasterList::Update()
{
	if(!m_pRequest || !m_pRequest->Done())
		return;
	std::shared_ptr<IHttpRequest> pRequest = std::move(m_pRequest);
	m_pRequest = nullptr;
	bool Ok = false;
	if(pRequest->State() == EHttpState::DONE && pRequest->StatusCode() == 200)
	{
		unsigned char *pResult;
		size_t Length;
		pRequest->Result(&pResult, &Length);
		const int64_t SeqBefore = m_State.m_ListSeq;
		Ok = Accept(reinterpret_cast<const char *>(pResult), Length, false);
		if(Ok && m_State.m_ListSeq != SeqBefore)
			Save(reinterpret_cast<const char *>(pResult), Length);
	}
	else
		log_debug("kogmaster", "Could not fetch %s", m_vUrls[m_Attempt].c_str());
	if(!Ok)
	{
		m_Attempt++;
		StartAttempt();
	}
}

bool CKogMasterList::Accept(const char *pBody, size_t Length, bool FromCache)
{
	std::vector<KogUpdate::SPublicKey> vKeys;
	RootKeys(&vKeys);
	KogMaster::SVerifyInput Input;
	Input.m_pChannel = KOG_CLIENT_CHANNEL;
	Input.m_pRoots = vKeys.data();
	Input.m_NumRoots = (int)vKeys.size();
	Input.m_Now = time_timestamp();
	Input.m_LastDelegationSeq = m_State.m_DelegationSeq;
	Input.m_LastListSeq = m_State.m_ListSeq;
	Input.m_AllowStale = FromCache;
	Input.m_pDevBaseUrl = DEV_OVERRIDE ? g_Config.m_KogMasterUrl : "";
	KogMaster::SList List;
	const KogMaster::EResult Result = KogMaster::Verify(pBody, Length, Input, &List);
	const KogMaster::EOutcome Outcome = KogMaster::Classify(Result);
	if(Outcome == KogMaster::EOutcome::KEEP_CURRENT)
	{
		log_debug("kogmaster", "Ignoring an older copy of the list (%s)", KogMaster::ResultName(Result));
		return !FromCache;
	}
	if(Outcome != KogMaster::EOutcome::ACCEPT)
	{
		log_info("kogmaster", "Ignoring the %s list: %s", FromCache ? "cached" : "fetched", KogMaster::ResultName(Result));
		return false;
	}
	m_List = std::move(List);
	m_Stale = Input.m_Now > m_List.m_NotAfter + KogMaster::CLOCK_SKEW_SECONDS;
	m_Changed = true;
	if(!FromCache)
	{
		m_vLearnedMasters = m_List.m_vMasters;
		m_vLearnedFallbacks = m_List.m_vDdnetFallbacks;
		m_State.m_DelegationSeq = std::max(m_State.m_DelegationSeq, m_List.m_DelegationSeq);
		m_State.m_ListSeq = std::max(m_State.m_ListSeq, m_List.m_Seq);
		const std::string Text = KogMaster::SerializeState(KOG_CLIENT_CHANNEL, m_State);
		IOHANDLE File = m_pStorage->OpenFile(STATE_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(File)
		{
			io_write(File, Text.data(), Text.size());
			io_close(File);
		}
	}
	log_info("kogmaster", "Accepted a list with %d servers", (int)m_List.m_vServers.size());
	return true;
}

void CKogMasterList::Save(const char *pBody, size_t Length)
{
	IOHANDLE File = m_pStorage->OpenFile(CACHE_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File)
	{
		io_write(File, pBody, Length);
		io_close(File);
	}
}

void CKogMasterList::LoadCache()
{
	void *pData;
	unsigned Length;
	if(!m_pStorage->ReadFile(CACHE_FILE, IStorage::TYPE_SAVE, &pData, &Length))
		return;
	Accept(static_cast<const char *>(pData), Length, true);
	free(pData);
}
