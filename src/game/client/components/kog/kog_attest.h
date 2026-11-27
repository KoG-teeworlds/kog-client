#ifndef GAME_CLIENT_COMPONENTS_KOG_KOG_ATTEST_H
#define GAME_CLIENT_COMPONENTS_KOG_KOG_ATTEST_H

#include <engine/shared/kog_attest.h>

#include <game/client/component.h>

// Answers the attestation challenge of a KoG server with the install key, see docs/CLIENT-ATTEST.md. The install key
// is the device key that was registered on the account after the browser login.
class CKogAttest : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnStateChange(int NewState, int OldState) override;

	// The message kog-attest-challenge of the main connection
	void OnChallenge(class CUnpacker *pUnpacker);

	// How the last challenge of this connection ended; ANSWER once a response was sent. NONE before any challenge.
	bool HadChallenge() const { return m_HadChallenge; }
	KogAttest::EDecision LastDecision() const { return m_LastDecision; }
	int Answers() const { return m_Join.Answers(); }

private:
	KogAttest::CJoin m_Join;
	bool m_HadChallenge = false;
	KogAttest::EDecision m_LastDecision = KogAttest::EDecision::SWITCH_OFF;
};

#endif
