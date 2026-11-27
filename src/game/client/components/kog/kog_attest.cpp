#include "kog_attest.h"

#include <base/time.h>

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/shared/kog_login.h>
#include <engine/shared/packer.h>

#include <game/client/gameclient.h>

void CKogAttest::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_OFFLINE || NewState == IClient::STATE_CONNECTING)
	{
		m_Join.Reset();
		m_HadChallenge = false;
	}
}

void CKogAttest::OnChallenge(CUnpacker *pUnpacker)
{
	KogAttest::SChallenge Challenge;
	if(!KogAttest::UnpackChallenge(pUnpacker, &Challenge))
		return;
	KogAttest::SInputs Inputs;
	char aAddress[64];
	if(!KogLogin::CanonicalAddress(Client()->ServerAddress(), aAddress, sizeof(aAddress)))
		return;
	Inputs.m_pAddress = aAddress;
	if(!GameClient()->m_KogHello.HelloHash(Inputs.m_aHelloHash))
		return;
	const bool CertVerified = GameClient()->m_KogTrust.LeafHash(Inputs.m_aCertHash);

	KogAttest::SDecision Decision;
	Decision.m_SwitchOn = g_Config.m_ClKogLogin != 0;
	Decision.m_ProofOn = g_Config.m_ClKogAutoLogin != 0;
	Decision.m_HasKey = GameClient()->m_KogLogin.KeyUsable();
	Decision.m_ServerIsKog = CKogLogin::ServerIsKog(ServerBrowser(), Client()->ServerAddress());
	Decision.m_CapabilityNegotiated = GameClient()->m_KogHello.Has(EKogCap::CLIENT_ATTEST);
	Decision.m_CertVerified = CertVerified;
	const CKogTrust::EVerdict Verdict = GameClient()->m_KogTrust.LastVerdict();
	Decision.m_CertBad = Verdict == CKogTrust::EVerdict::INVALID || Verdict == CKogTrust::EVerdict::REVOKED;
	Decision.m_CertRequired = g_Config.m_ClKogRequireServerCert != 0 && g_Config.m_ClKogServerCert != 0;

	const int64_t NowMs = time_get() * 1000 / time_freq();
	m_HadChallenge = true;
	m_LastDecision = m_Join.Decide(Decision, Challenge, Inputs, NowMs);
	if(m_LastDecision != KogAttest::EDecision::ANSWER)
		return;
	// counted before the signature is made: whatever happens next is not tried again for this challenge
	m_Join.Commit(Challenge, NowMs);

	uint8_t aSeed[KogAttest::SEED_SIZE];
	uint8_t aKeyId[KogAttest::KEY_ID_SIZE];
	uint8_t aSignature[KogAttest::SIGNATURE_SIZE];
	if(!GameClient()->m_KogLogin.LoadSeed(aSeed))
	{
		m_LastDecision = KogAttest::EDecision::NO_KEY;
		return;
	}
	const bool Signed = KogAttest::Sign(aSeed, Inputs, Challenge, aKeyId, aSignature);
	KogLogin::Wipe(aSeed, sizeof(aSeed));
	if(Signed)
		Client()->KogSendAttestResponse(aKeyId, Inputs.m_aMeasurement, aSignature);
}
