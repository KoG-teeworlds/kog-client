#ifndef ENGINE_CLIENT_UPDATER_H
#define ENGINE_CLIENT_UPDATER_H

#include <base/detect.h>
#include <base/lock.h>

#include <engine/shared/kog_failsafe.h>
#include <engine/shared/kog_update.h>
#include <engine/updater.h>
#include <game/kog_version.h>

#include <memory>
#include <string>
#include <vector>

#define CLIENT_EXEC KOG_CLIENT_EXECUTABLE
#define SERVER_EXEC "DDNet-Server"

#if defined(CONF_FAMILY_WINDOWS)
#define PLAT_EXT ".exe"
#define PLAT_NAME CONF_PLATFORM_STRING
#elif defined(CONF_FAMILY_UNIX)
#define PLAT_EXT ""
#if defined(CONF_ARCH_IA32)
#define PLAT_NAME CONF_PLATFORM_STRING "-x86"
#elif defined(CONF_ARCH_AMD64)
#define PLAT_NAME CONF_PLATFORM_STRING "-x86_64"
#else
#define PLAT_NAME CONF_PLATFORM_STRING "-unsupported"
#endif
#else
#if defined(AUTOUPDATE)
#error Compiling with autoupdater on an unsupported platform
#endif
#define PLAT_EXT ""
#define PLAT_NAME "unsupported-unsupported"
#endif

#define PLAT_CLIENT_DOWN CLIENT_EXEC "-" PLAT_NAME PLAT_EXT
#define PLAT_SERVER_DOWN SERVER_EXEC "-" PLAT_NAME PLAT_EXT

#define PLAT_CLIENT_EXEC CLIENT_EXEC PLAT_EXT
#define PLAT_SERVER_EXEC SERVER_EXEC PLAT_EXT

class CUpdaterFetchTask;
class IHttpRequest;

class CUpdater : public IUpdater
{
	friend class CUpdaterFetchTask;

	class IClient *m_pClient;
	class IStorage *m_pStorage;
	class IEngine *m_pEngine;
	class IHttp *m_pHttp;

	CLock m_Lock;

	EUpdaterState m_State GUARDED_BY(m_Lock);
	char m_aStatus[256] GUARDED_BY(m_Lock);
	int m_Percent GUARDED_BY(m_Lock);
	char m_aClientExecTmp[64];

	KogUpdate::SState m_Persist;
	bool m_StartupReported = false;
	bool m_RestartAfterRollback = false;

	struct SFetch
	{
		std::shared_ptr<IHttpRequest> m_apRequests[3];
		int m_NumRequests = 0;
		int m_Attempt = 0;
		bool m_Ok = false;
	};
	SFetch m_CheckFetch;
	SFetch m_DownloadFetch;

	struct SJob
	{
		KogUpdate::SFile m_File;
		bool m_Download;
	};
	std::vector<SJob> m_vJobs;
	size_t m_CurrentJob = 0;
	size_t m_UrlIndex = 0;
	std::shared_ptr<CUpdaterFetchTask> m_pCurrentTask;
	KogUpdate::SFile m_CurrentFile;
	std::string m_CurrentDest;
	KogUpdate::SManifest m_Manifest;
	bool m_ClientUpdate = false;
	bool m_ClientFetched = false;
	bool m_SwitchConfirmed = false;

	enum class ECheck
	{
		IDLE,
		RUNNING,
		DONE,
	};
	ECheck m_Check = ECheck::IDLE;
	SUpdateInfo m_Info = {};
	bool m_HasInfo = false;
	char m_aLatestVersion[32] = "";

	struct SChecked
	{
		KogUpdate::SManifest m_Manifest;
		KogUpdate::EVerdict m_Verdict = KogUpdate::EVerdict::UP_TO_DATE;
	};

	void LoadState();
	void SaveState();
	void RecordCheck(const char *pResult);
	void BackupConfig();
	void EnsureInstallId();

	void GetUrl(char *pBuf, int BufSize, int Attempt, const char *pFile);
	void StartFetch(SFetch &Fetch);
	bool PollFetch(SFetch &Fetch);
	bool Verify(const SFetch &Fetch, bool Quiet, SChecked *pOut);
	void SetInfo(const SChecked &Checked);

	void BuildJobs(const KogUpdate::SManifest &Manifest);
	void RemoveDownloads();
	void Fail();
	void FetchCurrentJob() REQUIRES(!m_Lock);

	void UpdateCheck();
	void ProcessManifestTasks() REQUIRES(!m_Lock);
	void PerformUpdate() REQUIRES(!m_Lock);
	void RunningUpdate() REQUIRES(!m_Lock);
	void CommitUpdate() REQUIRES(!m_Lock);

	void SetCurrentState(EUpdaterState NewState) REQUIRES(!m_Lock);

public:
	CUpdater();

	EUpdaterState GetCurrentState() override REQUIRES(!m_Lock);
	void GetCurrentFile(char *pBuf, int BufSize) override REQUIRES(!m_Lock);
	int GetCurrentPercent() override REQUIRES(!m_Lock);

	void InitiateUpdate() REQUIRES(!m_Lock) override;
	void CheckForUpdate() override;
	bool CheckFinished() override { return m_Check == ECheck::DONE; }
	const char *LatestVersion() override { return m_aLatestVersion; }
	bool GetUpdateInfo(SUpdateInfo *pInfo) override;
	void ConfirmChannelSwitch() override { m_SwitchConfirmed = true; }
	const char *Channel() override;
	int64_t LastCheckTime() override { return m_Persist.m_LastCheck; }
	void LastCheckResult(char *pBuf, int BufSize) override;
	void OnStartupComplete() override;
	void Init();
	void OnLaunch();
	void Update() REQUIRES(!m_Lock) override;
};

#endif
