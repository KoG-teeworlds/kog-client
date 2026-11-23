#ifndef ENGINE_SHARED_KOG_FAILSAFE_H
#define ENGINE_SHARED_KOG_FAILSAFE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace KogUpdate
{
constexpr int ROLLBACK_AFTER_FAILED_STARTS = 2;
constexpr size_t MAX_BAD_VERSIONS = 64;
constexpr size_t MAX_APPLIED = 8192;

struct SState
{
	std::map<std::string, int64_t> m_Sequences;
	std::string m_PendingVersion;
	std::string m_LastStartedVersion;
	int m_StartFailures = 0;
	std::vector<std::string> m_BadVersions;
	std::vector<std::string> m_Applied;
	int64_t m_LastCheck = 0;
	std::string m_LastResult;

	std::string Serialize() const;
	static SState Parse(const char *pText);

	int64_t Sequence(const char *pChannel) const;
	void SetSequence(const char *pChannel, int64_t Sequence);
	bool IsBad(const char *pChannel, const char *pVersion) const;
	void MarkBad(const char *pChannel, const char *pVersion);
};

enum class ELaunchAction
{
	CONTINUE,
	ROLLBACK,
};

// The marker is written at every launch and removed when the main menu is reached
ELaunchAction OnLaunch(SState *pState, bool MarkerPresent, const char *pRunningVersion);
void OnStartupComplete(SState *pState, const char *pRunningVersion);
bool ConfigBackupNeeded(const SState &State, const char *pRunningVersion);

class IFileOps
{
public:
	virtual ~IFileOps() = default;
	virtual bool Exists(const std::string &Path) = 0;
	virtual bool Rename(const std::string &From, const std::string &To) = 0;
	virtual bool Remove(const std::string &Path) = 0;
};

struct SChange
{
	std::string m_Path;
	std::string m_Source;
	bool m_Remove = false;
};

// Every replaced or removed file is kept as <path>.kogprev until the next update; new files are noted so a rollback removes them
bool ApplyUpdate(IFileOps &Ops, SState *pState, const std::vector<SChange> &vChanges, const char *pNewVersion);
void RollbackUpdate(IFileOps &Ops, SState *pState, const char *pChannel);
}

#endif
