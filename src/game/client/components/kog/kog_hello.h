#ifndef GAME_CLIENT_COMPONENTS_KOG_KOG_HELLO_H
#define GAME_CLIENT_COMPONENTS_KOG_KOG_HELLO_H

#include <engine/console.h>
#include <engine/shared/kog_hello.h>

#include <game/client/component.h>

// Says hello to KoG servers and keeps what both sides agreed to use, see docs/KOG-HELLO.md
class CKogHello : public CComponent
{
	static void ConKogVersion(IConsole::IResult *pResult, void *pUserData);
	static void ConKogCaps(IConsole::IResult *pResult, void *pUserData);

	uint32_t m_Offered = 0;
	uint32_t m_Negotiated = 0;
	bool m_Sent = false;
	bool m_Answered = false;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnStateChange(int NewState, int OldState) override;

	// What this client can do right now: the features that are on in the settings and have what they need
	uint32_t Offer() const;
	// Called when the connection to a server starts (after the version report). Fills the hello and returns true if
	// it is to be sent; false: send nothing, the connection stays a stock one.
	bool MakeHello(class CAbstractPacker *pPacker);
	// The answer of the server. Ignored unless a hello was sent on this connection, and only the first.
	void OnServerHello(class CUnpacker *pUnpacker);

	// The capabilities that both sides have for this connection. 0 until the server has answered.
	uint32_t Negotiated() const { return m_Negotiated; }
	bool Has(EKogCap Cap) const { return KogHello::Has(m_Negotiated, Cap); }
};

#endif
