#ifndef GAME_CLIENT_KOG_MOTION_H
#define GAME_CLIENT_KOG_MOTION_H

#include "kog_style.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// Eased hover state of widgets, keyed by the address of the widget's id. A fixed table (no allocation, no growth): a widget that
// is not in the table starts at its target, so a new page never animates in from nothing, and an entry that is evicted by another
// widget only costs that one widget a jump. Pure: the caller passes the time, so it is testable without a client.
namespace KogMotion
{
	class CHoverTable
	{
		static constexpr size_t SIZE = 512;
		static constexpr size_t PROBES = 8;

		struct SEntry
		{
			const void *m_pId = nullptr;
			float m_Value = 0.0f; // 0 = not hot, 1 = hot, linear in time
			double m_Time = 0.0; // when the value was last advanced
		};
		std::array<SEntry, SIZE> m_aEntries{};

		static size_t Slot(const void *pId) { return (size_t)(((uintptr_t)pId >> 4) * 2654435761u) % SIZE; }

	public:
		// The eased value in [0, 1] of the widget at Now. Several calls with the same Now (a widget asks more than once in a
		// frame) return the same value. Duration 0 (reduce motion) jumps to the target.
		float Update(const void *pId, bool Hot, double Now, float Duration)
		{
			const float Target = Hot ? 1.0f : 0.0f;
			SEntry *pFound = nullptr;
			SEntry *pOldest = nullptr;
			for(size_t Probe = 0; Probe < PROBES; ++Probe)
			{
				SEntry &Entry = m_aEntries[(Slot(pId) + Probe) % SIZE];
				if(Entry.m_pId == pId)
				{
					pFound = &Entry;
					break;
				}
				if(pOldest == nullptr || Entry.m_pId == nullptr || (pOldest->m_pId != nullptr && Entry.m_Time < pOldest->m_Time))
					pOldest = &Entry;
			}
			if(pFound == nullptr)
			{
				pFound = pOldest;
				pFound->m_pId = pId;
				pFound->m_Value = Target;
				pFound->m_Time = Now;
				return Target;
			}
			const double Elapsed = Now - pFound->m_Time;
			if(Elapsed > 0.0)
			{
				if(!(Duration > 0.0f) || Elapsed > 1.0)
					pFound->m_Value = Target;
				else
				{
					const float Step = (float)(Elapsed / Duration);
					pFound->m_Value = pFound->m_Value < Target ? std::min(pFound->m_Value + Step, Target) : std::max(pFound->m_Value - Step, Target);
				}
				pFound->m_Time = Now;
			}
			return KogStyle::EaseOutCubic(pFound->m_Value);
		}
	};
}

#endif
