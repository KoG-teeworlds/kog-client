#include "updater.h"

#include "kog_update_pubkey.h"

#include <base/fs.h>
#include <base/io.h>
#include <base/log.h>
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

#if !defined(CONF_FAMILY_WINDOWS)
#include <fcntl.h>
#include <sys/stat.h>
#endif

static constexpr const char *UPDATE_CHANNEL = "stable";
static constexpr const char *STATE_FILE = "kog_update_state.txt";

class CUpdaterFetchTask : public IHttpRequest::IProgressCallback
{
	CUpdater *m_pUpdater;
	std::shared_ptr<IHttpRequest> m_pHttpRequest;

protected:
	void OnProgress() override;
	void OnCompletion(EHttpState) override {}

public:
	CUpdaterFetchTask(CUpdater *pUpdater, const KogUpdate::SFile &File, const char *pDestPath);
	std::shared_ptr<IHttpRequest> HttpRequest() { return m_pHttpRequest; }
};

static void GetManifestUrl(char *pBuf, int BufSize, bool Signature)
{
	if(str_startswith(g_Config.m_KogUpdateUrl, "https://"))
		str_format(pBuf, BufSize, Signature ? "%s.sig" : "%s", g_Config.m_KogUpdateUrl);
	else
		str_copy(pBuf, Signature ? KOG_UPDATE_SIGNATURE_URL : KOG_UPDATE_URL, BufSize);
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

CUpdaterFetchTask::CUpdaterFetchTask(CUpdater *pUpdater, const KogUpdate::SFile &File, const char *pDestPath) :
	m_pUpdater(pUpdater)
{
	char aDestination[IO_MAX_PATH_LENGTH];
	str_format(aDestination, sizeof(aDestination), "update/%s", pDestPath);
	m_pHttpRequest = CreateHttpRequest(File.m_Url.c_str());
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

void CUpdater::StartManifestFetch(std::shared_ptr<IHttpRequest> &pManifest, std::shared_ptr<IHttpRequest> &pSignature)
{
	char aUrl[512];
	GetManifestUrl(aUrl, sizeof(aUrl), false);
	pManifest = HttpGet(aUrl);
	pManifest->WriteToMemory();
	pManifest->MaxResponseSize(KogUpdate::MAX_MANIFEST_SIZE);
	pManifest->Timeout(CTimeout{10000, 0, 500, 10});
	GetManifestUrl(aUrl, sizeof(aUrl), true);
	pSignature = HttpGet(aUrl);
	pSignature->WriteToMemory();
	pSignature->MaxResponseSize(1024);
	pSignature->Timeout(CTimeout{10000, 0, 500, 10});
	m_pHttp->Run(pManifest);
	m_pHttp->Run(pSignature);
}

int64_t CUpdater::LoadLastSequence()
{
	char *pText = m_pStorage->ReadFileStr(STATE_FILE, IStorage::TYPE_SAVE);
	if(!pText)
		return 0;
	const char *pValue = str_startswith(pText, "sequence=");
	const int64_t Sequence = pValue ? strtoll(pValue, nullptr, 10) : 0;
	free(pText);
	return Sequence;
}

void CUpdater::SaveLastSequence(int64_t Sequence)
{
	IOHANDLE File = m_pStorage->OpenFile(STATE_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		log_error("updater", "Could not save %s", STATE_FILE);
		return;
	}
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "sequence=%lld\n", (long long)Sequence);
	io_write(File, aBuf, str_length(aBuf));
	io_close(File);
}

bool CUpdater::VerifyManifest(const IHttpRequest &Manifest, const IHttpRequest &Signature, KogUpdate::SManifest *pOut, bool *pNewer)
{
	if(KOG_UPDATE_PUBKEY_PLACEHOLDER)
	{
		log_error("updater", "This build has no update signing key, updates are disabled");
		return false;
	}
	if(Manifest.State() != EHttpState::DONE || Signature.State() != EHttpState::DONE)
	{
		log_error("updater", "Could not download the update manifest");
		return false;
	}
	unsigned char *pManifest;
	size_t ManifestSize;
	Manifest.Result(&pManifest, &ManifestSize);
	unsigned char *pSignatureText;
	size_t SignatureSize;
	Signature.Result(&pSignatureText, &SignatureSize);

	unsigned char aSignature[64];
	unsigned char aKeyId[4];
	if(!KogUpdate::ParseSignatureFile(reinterpret_cast<const char *>(pSignatureText), SignatureSize, aSignature, aKeyId))
	{
		log_error("updater", "The signature file is malformed");
		return false;
	}
	if(!KogUpdate::VerifySignature(pManifest, ManifestSize, aSignature, aKeyId, KOG_UPDATE_PUBKEYS, (int)std::size(KOG_UPDATE_PUBKEYS)))
	{
		log_error("updater", "The signature of the update manifest is invalid");
		return false;
	}
	char aError[256];
	if(!KogUpdate::ParseManifest(reinterpret_cast<const char *>(pManifest), ManifestSize, PLAT_NAME, pOut, aError, sizeof(aError)))
	{
		log_error("updater", "Invalid update manifest: %s", aError);
		return false;
	}

	const int64_t LastSequence = LoadLastSequence();
	switch(KogUpdate::Decide(*pOut, UPDATE_CHANNEL, KOG_CLIENT_VERSION, LastSequence, time_timestamp()))
	{
	case KogUpdate::EVerdict::APPLY:
		*pNewer = true;
		break;
	case KogUpdate::EVerdict::UP_TO_DATE:
		*pNewer = false;
		break;
	case KogUpdate::EVerdict::EXPIRED:
		log_error("updater", "The update manifest has expired, or the system clock is wrong");
		return false;
	case KogUpdate::EVerdict::NOT_YET_VALID:
		log_error("updater", "The update manifest is dated in the future, check the system clock");
		return false;
	case KogUpdate::EVerdict::ROLLBACK:
		log_error("updater", "The update manifest is older than one already seen, refusing it");
		return false;
	case KogUpdate::EVerdict::WRONG_CHANNEL:
		log_error("updater", "The update manifest is for another channel");
		return false;
	case KogUpdate::EVerdict::BAD_VERSION:
		log_error("updater", "The versions could not be compared");
		return false;
	}
	if(pOut->m_Sequence > LastSequence)
		SaveLastSequence(pOut->m_Sequence);
	return true;
}

void CUpdater::CheckForUpdate()
{
	if(m_Check == ECheck::RUNNING || GetCurrentState() != CLEAN)
		return;
	m_Check = ECheck::RUNNING;
	m_aLatestVersion[0] = '\0';
	StartManifestFetch(m_pCheckManifest, m_pCheckSignature);
}

void CUpdater::UpdateCheck()
{
	if(m_Check != ECheck::RUNNING || !m_pCheckManifest->Done() || !m_pCheckSignature->Done())
		return;
	KogUpdate::SManifest Manifest;
	bool Newer = false;
	if(VerifyManifest(*m_pCheckManifest, *m_pCheckSignature, &Manifest, &Newer) && Newer)
		str_copy(m_aLatestVersion, Manifest.m_Version.c_str());
	m_pCheckManifest = nullptr;
	m_pCheckSignature = nullptr;
	m_Check = ECheck::DONE;
}

bool CUpdater::IsCurrent(const char *pPath, const SHA256_DIGEST &Sha256)
{
	char aPath[IO_MAX_PATH_LENGTH];
	m_pStorage->GetBinaryPath(pPath, aPath, sizeof(aPath));
	SHA256_DIGEST Local;
	return m_pStorage->CalculateHashes(aPath, IStorage::TYPE_ABSOLUTE, &Local) && Local == Sha256;
}

void CUpdater::BuildJobs(const KogUpdate::SManifest &Manifest)
{
	m_vJobs.clear();
	m_CurrentJob = 0;
	m_ClientUpdate = false;
	m_ClientFetched = false;
	for(const KogUpdate::SFile &File : Manifest.m_Files)
	{
		if(!IsCurrent(File.m_Path.c_str(), File.m_Sha256))
			m_vJobs.push_back({File, true});
	}
	for(const std::string &Path : Manifest.m_Remove)
	{
		KogUpdate::SFile File;
		File.m_Path = Path;
		m_vJobs.push_back({File, false});
	}
	if(Manifest.m_HasClient && !IsCurrent(PLAT_CLIENT_EXEC, Manifest.m_Client.m_Sha256))
	{
		m_ClientUpdate = true;
		m_ClientFile = Manifest.m_Client;
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

void CUpdater::FetchFile(const KogUpdate::SFile &File, const char *pDestPath)
{
	const CLockScope LockScope(m_Lock);
	m_pCurrentTask = std::make_shared<CUpdaterFetchTask>(this, File, pDestPath);
	str_copy(m_aStatus, m_pCurrentTask->HttpRequest()->Dest());
	m_pHttp->Run(m_pCurrentTask->HttpRequest());
}

bool CUpdater::MoveFile(const char *pFile)
{
	char aBackup[IO_MAX_PATH_LENGTH];
	char aUpdate[IO_MAX_PATH_LENGTH];
	str_format(aBackup, sizeof(aBackup), "%s.kogbak", pFile);
	str_format(aUpdate, sizeof(aUpdate), "update/%s", pFile);
	m_pStorage->RemoveBinaryFile(aBackup);
	m_pStorage->RenameBinaryFile(pFile, aBackup);
	return m_pStorage->RenameBinaryFile(aUpdate, pFile);
}

void CUpdater::RestoreFile(const char *pFile)
{
	char aBackup[IO_MAX_PATH_LENGTH];
	str_format(aBackup, sizeof(aBackup), "%s.kogbak", pFile);
	m_pStorage->RemoveBinaryFile(pFile);
	m_pStorage->RenameBinaryFile(aBackup, pFile);
}

void CUpdater::Update()
{
	UpdateCheck();
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

bool CUpdater::ReplaceClient()
{
	log_debug("updater", "Replacing " PLAT_CLIENT_EXEC);
	char aPath[IO_MAX_PATH_LENGTH];

	// Replace running executable by renaming twice...
	m_pStorage->RemoveBinaryFile(CLIENT_EXEC ".old");
	if(!m_pStorage->RenameBinaryFile(PLAT_CLIENT_EXEC, CLIENT_EXEC ".old"))
		return false;
	str_format(aPath, sizeof(aPath), "update/%s", m_aClientExecTmp);
	if(!m_pStorage->RenameBinaryFile(aPath, PLAT_CLIENT_EXEC))
	{
		m_pStorage->RenameBinaryFile(CLIENT_EXEC ".old", PLAT_CLIENT_EXEC);
		return false;
	}
	m_pStorage->RemoveBinaryFile(CLIENT_EXEC ".old");
#if !defined(CONF_FAMILY_WINDOWS)
	m_pStorage->GetBinaryPath(PLAT_CLIENT_EXEC, aPath, sizeof(aPath));
	return SetExecutableBit(aPath);
#else
	return true;
#endif
}

void CUpdater::InitiateUpdate()
{
	const EUpdaterState State = GetCurrentState();
	if(State != CLEAN && State != FAIL)
		return;
	m_vJobs.clear();
	SetCurrentState(GETTING_MANIFEST);
	StartManifestFetch(m_pManifestTask, m_pSignatureTask);
}

void CUpdater::ProcessManifestTasks()
{
	if(!m_pManifestTask->Done() || !m_pSignatureTask->Done())
		return;
	SetCurrentState(GOT_MANIFEST);
}

void CUpdater::PerformUpdate()
{
	SetCurrentState(PARSING_UPDATE);
	KogUpdate::SManifest Manifest;
	bool Newer = false;
	const bool Valid = VerifyManifest(*m_pManifestTask, *m_pSignatureTask, &Manifest, &Newer);
	m_pManifestTask = nullptr;
	m_pSignatureTask = nullptr;
	if(!Valid)
	{
		SetCurrentState(FAIL);
		return;
	}
	if(!Newer)
	{
		log_info("updater", "Already up to date");
		SetCurrentState(CLEAN);
		return;
	}
	BuildJobs(Manifest);
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
			log_error("updater", "Download failed: %s", m_pCurrentTask->HttpRequest()->Dest());
			Fail();
			return;
		}
	}

	while(m_CurrentJob < m_vJobs.size() && !m_vJobs[m_CurrentJob].m_Download)
		m_CurrentJob++;
	if(m_CurrentJob < m_vJobs.size())
	{
		const SJob &Job = m_vJobs[m_CurrentJob++];
		FetchFile(Job.m_File, Job.m_File.m_Path.c_str());
		return;
	}
	if(m_ClientUpdate && !m_ClientFetched)
	{
		FetchFile(m_ClientFile, m_aClientExecTmp);
		m_ClientFetched = true;
		return;
	}
	SetCurrentState(MOVE_FILES);
}

void CUpdater::CommitUpdate()
{
	std::vector<const char *> vMoved;
	bool Success = true;
	for(const SJob &Job : m_vJobs)
	{
		if(!Job.m_Download)
			continue;
		vMoved.push_back(Job.m_File.m_Path.c_str());
		if(!MoveFile(Job.m_File.m_Path.c_str()))
		{
			Success = false;
			break;
		}
	}
	if(Success && m_ClientUpdate)
		Success = ReplaceClient();

	if(!Success)
	{
		log_error("updater", "Could not install the update, restoring the previous files");
		for(auto It = vMoved.rbegin(); It != vMoved.rend(); ++It)
			RestoreFile(*It);
		Fail();
		return;
	}

	char aBackup[IO_MAX_PATH_LENGTH];
	for(const char *pPath : vMoved)
	{
		str_format(aBackup, sizeof(aBackup), "%s.kogbak", pPath);
		m_pStorage->RemoveBinaryFile(aBackup);
	}
	for(const SJob &Job : m_vJobs)
	{
		if(!Job.m_Download)
			m_pStorage->RemoveBinaryFile(Job.m_File.m_Path.c_str());
	}

	if(m_pClient->State() == IClient::STATE_ONLINE || m_pClient->EditorHasUnsavedData())
		SetCurrentState(NEED_RESTART);
	else
		m_pClient->Restart();
}
