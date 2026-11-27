#include "kog_notice_wire.h"

#include <base/str.h>

#include <engine/shared/packer.h>

#include <algorithm>

namespace KogNoticeWire {

bool Unpack(CUnpacker *pUnpacker, SNotice *pNotice)
{
	*pNotice = SNotice();
	const int Kind = pUnpacker->GetInt();
	const char *pKey = pUnpacker->GetString(CUnpacker::SANITIZE_CC);
	const char *pText = pUnpacker->GetString(CUnpacker::SANITIZE_CC);
	const int Value = pUnpacker->GetInt();
	const int Limit = pUnpacker->GetInt();
	if(pUnpacker->Error() || pText[0] == '\0')
		return false;

	pNotice->m_Kind = KogNotices::KindFromWire(Kind);
	str_copy(pNotice->m_aKey, pKey);
	str_copy(pNotice->m_aText, pText);
	pNotice->m_Value = std::max(Value, 0);
	pNotice->m_Limit = std::max(Limit, 0);
	return true;
}

}
