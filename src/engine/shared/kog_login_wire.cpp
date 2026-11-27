#include "kog_login_wire.h"

#include <base/mem.h>
#include <base/str.h>

#include <engine/shared/packer.h>

namespace KogLoginWire {

bool UnpackChallenge(CUnpacker *pUnpacker, SChallenge *pChallenge)
{
	*pChallenge = SChallenge();
	const int Revision = pUnpacker->GetInt();
	const unsigned char *pNonce = pUnpacker->GetRaw(NONCE_SIZE);
	const unsigned char *pSession = pUnpacker->GetRaw(ID_SIZE);
	const int TtlMs = pUnpacker->GetInt();
	if(pUnpacker->Error() || Revision < 1 || pNonce == nullptr || pSession == nullptr || TtlMs < MIN_TTL_MS || TtlMs > MAX_TTL_MS)
		return false;
	pChallenge->m_Revision = Revision;
	mem_copy(pChallenge->m_aNonce, pNonce, NONCE_SIZE);
	mem_copy(pChallenge->m_aSessionId, pSession, ID_SIZE);
	pChallenge->m_TtlMs = TtlMs;
	return true;
}

void PackProof(CAbstractPacker *pPacker, const uint8_t (&aDeviceId)[ID_SIZE], const uint8_t (&aSignature)[SIGNATURE_SIZE])
{
	pPacker->AddInt(REVISION);
	pPacker->AddRaw(aDeviceId, ID_SIZE);
	pPacker->AddRaw(aSignature, SIGNATURE_SIZE);
}

bool UnpackResult(CUnpacker *pUnpacker, SResult *pResult)
{
	*pResult = SResult();
	const int Revision = pUnpacker->GetInt();
	const int Status = pUnpacker->GetInt();
	const char *pName = pUnpacker->GetString(CUnpacker::SANITIZE_CC);
	if(pUnpacker->Error() || Revision < 1 || (Status != 0 && Status != 1))
		return false;
	pResult->m_Revision = Revision;
	pResult->m_LoggedIn = Status == 1;
	if(pResult->m_LoggedIn)
		str_copy(pResult->m_aAccountName, pName);
	return true;
}

}
