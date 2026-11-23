#include "updater.h"

#include "kog_update_pubkey.h"

#include <base/fs.h>
#include <base/io.h>
#include <base/log.h>
#include <base/secure.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/engine.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/kog_version.h>

#include <cstdlib>
#include <iterator>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#endif

static constexpr const char *STATE_FILE = "kog_update_state.txt";
static constexpr const char *MARKER_FILE = "kog_start_marker";
static constexpr const char *CONFIG_FILES[] = {"settings_ddnet.cfg", "settings_tclient.cfg"};

class CUpdaterFetchTask : public IHttpRequest::IProgressCallback
{
	CUpdater *m_pUpdater;
	std::shared_ptr<IHttpRequest> m_pHttpRequest;

protected:
	void OnProgress() override;
	void OnCompletion(EHttpState) override {}

public:
	CUpdaterFetchTask(CUpdater *pUpdater, const char *pUrl, const KogUpdate::SFile &File, const char *pDestPath);
	std::shared_ptr<IHttpRequest> HttpRequest() { return m_pHttpRequest; }
};

class CStorageFiles : public KogUpdate::IFileOps
{
	IStorage *m_pStorage;

public:
	explicit CStorageFiles(IStorage *pStorage) :
		m_pStorage(pStorage) {}
	bool Exists(const std::string &Path) override
	{
		char aPath[IO_MAX_PATH_LENGTH];
		m_pStorage->GetBinaryPath(Path.c_str(), aPath, sizeof(aPath));
		return fs_is_file(aPath);
	}
	bool Rename(const std::string &From, const std::string &To) override { return m_pStorage->RenameBinaryFile(From.c_str(), To.c_str()); }
	bool Remove(const std::string &Path) override { return m_pStorage->RemoveBinaryFile(Path.c_str()); }
};

static int64_t FreeDiskSpace(const char *pPath)
{
#if defined(CONF_FAMILY_WINDOWS)
	ULARGE_INTEGER Free;
	return GetDiskFreeSpaceExA(pPath, &Free, nullptr, nullptr) ? (int64_t)Free.QuadPart : -1;
#else
	struct statvfs Stats;
	return statvfs(pPath, &Stats) == 0 ? (int64_t)Stats.f_bavail * (int64_t)Stats.f_frsize : -1;
#endif
}

#if !defined(CONF_FAMILY_WINDOWS)
static bool SetExecutableBit(const char *pPath)
{
	const int FileDescriptor = open(pPath, O_RDWR);
	if(FileDescriptor < 0)
	{
		log_error("updater", "Failed to open file descriptor to set executable bit of '%s'", pPath);
		return false;
	}
	struct stat FileStats;
	if(fstat(FileDescriptor, &FileStats) != 0)
	{
		log_error("updater", "Failed to determine file stats to set executable bit of '%s'", pPath);
		return false;
	}
	if(fchmod(FileDescriptor, FileStats.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) != 0)
	{
		log_error("updater", "Failed to set executable bit of '%s'", pPath);
		return false;
	}
	return true;
}
#endif

namespace
{
struct SChannelStore
{
	std::vector<KogUpdate::SPublicKey> m_vCi;
	std::vector<KogUpdate::SPublicKey> m_vBackup;
	std::vector<KogUpdate::SOwnerKey> m_vOwner;
	KogUpdate::SChannelKeys m_Keys = {};
	bool m_Placeholder = true;
};

template<size_t N>
void FillPublic(std::vector<KogUpdate::SPublicKey> *pOut, const char *const (&aHex)[N])
{
	for(const char *pHex : aHex)
	{
		KogUpdate::SPublicKey Key = {};
		if(str_hex_decode(Key.m_aKey, sizeof(Key.m_aKey), pHex) == 0)
			pOut->push_back(Key);
	}
}

template<size_t N>
void FillOwner(std::vector<KogUpdate::SOwnerKey> *pOut, const char *const (&aHex)[N])
{
	for(const char *pHex : aHex)
	{
		KogUpdate::SOwnerKey Key = {};
		Key.m_pApplication = KOG_OWNER_KEY_APPLICATION;
		if(str_hex_decode(Key.m_aKey, sizeof(Key.m_aKey), pHex) == 0)
			pOut->push_back(Key);
	}
}

const std::vector<SChannelStore> &ChannelStores()
{
	static const std::vector<SChannelStore> s_Stores = [] {
		std::vector<SChannelStore> vStores(3);
		FillPublic(&vStores[0].m_vCi, KOG_STABLE_CI_KEYS);
		FillPublic(&vStores[0].m_vBackup, KOG_STABLE_BACKUP_CI_KEYS);
		FillOwner(&vStores[0].m_vOwner, KOG_STABLE_OWNER_KEYS);
		FillPublic(&vStores[1].m_vCi, KOG_CANARY_CI_KEYS);
		FillPublic(&vStores[2].m_vCi, KOG_DEV_CI_KEYS);
		const char *const apChannels[] = {KogUpdate::CHANNEL_STABLE, KogUpdate::CHANNEL_CANARY, KogUpdate::CHANNEL_DEV};
		const bool aPlaceholder[] = {KOG_UPDATE_KEYS_PLACEHOLDER_STABLE != 0, KOG_UPDATE_KEYS_PLACEHOLDER_CANARY != 0, KOG_UPDATE_KEYS_PLACEHOLDER_DEV != 0};
		for(size_t i = 0; i < vStores.size(); i++)
		{
			SChannelStore &Store = vStores[i];
			Store.m_Keys = {apChannels[i], Store.m_vCi.data(), (int)Store.m_vCi.size(), Store.m_vOwner.data(), (int)Store.m_vOwner.size(), Store.m_vBackup.data(), (int)Store.m_vBackup.size()};
			Store.m_Placeholder = aPlaceholder[i] || Store.m_vCi.empty() || (i == 0 && Store.m_vOwner.empty());
		}
		return vStores;
	}();
	return s_Stores;
}
}

static const KogUpdate::SChannelKeys *KeysForChannel(const char *pChannel)
{
	for(const SChannelStore &Store : ChannelStores())
	{
		if(str_comp(Store.m_Keys.m_pChannel, pChannel) == 0)
			return Store.m_Placeholder ? nullptr : &Store.m_Keys;
	}
	return nullptr;
}

CUpdaterFetchTask::CUpdaterFetchTask(CUpdater *pUpdater, const char *pUrl, const KogUpdate::SFile &File, const char *pDestPath) :
	m_pUpdater(pUpdater)
{
	char aDestination[IO_MAX_PATH_LENGTH];
	str_format(aDestination, sizeof(aDestination), "update/%s", pDestPath);
	m_pHttpRequest = CreateHttpRequest(pUrl);
	m_pHttpRequest->WriteToFile(pUpdater->m_pStorage, aDestination, -2);
	m_pHttpRequest->ExpectSha256(File.m_Sha256);
	m_pHttpRequest->MaxResponseSize(File.m_Size);
	m_pHttpRequest->Timeout(CTimeout{10000, 0, 500, 10});
	m_pHttpRequest->SetProgressCallback(this);
}

void CUpdaterFetchTask::OnProgress()
{
	const CLockScope LockScope(m_pUpdater->m_Lock);
	m_pUpdater->m_Percent = m_pHttpRequest->Progress();
}

CUpdater::CUpdater()
{
	m_pClient = nullptr;
	m_pStorage = nullptr;
	m_pEngine = nullptr;
	m_pHttp = nullptr;
	m_State = CLEAN;
	m_Percent = 0;
	m_aStatus[0] = '\0';
	IStorage::FormatTmpPath(m_aClientExecTmp, sizeof(m_aClientExecTmp), CLIENT_EXEC);
}

void CUpdater::Init()
{
	m_pClient = Kernel()->RequestInterface<IClient>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pEngine = Kernel()->RequestInterface<IEngine>();
	m_pHttp = Kernel()->RequestInterface<IHttp>();
}

void CUpdater::SetCurrentState(EUpdaterState NewState)
{
	const CLockScope LockScope(m_Lock);
	m_State = NewState;
}

IUpdater::EUpdaterState CUpdater::GetCurrentState()
{
	const CLockScope LockScope(m_Lock);
	return m_State;
}

void CUpdater::GetCurrentFile(char *pBuf, int BufSize)
{
	const CLockScope LockScope(m_Lock);
	str_copy(pBuf, m_aStatus, BufSize);
}

int CUpdater::GetCurrentPercent()
{
	const CLockScope LockScope(m_Lock);
	return m_Percent;
}

const char *CUpdater::Channel()
{
	return KogUpdate::IsKnownChannel(g_Config.m_KogUpdateChannel) ? g_Config.m_KogUpdateChannel : KogUpdate::CHANNEL_STABLE;
}

void CUpdater::LoadState()
{
	char *pText = m_pStorage->ReadFileStr(STATE_FILE, IStorage::TYPE_SAVE);
	m_Persist = KogUpdate::SState::Parse(pText);
	free(pText);
}

void CUpdater::SaveState()
{
	IOHANDLE File = m_pStorage->OpenFile(STATE_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		log_error("updater", "Could not save %s", STATE_FILE);
		return;
	}
	const std::string Text = m_Persist.Serialize();
	io_write(File, Text.data(), Text.size());
	io_close(File);
}

void CUpdater::RecordCheck(const char *pResult)
{
	m_Persist.m_LastCheck = time_timestamp();
	m_Persist.m_LastResult = pResult;
	SaveState();
}

void CUpdater::LastCheckResult(char *pBuf, int BufSize)
{
	str_copy(pBuf, m_Persist.m_LastResult.c_str(), BufSize);
}

void CUpdater::EnsureInstallId()
{
	if(g_Config.m_KogInstallId[0] != '\0')
		return;
	unsigned char aRandom[16];
	secure_random_fill(aRandom, sizeof(aRandom));
	char aHex[33];
	static const char DIGITS[] = "0123456789abcdef";
	for(int i = 0; i < 16; i++)
	{
		aHex[2 * i] = DIGITS[aRandom[i] >> 4];
		aHex[2 * i + 1] = DIGITS[aRandom[i] & 15];
	}
	aHex[32] = '\0';
	str_copy(g_Config.m_KogInstallId, aHex);
}

void CUpdater::BackupConfig()
{
	for(const char *pFile : CONFIG_FILES)
	{
		char aBackup[IO_MAX_PATH_LENGTH];
		str_format(aBackup, sizeof(aBackup), "%s.kog-%s.bak", pFile, m_Persist.m_LastStartedVersion.c_str());
		void *pData;
		unsigned Length;
		if(m_pStorage->FileExists(aBackup, IStorage::TYPE_SAVE) || !m_pStorage->ReadFile(pFile, IStorage::TYPE_SAVE, &pData, &Length))
			continue;
		IOHANDLE File = m_pStorage->OpenFile(aBackup, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(File)
		{
			io_write(File, pData, Length);
			io_close(File);
		}
		free(pData);
	}
}

void CUpdater::OnLaunch()
{
	LoadState();
	CStorageFiles Files(m_pStorage);
	const bool MarkerPresent = m_pStorage->FileExists(MARKER_FILE, IStorage::TYPE_SAVE);
	if(KogUpdate::OnLaunch(&m_Persist, MarkerPresent, KOG_CLIENT_VERSION) == KogUpdate::ELaunchAction::ROLLBACK)
	{
		log_error("updater", "Version %s did not start %d times in a row, restoring the previous version", KOG_CLIENT_VERSION, KogUpdate::ROLLBACK_AFTER_FAILED_STARTS);
		KogUpdate::RollbackUpdate(Files, &m_Persist, Channel());
		m_RestartAfterRollback = true;
	}
	else if(KogUpdate::ConfigBackupNeeded(m_Persist, KOG_CLIENT_VERSION))
	{
		BackupConfig();
	}
	IOHANDLE Marker = m_pStorage->OpenFile(MARKER_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(Marker)
		io_close(Marker);
	SaveState();
}

void CUpdater::OnStartupComplete()
{
	if(m_StartupReported)
		return;
	m_StartupReported = true;
	m_pStorage->RemoveFile(MARKER_FILE, IStorage::TYPE_SAVE);
	KogUpdate::OnStartupComplete(&m_Persist, KOG_CLIENT_VERSION);
	SaveState();
}

void CUpdater::GetUrl(char *pBuf, int BufSize, int Attempt, const char *pFile)
{
	const char *pBase = KOG_UPDATE_FALLBACK_URL;
	if(Attempt == 0)
		pBase = str_startswith(g_Config.m_KogUpdateUrl, "https://") ? g_Config.m_KogUpdateUrl : KOG_UPDATE_BASE_URL;
	str_format(pBuf, BufSize, "%s/%s/%s", pBase, Channel(), pFile);
}

void CUpdater::StartFetch(SFetch &Fetch)
{
	static const char *const FILES[] = {"update.json", "update.json.sig", "update.json.sigb"};
	const KogUpdate::SChannelKeys *pKeys = KeysForChannel(Channel());
	Fetch.m_NumRequests = pKeys != nullptr && pKeys->m_NumOwnerKeys > 0 ? 3 : 2;
	for(int i = 0; i < Fetch.m_NumRequests; i++)
	{
		char aUrl[512];
		GetUrl(aUrl, sizeof(aUrl), Fetch.m_Attempt, FILES[i]);
		Fetch.m_apRequests[i] = HttpGet(aUrl);
		Fetch.m_apRequests[i]->WriteToMemory();
		Fetch.m_apRequests[i]->MaxResponseSize(i == 0 ? (int64_t)KogUpdate::MAX_MANIFEST_SIZE : 4096);
		Fetch.m_apRequests[i]->Timeout(CTimeout{10000, 0, 500, 10});
		m_pHttp->Run(Fetch.m_apRequests[i]);
	}
}

bool CUpdater::PollFetch(SFetch &Fetch)
{
	for(int i = 0; i < Fetch.m_NumRequests; i++)
	{
		if(!Fetch.m_apRequests[i]->Done())
			return false;
	}
	const bool Required = Fetch.m_apRequests[0]->State() == EHttpState::DONE && Fetch.m_apRequests[1]->State() == EHttpState::DONE;
	if(!Required && Fetch.m_Attempt == 0)
	{
		Fetch.m_Attempt = 1;
		StartFetch(Fetch);
		return false;
	}
	Fetch.m_Ok = Required;
	return true;
}

bool CUpdater::Verify(const SFetch &Fetch, bool Quiet, SChecked *pOut)
{
	const char *pChannel = Channel();
	const KogUpdate::SChannelKeys *pKeys = KeysForChannel(pChannel);
	if(pKeys == nullptr)
	{
		log_info("updater", "This build has no update keys for the %s channel, updates are disabled", pChannel);
		RecordCheck("not configured");
		return false;
	}
	unsigned char *pManifest;
	size_t ManifestSize;
	Fetch.m_apRequests[0]->Result(&pManifest, &ManifestSize);
	unsigned char *pCi;
	size_t CiSize;
	Fetch.m_apRequests[1]->Result(&pCi, &CiSize);
	unsigned char *pOwner = nullptr;
	size_t OwnerSize = 0;
	if(Fetch.m_NumRequests == 3 && Fetch.m_apRequests[2]->State() == EHttpState::DONE)
		Fetch.m_apRequests[2]->Result(&pOwner, &OwnerSize);

	const KogUpdate::ESignatureResult Signatures = KogUpdate::VerifyManifestSignatures(pManifest, ManifestSize, reinterpret_cast<const char *>(pCi), CiSize, reinterpret_cast<const char *>(pOwner), OwnerSize, pKeys);
	if(Signatures != KogUpdate::ESignatureResult::OK)
	{
		log_error("updater", "The update manifest is not signed as required (%d)", (int)Signatures);
		RecordCheck(Signatures == KogUpdate::ESignatureResult::MISSING_OWNER ? "not fully signed" : "invalid signature");
		return false;
	}
	char aError[256];
	if(!KogUpdate::ParseManifest(reinterpret_cast<const char *>(pManifest), ManifestSize, PLAT_NAME, &pOut->m_Manifest, aError, sizeof(aError)))
	{
		log_error("updater", "Invalid update manifest: %s", aError);
		RecordCheck("invalid manifest");
		return false;
	}

	EnsureInstallId();
	KogUpdate::SDecideInput Input;
	Input.m_pChannel = pChannel;
	Input.m_pCurrentVersion = KOG_CLIENT_VERSION;
	Input.m_LastSequence = m_Persist.Sequence(pChannel);
	Input.m_Now = time_timestamp();
	Input.m_Bucket = KogUpdate::Bucket(g_Config.m_KogInstallId);
	Input.m_SwitchConfirmed = m_SwitchConfirmed;
	Input.m_pBadVersion = m_Persist.IsBad(pChannel, pOut->m_Manifest.m_Version.c_str()) ? pOut->m_Manifest.m_Version.c_str() : "";
	pOut->m_Verdict = KogUpdate::Decide(pOut->m_Manifest, Input);

	const char *pProblem = nullptr;
	switch(pOut->m_Verdict)
	{
	case KogUpdate::EVerdict::EXPIRED:
		pProblem = "The update manifest has expired, or the system clock is wrong";
		break;
	case KogUpdate::EVerdict::NOT_YET_VALID:
		pProblem = "The update manifest is dated in the future, check the system clock";
		break;
	case KogUpdate::EVerdict::ROLLBACK:
		pProblem = "The update manifest is older than one already seen, refusing it";
		break;
	case KogUpdate::EVerdict::WRONG_CHANNEL:
		pProblem = "The update manifest is for another channel";
		break;
	case KogUpdate::EVerdict::BAD_VERSION:
		pProblem = "The versions could not be compared";
		break;
	default:
		break;
	}
	if(pProblem)
	{
		log_error("updater", "%s", pProblem);
		RecordCheck("invalid manifest");
		return false;
	}
	if(pOut->m_Manifest.m_Sequence > m_Persist.Sequence(pChannel))
		m_Persist.SetSequence(pChannel, pOut->m_Manifest.m_Sequence);
	(void)Quiet;
	return true;
}

void CUpdater::SetInfo(const SChecked &Checked)
{
	m_aLatestVersion[0] = '\0';
	m_HasInfo = false;
	const bool Apply = Checked.m_Verdict == KogUpdate::EVerdict::APPLY;
	if(!Apply && Checked.m_Verdict != KogUpdate::EVerdict::NEEDS_CONFIRMATION)
	{
		RecordCheck("up to date");
		return;
	}
	const KogUpdate::SManifest &Manifest = Checked.m_Manifest;
	m_Info = SUpdateInfo();
	str_copy(m_Info.m_aVersion, Manifest.m_Version.c_str());
	str_copy(m_Info.m_aChannel, Manifest.m_Channel.c_str());
	m_Info.m_Size = Manifest.TotalSize();
	str_copy(m_Info.m_aNotesUrl, Manifest.m_NotesUrl.c_str());
	for(const std::string &Change : Manifest.m_vChanges)
		str_copy(m_Info.m_aChanges[m_Info.m_NumChanges++], Change.c_str());
	m_Info.m_Required = Apply && KogUpdate::UpdateRequired(Manifest, KOG_CLIENT_VERSION);
	m_Info.m_NeedsConfirmation = !Apply;
	m_HasInfo = true;
	if(Apply)
		str_copy(m_aLatestVersion, Manifest.m_Version.c_str());
	RecordCheck(Apply ? "update available" : "other channel");
}

bool CUpdater::GetUpdateInfo(SUpdateInfo *pInfo)
{
	if(!m_HasInfo)
		return false;
	*pInfo = m_Info;
	return true;
}

void CUpdater::CheckForUpdate()
{
	if(m_Check == ECheck::RUNNING || GetCurrentState() != CLEAN)
		return;
	m_Check = ECheck::RUNNING;
	m_CheckFetch = SFetch();
	StartFetch(m_CheckFetch);
}

void CUpdater::UpdateCheck()
{
	if(m_Check != ECheck::RUNNING || !PollFetch(m_CheckFetch))
		return;
	SChecked Checked;
	if(!m_CheckFetch.m_Ok)
	{
		log_info("updater", "No connection to the update server");
		m_aLatestVersion[0] = '\0';
		m_HasInfo = false;
		RecordCheck("no connection");
	}
	else if(Verify(m_CheckFetch, true, &Checked))
	{
		SetInfo(Checked);
	}
	else
	{
		m_aLatestVersion[0] = '\0';
		m_HasInfo = false;
	}
	for(auto &pRequest : m_CheckFetch.m_apRequests)
		pRequest = nullptr;
	m_Check = ECheck::DONE;
}

void CUpdater::BuildJobs(const KogUpdate::SManifest &Manifest)
{
	m_vJobs.clear();
	m_CurrentJob = 0;
	m_ClientUpdate = Manifest.m_HasClient;
	m_ClientFetched = false;
	CStorageFiles Files(m_pStorage);
	for(const KogUpdate::SFile &File : Manifest.m_Files)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		m_pStorage->GetBinaryPath(File.m_Path.c_str(), aPath, sizeof(aPath));
		SHA256_DIGEST Local;
		if(!(m_pStorage->CalculateHashes(aPath, IStorage::TYPE_ABSOLUTE, &Local) && Local == File.m_Sha256))
			m_vJobs.push_back({File, true});
	}
	for(const std::string &Path : Manifest.m_Remove)
	{
		KogUpdate::SFile File;
		File.m_Path = Path;
		m_vJobs.push_back({File, false});
	}
	if(Manifest.m_HasClient)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		m_pStorage->GetBinaryPath(PLAT_CLIENT_EXEC, aPath, sizeof(aPath));
		SHA256_DIGEST Local;
		if(m_pStorage->CalculateHashes(aPath, IStorage::TYPE_ABSOLUTE, &Local) && Local == Manifest.m_Client.m_Sha256)
			m_ClientUpdate = false;
	}
}

void CUpdater::RemoveDownloads()
{
	char aPath[IO_MAX_PATH_LENGTH];
	for(const SJob &Job : m_vJobs)
	{
		if(Job.m_Download)
		{
			str_format(aPath, sizeof(aPath), "update/%s", Job.m_File.m_Path.c_str());
			m_pStorage->RemoveBinaryFile(aPath);
		}
	}
	str_format(aPath, sizeof(aPath), "update/%s", m_aClientExecTmp);
	m_pStorage->RemoveBinaryFile(aPath);
}

void CUpdater::Fail()
{
	m_pCurrentTask = nullptr;
	RemoveDownloads();
	SetCurrentState(FAIL);
}

void CUpdater::FetchCurrentJob()
{
	const CLockScope LockScope(m_Lock);
	m_pCurrentTask = std::make_shared<CUpdaterFetchTask>(this, m_CurrentFile.m_vUrls[m_UrlIndex].c_str(), m_CurrentFile, m_CurrentDest.c_str());
	str_copy(m_aStatus, m_pCurrentTask->HttpRequest()->Dest());
	m_pHttp->Run(m_pCurrentTask->HttpRequest());
}

void CUpdater::Update()
{
	UpdateCheck();
	if(m_RestartAfterRollback && GetCurrentState() == CLEAN)
	{
		m_RestartAfterRollback = false;
		m_pClient->Restart();
	}
	switch(GetCurrentState())
	{
	case IUpdater::GETTING_MANIFEST:
		ProcessManifestTasks();
		break;
	case IUpdater::GOT_MANIFEST:
		PerformUpdate();
		break;
	case IUpdater::DOWNLOADING:
		RunningUpdate();
		break;
	case IUpdater::MOVE_FILES:
		CommitUpdate();
		break;
	default:
		return;
	}
}

void CUpdater::InitiateUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(State != CLEAN && State != FAIL)
		return;
	m_vJobs.clear();
	SetCurrentState(GETTING_MANIFEST);
	m_DownloadFetch = SFetch();
	StartFetch(m_DownloadFetch);
}

void CUpdater::ProcessManifestTasks()
{
	if(!PollFetch(m_DownloadFetch))
		return;
	SetCurrentState(GOT_MANIFEST);
}

void CUpdater::PerformUpdate()
{
	SetCurrentState(PARSING_UPDATE);
	SChecked Checked;
	const bool Valid = m_DownloadFetch.m_Ok && Verify(m_DownloadFetch, false, &Checked);
	for(auto &pRequest : m_DownloadFetch.m_apRequests)
		pRequest = nullptr;
	if(!m_DownloadFetch.m_Ok)
		log_error("updater", "Could not download the update manifest");
	if(!Valid)
	{
		SetCurrentState(FAIL);
		return;
	}
	if(Checked.m_Verdict != KogUpdate::EVerdict::APPLY)
	{
		log_info("updater", "Nothing to install");
		SetCurrentState(CLEAN);
		return;
	}
	m_Manifest = Checked.m_Manifest;
	const int64_t Required = m_Manifest.TotalSize() + m_Manifest.TotalSize() / 10;
	char aBinaryPath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetBinaryPath(PLAT_CLIENT_EXEC, aBinaryPath, sizeof(aBinaryPath));
	const int64_t Free = FreeDiskSpace(aBinaryPath);
	if(Free >= 0 && Free < Required)
	{
		log_error("updater", "Not enough free disk space for the update");
		SetCurrentState(FAIL);
		return;
	}
	BuildJobs(m_Manifest);
	SetCurrentState(DOWNLOADING);
}

void CUpdater::RunningUpdate()
{
	if(m_pCurrentTask)
	{
		if(!m_pCurrentTask->HttpRequest()->Done())
			return;
		if(m_pCurrentTask->HttpRequest()->State() != EHttpState::DONE)
		{
			if(m_UrlIndex + 1 < m_CurrentFile.m_vUrls.size())
			{
				m_UrlIndex++;
				FetchCurrentJob();
				return;
			}
			log_error("updater", "Download failed: %s", m_pCurrentTask->HttpRequest()->Dest());
			Fail();
			return;
		}
	}

	while(m_CurrentJob < m_vJobs.size() && !m_vJobs[m_CurrentJob].m_Download)
		m_CurrentJob++;
	if(m_CurrentJob < m_vJobs.size())
	{
		m_CurrentFile = m_vJobs[m_CurrentJob].m_File;
		m_CurrentDest = m_CurrentFile.m_Path;
		m_CurrentJob++;
		m_UrlIndex = 0;
		FetchCurrentJob();
		return;
	}
	if(m_ClientUpdate && !m_ClientFetched)
	{
		m_CurrentFile = m_Manifest.m_Client;
		m_CurrentDest = m_aClientExecTmp;
		m_ClientFetched = true;
		m_UrlIndex = 0;
		FetchCurrentJob();
		return;
	}
	SetCurrentState(MOVE_FILES);
}

void CUpdater::CommitUpdate()
{
	std::vector<KogUpdate::SChange> vChanges;
	for(const SJob &Job : m_vJobs)
	{
		KogUpdate::SChange Change;
		Change.m_Path = Job.m_File.m_Path;
		Change.m_Remove = !Job.m_Download;
		if(Job.m_Download)
			Change.m_Source = "update/" + Job.m_File.m_Path;
		vChanges.push_back(Change);
	}
	if(m_ClientUpdate)
	{
		KogUpdate::SChange Client;
		Client.m_Path = PLAT_CLIENT_EXEC;
		Client.m_Source = std::string("update/") + m_aClientExecTmp;
		vChanges.push_back(Client);
	}

	CStorageFiles Files(m_pStorage);
	if(!KogUpdate::ApplyUpdate(Files, &m_Persist, vChanges, m_Manifest.m_Version.c_str()))
	{
		log_error("updater", "Could not install the update, the previous files are in place");
		Fail();
		return;
	}
#if !defined(CONF_FAMILY_WINDOWS)
	if(m_ClientUpdate)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		m_pStorage->GetBinaryPath(PLAT_CLIENT_EXEC, aPath, sizeof(aPath));
		SetExecutableBit(aPath);
	}
#endif
	m_SwitchConfirmed = false;
	m_aLatestVersion[0] = '\0';
	m_HasInfo = false;
	RecordCheck("installed");

	if(m_pClient->State() == IClient::STATE_ONLINE || m_pClient->EditorHasUnsavedData())
		SetCurrentState(NEED_RESTART);
	else
		m_pClient->Restart();
}
