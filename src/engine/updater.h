#ifndef ENGINE_UPDATER_H
#define ENGINE_UPDATER_H

#include "kernel.h"

#include <cstdint>

class IUpdater : public IInterface
{
	MACRO_INTERFACE("updater")
public:
	enum EUpdaterState
	{
		CLEAN,
		GETTING_MANIFEST,
		GOT_MANIFEST,
		PARSING_UPDATE,
		DOWNLOADING,
		MOVE_FILES,
		NEED_RESTART,
		FAIL,
	};

	virtual void Update() = 0;
	virtual void InitiateUpdate() = 0;
	virtual void CheckForUpdate() = 0;
	virtual bool CheckFinished() = 0;
	// Empty unless the signed manifest announces a newer version that this install may take
	virtual const char *LatestVersion() = 0;

	struct SUpdateInfo
	{
		char m_aVersion[32];
		char m_aChannel[16];
		int64_t m_Size;
		char m_aNotesUrl[400];
		char m_aChanges[3][128];
		int m_NumChanges;
		bool m_Required;
		bool m_NeedsConfirmation;
	};
	virtual bool GetUpdateInfo(SUpdateInfo *pInfo) = 0;
	virtual void ConfirmChannelSwitch() = 0;
	virtual const char *Channel() = 0;
	virtual int64_t LastCheckTime() = 0;
	virtual void LastCheckResult(char *pBuf, int BufSize) = 0;
	virtual void OnStartupComplete() = 0;

	virtual EUpdaterState GetCurrentState() = 0;
	virtual void GetCurrentFile(char *pBuf, int BufSize) = 0;
	virtual int GetCurrentPercent() = 0;
};

#endif
