#include "kog_failsafe.h"

#include <base/str.h>

#include <algorithm>
#include <cstdlib>

namespace KogUpdate
{
std::string SState::Serialize() const
{
	std::string Text;
	for(const auto &[Channel, Sequence] : m_Sequences)
		Text += "sequence." + Channel + "=" + std::to_string(Sequence) + "\n";
	if(!m_PendingVersion.empty())
		Text += "pending=" + m_PendingVersion + "\n";
	if(!m_LastStartedVersion.empty())
		Text += "last_started=" + m_LastStartedVersion + "\n";
	Text += "start_failures=" + std::to_string(m_StartFailures) + "\n";
	for(const std::string &Bad : m_BadVersions)
		Text += "bad=" + Bad + "\n";
	for(const std::string &Applied : m_Applied)
		Text += "applied=" + Applied + "\n";
	if(m_LastCheck != 0)
		Text += "last_check=" + std::to_string(m_LastCheck) + "\n";
	if(!m_LastResult.empty())
		Text += "last_result=" + m_LastResult + "\n";
	return Text;
}

SState SState::Parse(const char *pText)
{
	SState State;
	std::string Remaining = pText ? pText : "";
	size_t Start = 0;
	while(Start < Remaining.size())
	{
		size_t End = Remaining.find('\n', Start);
		if(End == std::string::npos)
			End = Remaining.size();
		const std::string Line = Remaining.substr(Start, End - Start);
		Start = End + 1;
		const size_t Equals = Line.find('=');
		if(Equals == std::string::npos)
			continue;
		const std::string Key = Line.substr(0, Equals);
		const std::string Value = Line.substr(Equals + 1);
		if(Key.rfind("sequence.", 0) == 0)
		{
			const std::string Channel = Key.substr(9);
			if(!Channel.empty() && Channel.size() <= 32)
				State.m_Sequences[Channel] = strtoll(Value.c_str(), nullptr, 10);
		}
		else if(Key == "pending")
			State.m_PendingVersion = Value;
		else if(Key == "last_started")
			State.m_LastStartedVersion = Value;
		else if(Key == "start_failures")
			State.m_StartFailures = std::max(0, std::min(1000, atoi(Value.c_str())));
		else if(Key == "bad" && State.m_BadVersions.size() < MAX_BAD_VERSIONS)
			State.m_BadVersions.push_back(Value);
		else if(Key == "applied" && State.m_Applied.size() < MAX_APPLIED && Value.size() > 1)
			State.m_Applied.push_back(Value);
		else if(Key == "last_check")
			State.m_LastCheck = strtoll(Value.c_str(), nullptr, 10);
		else if(Key == "last_result")
			State.m_LastResult = Value.substr(0, 80);
	}
	return State;
}

int64_t SState::Sequence(const char *pChannel) const
{
	const auto It = m_Sequences.find(pChannel);
	return It == m_Sequences.end() ? 0 : It->second;
}

void SState::SetSequence(const char *pChannel, int64_t Sequence)
{
	m_Sequences[pChannel] = Sequence;
}

bool SState::IsBad(const char *pChannel, const char *pVersion) const
{
	const std::string Entry = std::string(pChannel) + ":" + pVersion;
	return std::find(m_BadVersions.begin(), m_BadVersions.end(), Entry) != m_BadVersions.end();
}

void SState::MarkBad(const char *pChannel, const char *pVersion)
{
	if(IsBad(pChannel, pVersion))
		return;
	if(m_BadVersions.size() >= MAX_BAD_VERSIONS)
		m_BadVersions.erase(m_BadVersions.begin());
	m_BadVersions.push_back(std::string(pChannel) + ":" + pVersion);
}

ELaunchAction OnLaunch(SState *pState, bool MarkerPresent, const char *pRunningVersion)
{
	pState->m_StartFailures = MarkerPresent ? pState->m_StartFailures + 1 : 0;
	const bool Pending = !pState->m_PendingVersion.empty() && pState->m_PendingVersion == pRunningVersion;
	return Pending && pState->m_StartFailures >= ROLLBACK_AFTER_FAILED_STARTS ? ELaunchAction::ROLLBACK : ELaunchAction::CONTINUE;
}

void OnStartupComplete(SState *pState, const char *pRunningVersion)
{
	pState->m_StartFailures = 0;
	pState->m_LastStartedVersion = pRunningVersion;
	if(pState->m_PendingVersion == pRunningVersion)
		pState->m_PendingVersion.clear();
}

bool ConfigBackupNeeded(const SState &State, const char *pRunningVersion)
{
	return !State.m_LastStartedVersion.empty() && State.m_LastStartedVersion != pRunningVersion;
}

static void Restore(IFileOps &Ops, const std::string &Entry)
{
	const std::string Path = Entry.substr(1);
	const std::string Backup = Path + ".kogprev";
	if(Entry[0] == '+')
	{
		Ops.Remove(Path);
		return;
	}
	Ops.Remove(Path);
	Ops.Rename(Backup, Path);
}

bool ApplyUpdate(IFileOps &Ops, SState *pState, const std::vector<SChange> &vChanges, const char *pNewVersion)
{
	std::vector<std::string> vDone;
	bool Success = true;
	for(const SChange &Change : vChanges)
	{
		const std::string Backup = Change.m_Path + ".kogprev";
		const bool Existed = Ops.Exists(Change.m_Path);
		if(Change.m_Remove && !Existed)
			continue;
		if(Existed)
		{
			Ops.Remove(Backup);
			if(!Ops.Rename(Change.m_Path, Backup))
			{
				Success = false;
				break;
			}
		}
		if(!Change.m_Remove && !Ops.Rename(Change.m_Source, Change.m_Path))
		{
			if(Existed)
				Ops.Rename(Backup, Change.m_Path);
			Success = false;
			break;
		}
		vDone.push_back((Change.m_Remove ? "-" : Existed ? "~" : "+") + Change.m_Path);
	}
	if(!Success)
	{
		for(auto It = vDone.rbegin(); It != vDone.rend(); ++It)
			Restore(Ops, *It);
		return false;
	}
	for(const std::string &Old : pState->m_Applied)
	{
		const std::string Path = Old.substr(1);
		const bool InThisUpdate = std::any_of(vDone.begin(), vDone.end(), [&](const std::string &Done) { return Done.substr(1) == Path; });
		if(Old[0] != '+' && !InThisUpdate)
			Ops.Remove(Path + ".kogprev");
	}
	pState->m_Applied = vDone;
	pState->m_PendingVersion = pNewVersion;
	pState->m_StartFailures = 0;
	return true;
}

void RollbackUpdate(IFileOps &Ops, SState *pState, const char *pChannel)
{
	for(auto It = pState->m_Applied.rbegin(); It != pState->m_Applied.rend(); ++It)
		Restore(Ops, *It);
	if(!pState->m_PendingVersion.empty())
		pState->MarkBad(pChannel, pState->m_PendingVersion.c_str());
	pState->m_Applied.clear();
	pState->m_PendingVersion.clear();
	pState->m_StartFailures = 0;
}
}
