#include "kog_hello.h"

#include <base/str.h>

#include <engine/shared/packer.h>

namespace KogHello {

static const int gs_aIds[] = {
#define X(Name, Id, Label) Id,
	KOG_CAPABILITIES(X)
#undef X
};

static const char *const gs_apLabels[] = {
#define X(Name, Id, Label) Label,
	KOG_CAPABILITIES(X)
#undef X
};

int WireId(EKogCap Cap)
{
	return gs_aIds[(int)Cap];
}

const char *Label(EKogCap Cap)
{
	return gs_apLabels[(int)Cap];
}

bool MaySendHello(const SHelloDecision &Decision)
{
	return Decision.m_SwitchOn && Decision.m_ServerIsKog && Decision.m_MainConnection && !Decision.m_Sixup && Decision.m_Offer != 0;
}

void PackClientHello(CAbstractPacker *pPacker, uint32_t Offer, const char *pBuild, const char *pChannel)
{
	int Count = 0;
	for(int Cap = 0; Cap < (int)EKogCap::COUNT; Cap++)
		Count += (Offer >> Cap) & 1;
	pPacker->AddInt(REVISION);
	pPacker->AddInt(Count);
	for(int Cap = 0; Cap < (int)EKogCap::COUNT; Cap++)
	{
		if((Offer >> Cap) & 1)
			pPacker->AddInt(gs_aIds[Cap]);
	}
	pPacker->AddString(pBuild, 31);
	pPacker->AddString(pChannel, 15);
}

bool UnpackServerHello(CUnpacker *pUnpacker, SServerHello *pHello)
{
	*pHello = SServerHello();
	const int Revision = pUnpacker->GetInt();
	const int Listed = pUnpacker->GetInt();
	if(pUnpacker->Error() || Revision < 1 || Listed < 0 || Listed > MAX_LISTED)
		return false;

	uint32_t Mask = 0;
	int Unknown = 0;
	for(int i = 0; i < Listed; i++)
	{
		const int Id = pUnpacker->GetInt();
		if(pUnpacker->Error() || Id < 1 || Id > MAX_ID)
			return false;
		bool Known = false;
		for(int Cap = 0; Cap < (int)EKogCap::COUNT; Cap++)
		{
			if(gs_aIds[Cap] == Id)
			{
				Mask |= 1u << Cap;
				Known = true;
			}
		}
		if(!Known)
			Unknown++;
	}

	pHello->m_Revision = Revision;
	pHello->m_Mask = Mask;
	pHello->m_NumUnknown = Unknown;
	return true;
}

void FormatMask(uint32_t Mask, char *pBuf, int BufSize)
{
	pBuf[0] = '\0';
	for(int Cap = 0; Cap < (int)EKogCap::COUNT; Cap++)
	{
		if((Mask >> Cap) & 1)
		{
			if(pBuf[0] != '\0')
				str_append(pBuf, ",", BufSize);
			str_append(pBuf, gs_apLabels[Cap], BufSize);
		}
	}
	if(pBuf[0] == '\0')
		str_copy(pBuf, "none", BufSize);
}

}
