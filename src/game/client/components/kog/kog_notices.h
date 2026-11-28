#ifndef GAME_CLIENT_COMPONENTS_KOG_KOG_NOTICES_H
#define GAME_CLIENT_COMPONENTS_KOG_KOG_NOTICES_H

#include <engine/shared/kog_notices.h>

#include <game/client/component.h>

class CUnpacker;

// A toast for what a KoG server tells the player: achievements, quests, maintenance. From the typed message when the
// server agreed to the capability notices (the text is then also written to the chat log, the server sends no chat
// line), otherwise from the two chat lines the backend and the server write. Only on servers listed for the KoG community.
class CKogNotices : public CComponent
{
	KogNotices::CToasts m_Toasts;

	void Add(KogNotices::SToast Toast);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnStateChange(int NewState, int OldState) override;
	void OnMessage(int Msg, void *pRawMsg) override;
	void OnRender() override;

	// The message kog-notice, read only if the server agreed to notices on this connection
	void OnNotice(CUnpacker *pUnpacker);
};

#endif
