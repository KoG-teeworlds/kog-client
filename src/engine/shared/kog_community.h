#ifndef ENGINE_SHARED_KOG_COMMUNITY_H
#define ENGINE_SHARED_KOG_COMMUNITY_H

namespace KogCommunity
{
constexpr const char *ID = "kog";

enum class EDefault
{
	WAIT,
	KEEP,
	KOG,
	DDNET,
};

struct SState
{
	bool m_FirstStart;
	bool m_SwitchOn;
	bool m_InfoAvailable;
	bool m_HasKog;
	bool m_HasDDNet;
	bool m_Touched;
};

// Only a first start (no config file yet) gets a default, never a returning player.
inline EDefault PickDefault(const SState &State)
{
	if(!State.m_FirstStart)
		return EDefault::KEEP;
	if(!State.m_InfoAvailable)
		return EDefault::WAIT;
	if(State.m_Touched)
		return EDefault::KEEP;
	if(State.m_SwitchOn && State.m_HasKog)
		return EDefault::KOG;
	if(State.m_HasDDNet)
		return EDefault::DDNET;
	return EDefault::KEEP;
}
} // namespace KogCommunity

#endif
