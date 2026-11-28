#ifndef ENGINE_SHARED_KOG_NOTICES_H
#define ENGINE_SHARED_KOG_NOTICES_H

#include <base/str.h>

#include <algorithm>
#include <deque>
#include <string>

// Notices of a KoG server, see docs/KOG-NOTICE.md. A server that agreed to the capability notices sends them as the
// typed message kog-notice (kog_notice_wire.h). For every other server, the backend and the server write achievements
// and completed quests as plain chat lines; only their first words tell what they are (Classify).
namespace KogNotices
{
	// The values of the kinds 1 to 7 are the ones of the wire
	enum class EKind
	{
		NONE,
		INFO,
		ACHIEVEMENT,
		QUEST, // completed
		QUEST_PROGRESS,
		MAINTENANCE,
		WARNING,
		TOURNAMENT,
	};

	// A kind that this client does not know is an info
	inline EKind KindFromWire(int Kind)
	{
		return Kind >= (int)EKind::INFO && Kind <= (int)EKind::TOURNAMENT ? (EKind)Kind : EKind::INFO;
}

inline constexpr int MAX_TOASTS_PER_WINDOW = 4;
inline constexpr float WINDOW_SECONDS = 10.0f;
inline constexpr float TOAST_SECONDS = 5.0f;
inline constexpr int MAX_TEXT_BYTES = 250;
// a maintenance banner is shown at most this long, whatever the server says
inline constexpr int MAX_COUNTDOWN_SECONDS = 3600;

// The kind of a line, and where the text after the fixed words starts
inline EKind Classify(const char *pLine, const char **ppBody)
{
	struct SPrefix
	{
		const char *m_pText;
		EKind m_Kind;
	};
	for(const SPrefix &Prefix : {SPrefix{"Achievement unlocked: ", EKind::ACHIEVEMENT}, SPrefix{"Quest completed: ", EKind::QUEST}})
	{
		if(str_startswith(pLine, Prefix.m_pText) != nullptr)
		{
			const char *pBody = pLine + str_length(Prefix.m_pText);
			if(*pBody == '\0')
				return EKind::NONE;
			*ppBody = pBody;
			return Prefix.m_Kind;
		}
	}
	return EKind::NONE;
}

// At most MAX_TOASTS_PER_WINDOW toasts in WINDOW_SECONDS; the rest stay in the chat only
class CRateLimit
{
	float m_aTimes[MAX_TOASTS_PER_WINDOW] = {};
	int m_Num = 0;

public:
	bool Allow(float Now)
	{
		int Kept = 0;
		for(int i = 0; i < m_Num; i++)
		{
			if(Now - m_aTimes[i] < WINDOW_SECONDS)
				m_aTimes[Kept++] = m_aTimes[i];
		}
		m_Num = Kept;
		if(m_Num >= MAX_TOASTS_PER_WINDOW)
			return false;
		m_aTimes[m_Num++] = Now;
		return true;
	}
	void Reset() { m_Num = 0; }
};

struct SToast
{
	EKind m_Kind = EKind::INFO;
	std::string m_Key; // empty: never replaced
	std::string m_Text;
	float m_Start = 0.0f;
	float m_Duration = TOAST_SECONDS;
	int m_Value = 0;
	int m_Limit = 0;

	// maintenance and tournament: the countdown the server started, counted here
	bool HasCountdown() const { return (m_Kind == EKind::MAINTENANCE || m_Kind == EKind::TOURNAMENT) && m_Value > 0; }
	int SecondsLeft(float Now) const { return std::max(0, (int)(m_Value - (Now - m_Start) + 0.999f)); }
	bool HasProgress() const { return m_Kind == EKind::QUEST_PROGRESS && m_Limit > 0; }
};

// How long a notice stays on the screen: a countdown until it ends, everything else a few seconds
inline float DurationOf(EKind Kind, int Value)
{
	if((Kind == EKind::MAINTENANCE || Kind == EKind::TOURNAMENT) && Value > 0)
		return (float)std::min(Value, MAX_COUNTDOWN_SECONDS);
	return TOAST_SECONDS;
}

// What is on the screen. A notice with the key of one that is still shown replaces it (a counter that moves, a
// countdown that changes) and does not count against the rate limit; a new one does, and the rest stay in the chat only.
class CToasts
{
	std::deque<SToast> m_Toasts;
	CRateLimit m_RateLimit;

public:
	bool Show(SToast Toast, float Now)
	{
		Toast.m_Start = Now;
		Toast.m_Duration = DurationOf(Toast.m_Kind, Toast.m_Value);
		if(!Toast.m_Key.empty())
		{
			for(SToast &Shown : m_Toasts)
			{
				if(Shown.m_Key == Toast.m_Key)
				{
					Shown = std::move(Toast);
					return true;
				}
			}
		}
		if(!m_RateLimit.Allow(Now))
			return false;
		m_Toasts.push_back(std::move(Toast));
		if(m_Toasts.size() > (size_t)MAX_TOASTS_PER_WINDOW)
			m_Toasts.pop_front();
		return true;
	}
	void Expire(float Now)
	{
		std::erase_if(m_Toasts, [Now](const SToast &Toast) { return Now - Toast.m_Start > Toast.m_Duration; });
	}
	void Clear()
	{
		m_Toasts.clear();
		m_RateLimit.Reset();
	}
	const std::deque<SToast> &All() const { return m_Toasts; }
	bool Empty() const { return m_Toasts.empty(); }
};
}

#endif
