#ifndef ENGINE_SHARED_KOG_NOTICES_H
#define ENGINE_SHARED_KOG_NOTICES_H

#include <base/str.h>

// Which server chat lines are notices, see docs/CLIENT-SERVER-ROADMAP.md (section "Built on what is on the wire today").
// The backend and the server write them as plain chat lines; only their first words tell what they are.
namespace KogNotices
{
	enum class EKind
	{
		NONE,
		ACHIEVEMENT,
		QUEST,
	};

	inline constexpr int MAX_TOASTS_PER_WINDOW = 4;
	inline constexpr float WINDOW_SECONDS = 10.0f;
	inline constexpr float TOAST_SECONDS = 5.0f;
	inline constexpr int MAX_TEXT_BYTES = 250;

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
}

#endif
