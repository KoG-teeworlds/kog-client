#include "kog_hello.h"

#include <base/str.h>

#include <engine/shared/config.h>

#include <game/kog_version.h>

void CKogHello::ConKogVersion(IConsole::IResult *pResult, void *pUserData)
{
	CKogHello *pThis = static_cast<CKogHello *>(pUserData);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s %s (based on TClient %s and DDNet %s)", KOG_CLIENT_NAME, KOG_CLIENT_VERSION, KOG_BASED_ON_TCLIENT, KOG_BASED_ON_DDNET);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "kog", aBuf);
}

void CKogHello::OnConsoleInit()
{
	Console()->Register("kog_version", "", CFGFLAG_CLIENT, ConKogVersion, this, "Print the version of this client and what it is based on");
}
