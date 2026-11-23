#ifndef ENGINE_UPDATER_H
#define ENGINE_UPDATER_H

#include "kernel.h"

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
	// Empty unless the signed manifest announces a newer version
	virtual const char *LatestVersion() = 0;

	virtual EUpdaterState GetCurrentState() = 0;
	virtual void GetCurrentFile(char *pBuf, int BufSize) = 0;
	virtual int GetCurrentPercent() = 0;
};

#endif
