#include "kog_attest.h"

#include <base/hash.h>
#include <base/mem.h>
#include <base/str.h>

#include <engine/external/monocypher/monocypher-ed25519.h>
#include <engine/shared/kog_login.h>
#include <engine/shared/packer.h>

namespace KogAttest {

bool UnpackChallenge(CUnpacker *pUnpacker, SChallenge *pChallenge)
{
	*pChallenge = SChallenge();
	const int Revision = pUnpacker->GetInt();
	const unsigned char *pNonce = pUnpacker->GetRaw(NONCE_SIZE);
	const unsigned char *pSession = pUnpacker->GetRaw(SESSION_ID_SIZE);
	const int TtlMs = pUnpacker->GetInt();
	const int NumRanges = pUnpacker->GetInt();
	if(pUnpacker->Error() || Revision < 1 || pNonce == nullptr || pSession == nullptr || TtlMs < MIN_TTL_MS || TtlMs > MAX_TTL_MS || NumRanges < 0 || NumRanges > MAX_RANGES)
		return false;
	for(int i = 0; i < NumRanges; i++)
	{
		pChallenge->m_aRanges[i].m_Module = pUnpacker->GetInt();
		pChallenge->m_aRanges[i].m_Offset = pUnpacker->GetInt();
		pChallenge->m_aRanges[i].m_Length = pUnpacker->GetInt();
	}
	if(pUnpacker->Error())
		return false;
	pChallenge->m_NumRanges = NumRanges;
	if(Revision >= 2)
	{
		const unsigned char *pCertHash = pUnpacker->GetRaw(HASH_SIZE);
		if(pUnpacker->Error() || pCertHash == nullptr)
			return false;
		pChallenge->m_HasCertHash = true;
		mem_copy(pChallenge->m_aCertHash, pCertHash, HASH_SIZE);
	}
	pChallenge->m_Revision = Revision;
	mem_copy(pChallenge->m_aNonce, pNonce, NONCE_SIZE);
	mem_copy(pChallenge->m_aSessionId, pSession, SESSION_ID_SIZE);
	pChallenge->m_TtlMs = TtlMs;
	return true;
}

void PackResponse(CAbstractPacker *pPacker, const uint8_t (&aKeyId)[KEY_ID_SIZE], EStatus Status, const uint8_t (&aMeasurement)[MEASUREMENT_SIZE], const uint8_t (&aSignature)[SIGNATURE_SIZE])
{
	pPacker->AddInt(REVISION);
	pPacker->AddRaw(aKeyId, KEY_ID_SIZE);
	pPacker->AddInt((int)Status);
	pPacker->AddRaw(aMeasurement, MEASUREMENT_SIZE);
	pPacker->AddRaw(aSignature, SIGNATURE_SIZE);
}

void KeyId(const uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE], uint8_t (&aKeyId)[KEY_ID_SIZE])
{
	const SHA256_DIGEST Digest = sha256(aPublicKey, PUBLIC_KEY_SIZE);
	mem_copy(aKeyId, Digest.data, KEY_ID_SIZE);
}

void HelloHash(const void *pHello, size_t Size, uint8_t (&aHash)[HASH_SIZE])
{
	const SHA256_DIGEST Digest = sha256(pHello, Size);
	mem_copy(aHash, Digest.data, HASH_SIZE);
}

void CertHash(const void *pCert, size_t Size, uint8_t (&aHash)[HASH_SIZE])
{
	HelloHash(pCert, Size, aHash);
}

size_t BuildSignedBytes(uint8_t *pOut, size_t OutSize, const char *pAddress, const uint8_t (&aNonce)[NONCE_SIZE], const uint8_t (&aSessionId)[SESSION_ID_SIZE], const uint8_t (&aCertHash)[HASH_SIZE], const uint8_t (&aHelloHash)[HASH_SIZE], const uint8_t (&aKeyId)[KEY_ID_SIZE], const uint8_t (&aMeasurement)[MEASUREMENT_SIZE])
{
	const size_t DomainLength = str_length(SIGN_DOMAIN) + 1; // includes the 0 byte
	const size_t AddressLength = str_length(pAddress);
	const size_t Total = DomainLength + 1 + AddressLength + NONCE_SIZE + SESSION_ID_SIZE + HASH_SIZE + HASH_SIZE + KEY_ID_SIZE + MEASUREMENT_SIZE;
	if(AddressLength == 0 || AddressLength > MAX_ADDRESS_SIZE || Total > OutSize)
		return 0;
	size_t Pos = 0;
	const auto Add = [&](const void *pData, size_t Size) {
		mem_copy(pOut + Pos, pData, Size);
		Pos += Size;
	};
	Add(SIGN_DOMAIN, DomainLength);
	pOut[Pos++] = (uint8_t)AddressLength;
	Add(pAddress, AddressLength);
	Add(aNonce, NONCE_SIZE);
	Add(aSessionId, SESSION_ID_SIZE);
	Add(aCertHash, HASH_SIZE);
	Add(aHelloHash, HASH_SIZE);
	Add(aKeyId, KEY_ID_SIZE);
	Add(aMeasurement, MEASUREMENT_SIZE);
	return Total;
}

bool Sign(const uint8_t (&aSeed)[SEED_SIZE], const SInputs &Inputs, const SChallenge &Challenge, uint8_t (&aKeyId)[KEY_ID_SIZE], uint8_t (&aSignature)[SIGNATURE_SIZE])
{
	uint8_t aSeedCopy[SEED_SIZE];
	uint8_t aSecret[64];
	uint8_t aPublic[PUBLIC_KEY_SIZE];
	mem_copy(aSeedCopy, aSeed, sizeof(aSeedCopy));
	crypto_ed25519_key_pair(aSecret, aPublic, aSeedCopy);
	KeyId(aPublic, aKeyId);

	uint8_t aMessage[MAX_SIGNED_SIZE];
	const size_t Length = BuildSignedBytes(aMessage, sizeof(aMessage), Inputs.m_pAddress, Challenge.m_aNonce, Challenge.m_aSessionId, Inputs.m_aCertHash, Inputs.m_aHelloHash, aKeyId, Inputs.m_aMeasurement);
	if(Length != 0)
		crypto_ed25519_sign(aSignature, aSecret, aMessage, Length);
	KogLogin::Wipe(aSecret, sizeof(aSecret));
	KogLogin::Wipe(aSeedCopy, sizeof(aSeedCopy));
	return Length != 0;
}

bool Verify(const uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE], const SInputs &Inputs, const SChallenge &Challenge, const uint8_t (&aKeyId)[KEY_ID_SIZE], const uint8_t (&aSignature)[SIGNATURE_SIZE])
{
	uint8_t aMessage[MAX_SIGNED_SIZE];
	const size_t Length = BuildSignedBytes(aMessage, sizeof(aMessage), Inputs.m_pAddress, Challenge.m_aNonce, Challenge.m_aSessionId, Inputs.m_aCertHash, Inputs.m_aHelloHash, aKeyId, Inputs.m_aMeasurement);
	return Length != 0 && crypto_ed25519_check(aSignature, aPublicKey, aMessage, Length) == 0;
}

void CJoin::Reset()
{
	*this = CJoin();
}

EDecision CJoin::Decide(const SDecision &Decision, const SChallenge &Challenge, const SInputs &Inputs, int64_t NowMs) const
{
	if(!Decision.m_SwitchOn || !Decision.m_ProofOn)
		return EDecision::SWITCH_OFF;
	if(!Decision.m_ServerIsKog)
		return EDecision::NOT_KOG;
	if(!Decision.m_CapabilityNegotiated)
		return EDecision::NO_CAPABILITY;
	if(!Decision.m_HasKey)
		return EDecision::NO_KEY;
	if(Decision.m_CertBad)
		return EDecision::CERT_BAD;
	if(Decision.m_CertRequired && !Decision.m_CertVerified)
		return EDecision::CERT_REQUIRED;
	// the server may only name the certificate this client saw and checked itself
	if(Challenge.m_HasCertHash && mem_comp(Challenge.m_aCertHash, Inputs.m_aCertHash, HASH_SIZE) != 0)
		return EDecision::CERT_MISMATCH;
	for(int i = 0; i < m_Answers; i++)
	{
		if(mem_comp(m_aNonces[i], Challenge.m_aNonce, NONCE_SIZE) == 0)
			return EDecision::REPLAY;
	}
	if(m_Answers >= MAX_ANSWERS_PER_JOIN)
		return EDecision::TOO_MANY;
	if(m_Answers > 0 && NowMs - m_LastMs < MIN_SPACING_MS)
		return EDecision::TOO_SOON;
	return EDecision::ANSWER;
}

void CJoin::Commit(const SChallenge &Challenge, int64_t NowMs)
{
	if(m_Answers >= MAX_ANSWERS_PER_JOIN)
		return;
	mem_copy(m_aNonces[m_Answers], Challenge.m_aNonce, NONCE_SIZE);
	m_Answers++;
	m_LastMs = NowMs;
}

const char *DecisionLabel(EDecision Decision)
{
	switch(Decision)
	{
	case EDecision::ANSWER: return "answered";
	case EDecision::SWITCH_OFF: return "switch off";
	case EDecision::NOT_KOG: return "not a listed KoG server";
	case EDecision::NO_CAPABILITY: return "server did not agree to client_attest";
	case EDecision::NO_KEY: return "no usable device key";
	case EDecision::CERT_BAD: return "server certificate is wrong or revoked";
	case EDecision::CERT_REQUIRED: return "server certificate required and missing";
	case EDecision::CERT_MISMATCH: return "challenge names another certificate";
	case EDecision::REPLAY: return "nonce answered before";
	case EDecision::TOO_MANY: return "too many challenges in this join";
	case EDecision::TOO_SOON: return "challenge came too soon";
	}
	return "";
}

}
