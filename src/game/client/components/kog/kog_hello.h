#ifndef GAME_CLIENT_COMPONENTS_KOG_KOG_HELLO_H
#define GAME_CLIENT_COMPONENTS_KOG_KOG_HELLO_H

#include <engine/console.h>
#include <engine/shared/kog_protocol.h>

#include <game/client/component.h>

class CKogHello : public CComponent
{
	static void ConKogVersion(IConsole::IResult *pResult, void *pUserData);

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;

	static constexpr unsigned Capabilities() { return KOG_CAP_NONE; }
};

#endif
