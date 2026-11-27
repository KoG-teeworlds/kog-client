#ifndef GAME_CLIENT_COMPONENTS_KOG_KOG_NOTICES_H
#define GAME_CLIENT_COMPONENTS_KOG_KOG_NOTICES_H

#include <engine/shared/kog_notices.h>

#include <game/client/component.h>

#include <deque>
#include <string>

// A toast for the lines the KoG backend writes into the chat of a player (achievements) and the KoG server writes
// for a completed quest. The line stays in the chat. Only on servers listed for the KoG community.
class CKogNotices : public CComponent
{
	struct SToast
	{
		KogNotices::EKind m_Kind;
		std::string m_Text;
		float m_Start;
	};

	std::deque<SToast> m_Toasts;
	KogNotices::CRateLimit m_RateLimit;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnStateChange(int NewState, int OldState) override;
	void OnMessage(int Msg, void *pRawMsg) override;
	void OnRender() override;
};

#endif
