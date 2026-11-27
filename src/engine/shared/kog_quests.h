#ifndef ENGINE_SHARED_KOG_QUESTS_H
#define ENGINE_SHARED_KOG_QUESTS_H

#include <base/str.h>

#include <string>
#include <vector>

// The quest lines a KoG map carries in its server settings, as the server reads them (docs/MAP-QUESTS.md of the server):
//   kog_quest <id> <individual|team> "<name>" "<description>" <stat>[:<param>] <op> <number> [...]
// The client only lists them; the server decides what is completed. A line the server would refuse is not listed, so
// the list never shows a quest that cannot be completed.
namespace KogQuests
{
inline constexpr int MAX_QUESTS = 32;
inline constexpr int MAX_ID_LENGTH = 32;
inline constexpr int MAX_NAME_BYTES = 48;
inline constexpr int MAX_DESCRIPTION_BYTES = 160;
inline constexpr int MAX_CONDITIONS = 8;

struct SQuest
{
	std::string m_Id;
	bool m_Team = false;
	std::string m_Name;
	std::string m_Description;
	int m_NumConditions = 0;
};

namespace Detail
{
inline const char *SkipSpace(const char *p)
{
	while(*p == ' ' || *p == '\t')
		p++;
	return p;
}

inline bool Word(const char *&p, std::string &Out)
{
	p = SkipSpace(p);
	const char *Start = p;
	while(*p != '\0' && *p != ' ' && *p != '\t')
		p++;
	Out.assign(Start, p - Start);
	return p != Start;
}

inline bool Quoted(const char *&p, std::string &Out, size_t MaxBytes)
{
	p = SkipSpace(p);
	if(*p != '"')
		return false;
	const char *Start = ++p;
	while(*p != '\0' && *p != '"')
		p++;
	if(*p != '"')
		return false;
	Out.assign(Start, p - Start);
	p++;
	if(Out.empty() || Out.size() > MaxBytes || str_utf8_check(Out.c_str()) == 0)
		return false;
	for(const unsigned char c : Out)
	{
		if(c < 0x20 || c == 0x7f)
			return false;
	}
	return true;
}

inline bool ValidId(const std::string &Id)
{
	if(Id.empty() || Id.size() > (size_t)MAX_ID_LENGTH)
		return false;
	for(const char c : Id)
	{
		if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
			return false;
	}
	return true;
}

inline bool KnownStat(const std::string &Stat)
{
	for(const char *pKnown : {"freeze_max", "weapon_fired", "team_size", "freeze_count", "run_time", "spectated", "death_count", "checkpoint_count"})
	{
		if(Stat == pKnown)
			return true;
	}
	return false;
}

inline bool Condition(const char *&p, int &NumConditions)
{
	std::string Token;
	if(!Word(p, Token))
		return false;
	std::string Stat = Token;
	const size_t Colon = Token.find(':');
	if(Colon != std::string::npos)
	{
		Stat = Token.substr(0, Colon);
		const std::string Param = Token.substr(Colon + 1);
		if(Stat != "weapon_fired" || Param.size() != 1 || Param[0] < '0' || Param[0] > '5')
			return false;
	}
	if(!KnownStat(Stat))
		return false;

	std::string Op;
	if(!Word(p, Op))
		return false;
	if(Op != "<" && Op != "<=" && Op != ">" && Op != ">=" && Op != "==" && Op != "!=")
		return false;

	std::string Number;
	if(!Word(p, Number))
		return false;
	bool Dot = false;
	bool Digit = false;
	for(size_t i = 0; i < Number.size(); i++)
	{
		const char c = Number[i];
		if(c >= '0' && c <= '9')
			Digit = true;
		else if(c == '.' && !Dot)
			Dot = true;
		else if(!(c == '-' && i == 0))
			return false;
	}
	if(!Digit)
		return false;
	NumConditions++;
	return true;
}
}

// Reads one settings line. Returns false for a line that is not a quest or that the server would refuse.
inline bool ParseLine(const char *pLine, SQuest *pQuest)
{
	const char *p = Detail::SkipSpace(pLine);
	if(str_comp_nocase_num(p, "kog_quest", 9) != 0 || (p[9] != ' ' && p[9] != '\t'))
		return false;
	p += 9;

	SQuest Quest;
	std::string Scope;
	if(!Detail::Word(p, Quest.m_Id) || !Detail::ValidId(Quest.m_Id))
		return false;
	if(!Detail::Word(p, Scope) || (Scope != "individual" && Scope != "team"))
		return false;
	Quest.m_Team = Scope == "team";
	if(!Detail::Quoted(p, Quest.m_Name, MAX_NAME_BYTES) || !Detail::Quoted(p, Quest.m_Description, MAX_DESCRIPTION_BYTES))
		return false;

	while(*Detail::SkipSpace(p) != '\0')
	{
		if(Quest.m_NumConditions >= MAX_CONDITIONS || !Detail::Condition(p, Quest.m_NumConditions))
			return false;
	}
	if(Quest.m_NumConditions == 0)
		return false;

	*pQuest = std::move(Quest);
	return true;
}

// The quests of the map that is loaded
class CList
{
	std::vector<SQuest> m_vQuests;

public:
	void Clear() { m_vQuests.clear(); }
	// A quest id that is already listed, and more than MAX_QUESTS, are refused like the server does
	bool AddLine(const char *pLine)
	{
		SQuest Quest;
		if(m_vQuests.size() >= (size_t)MAX_QUESTS || !ParseLine(pLine, &Quest))
			return false;
		for(const SQuest &Known : m_vQuests)
		{
			if(Known.m_Id == Quest.m_Id)
				return false;
		}
		m_vQuests.push_back(std::move(Quest));
		return true;
	}
	const std::vector<SQuest> &Quests() const { return m_vQuests; }
	bool Empty() const { return m_vQuests.empty(); }
};
}

#endif
