/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "localization.h"

#include <base/io.h>
#include <base/log.h>
#include <base/os.h>
#include <base/str.h>

#include <engine/console.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>

// The arguments a format string consumes, by argument position, and the number of %%.
struct SFormatSignature
{
	std::map<int, std::string> m_Args;
	int m_Percents = 0;

	bool operator==(const SFormatSignature &Other) const { return m_Args == Other.m_Args && m_Percents == Other.m_Percents; }
};

// Returns false if the string contains %n, mixes numbered and unnumbered
// arguments or uses one argument with two different types.
static bool ParseFormat(const char *pStr, SFormatSignature &Out)
{
	int Next = 1;
	bool Numbered = false;
	bool Unnumbered = false;

	const auto ReadPosition = [&](const char *&p) {
		const char *pStart = p;
		int Pos = 0;
		while(*p >= '0' && *p <= '9')
			Pos = std::min(Pos * 10 + (*p++ - '0'), 100000);
		if(*p == '$' && p != pStart && Pos > 0)
		{
			p++;
			return Pos;
		}
		p = pStart;
		return 0;
	};
	const auto AddArg = [&](int Pos, const std::string &Type) {
		(Pos ? Numbered : Unnumbered) = true;
		if(!Pos)
			Pos = Next++;
		auto [It, Inserted] = Out.m_Args.emplace(Pos, Type);
		return Inserted || It->second == Type;
	};

	for(const char *p = pStr; *p; p++)
	{
		if(*p != '%')
			continue;
		p++;
		if(*p == '%')
		{
			Out.m_Percents++;
			continue;
		}
		const int Pos = ReadPosition(p);
		while(*p && strchr("-+ #0'I", *p))
			p++;
		for(int Part = 0; Part < 2; Part++)
		{
			if(Part == 1)
			{
				if(*p != '.')
					break;
				p++;
			}
			if(*p == '*')
			{
				p++;
				if(!AddArg(ReadPosition(p), "*"))
					return false;
			}
			else
			{
				while(*p >= '0' && *p <= '9')
					p++;
			}
		}
		std::string Type;
		while(*p && strchr("hlLqjzt", *p))
			Type += *p++;
		if(*p == 'n')
			return false;
		Type += *p ? *p : '!';
		if(!AddArg(Pos, Type))
			return false;
		if(!*p)
			break;
	}
	return !(Numbered && Unnumbered);
}

bool CLocalizationDatabase::IsValidTranslation(const char *pSource, const char *pTranslation, const char **ppReason)
{
	const char *pReason = nullptr;
	SFormatSignature Source, Translation;
	if(strlen(pTranslation) > MAX_TRANSLATION_LENGTH)
		pReason = "translation is too long";
	else if(!ParseFormat(pTranslation, Translation) || !ParseFormat(pSource, Source))
		pReason = "invalid format specifier (or %n)";
	else if(!(Source == Translation))
		pReason = "format specifiers differ from the source string";
	if(ppReason)
		*ppReason = pReason;
	return pReason == nullptr;
}

const char *Localize(const char *pStr, const char *pContext)
{
	const char *pNewStr = g_Localization.FindString(str_quickhash(pStr), str_quickhash(pContext));
	return pNewStr ? pNewStr : pStr;
}

// TClient
const char *TCLocalize(const char *pStr, const char *pContext)
{
	return Localize(pStr, pContext);
}

void CLocalizationDatabase::LoadIndexfile(IStorage *pStorage, IConsole *pConsole)
{
	m_vLanguages.clear();

	const std::vector<std::string> vEnglishLanguageCodes = {"en"};
	m_vLanguages.emplace_back("English", "", 826, vEnglishLanguageCodes);

	const char *pFilename = "languages/index.txt";
	CLineReader LineReader;
	if(!LineReader.OpenFile(pStorage->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_ALL)))
	{
		log_error("localization", "Couldn't open index file '%s'", pFilename);
		return;
	}

	while(const char *pLine = LineReader.Get())
	{
		if(!str_length(pLine) || pLine[0] == '#') // skip empty lines and comments
			continue;

		char aEnglishName[128];
		str_copy(aEnglishName, pLine);

		pLine = LineReader.Get();
		if(!pLine)
		{
			log_error("localization", "Unexpected end of index file after language '%s'", aEnglishName);
			break;
		}
		if(!str_startswith(pLine, "== "))
		{
			log_error("localization", "Missing native name for language '%s'", aEnglishName);
			(void)LineReader.Get();
			(void)LineReader.Get();
			continue;
		}
		char aNativeName[128];
		str_copy(aNativeName, pLine + 3);

		pLine = LineReader.Get();
		if(!pLine)
		{
			log_error("localization", "Unexpected end of index file after language '%s'", aEnglishName);
			break;
		}
		if(!str_startswith(pLine, "== "))
		{
			log_error("localization", "Missing country code for language '%s'", aEnglishName);
			(void)LineReader.Get();
			continue;
		}
		char aCountryCode[128];
		str_copy(aCountryCode, pLine + 3);

		pLine = LineReader.Get();
		if(!pLine)
		{
			log_error("localization", "Unexpected end of index file after language '%s'", aEnglishName);
			break;
		}
		if(!str_startswith(pLine, "== "))
		{
			log_error("localization", "Missing language codes for language '%s'", aEnglishName);
			continue;
		}
		const char *pLanguageCodes = pLine + 3;
		char aLanguageCode[256];
		std::vector<std::string> vLanguageCodes;
		while((pLanguageCodes = str_next_token(pLanguageCodes, ";", aLanguageCode, sizeof(aLanguageCode))))
		{
			if(aLanguageCode[0])
			{
				vLanguageCodes.emplace_back(aLanguageCode);
			}
		}
		if(vLanguageCodes.empty())
		{
			log_error("localization", "At least one language code required for language '%s'", aEnglishName);
			continue;
		}

		char aFilename[IO_MAX_PATH_LENGTH];
		str_format(aFilename, sizeof(aFilename), "languages/%s.txt", aEnglishName);
		m_vLanguages.emplace_back(aNativeName, aFilename, str_toint(aCountryCode), vLanguageCodes);
	}

	std::sort(m_vLanguages.begin(), m_vLanguages.end());
}

void CLocalizationDatabase::SelectDefaultLanguage(IConsole *pConsole, char *pFilename, size_t Length) const
{
	if(Languages().empty())
		return;
	if(Languages().size() == 1)
	{
		str_copy(pFilename, Languages()[0].m_Filename.c_str(), Length);
		return;
	}

	char aLocaleStr[128];
	os_locale_str(aLocaleStr, sizeof(aLocaleStr));

	log_info("localization", "Choosing default language based on user locale '%s'", aLocaleStr);

	while(true)
	{
		const CLanguage *pPrefixMatch = nullptr;
		for(const auto &Language : Languages())
		{
			for(const auto &LanguageCode : Language.m_vLanguageCodes)
			{
				if(LanguageCode == aLocaleStr)
				{
					// Exact match found, use it immediately
					str_copy(pFilename, Language.m_Filename.c_str(), Length);
					return;
				}
				else if(LanguageCode.starts_with(aLocaleStr))
				{
					// Locale is prefix of language code, e.g. locale is "en" and current language is "en-US"
					pPrefixMatch = &Language;
				}
			}
		}
		// Use prefix match if no exact match was found
		if(pPrefixMatch)
		{
			str_copy(pFilename, pPrefixMatch->m_Filename.c_str(), Length);
			return;
		}

		// Remove last segment of locale string and try again with more generic locale, e.g. "en-US" -> "en"
		int i = str_length(aLocaleStr) - 1;
		for(; i >= 0; --i)
		{
			if(aLocaleStr[i] == '-')
			{
				aLocaleStr[i] = '\0';
				break;
			}
		}

		// Stop if no more locale segments are left
		if(i <= 0)
			break;
	}
}

bool CLocalizationDatabase::Load(const char *pFilename, IStorage *pStorage, IConsole *pConsole, bool Clear)
{
	// empty string means unload
	if(pFilename[0] == 0)
	{
		m_vStrings.clear();
		m_StringsHeap.Reset();
		return true;
	}

	CLineReader LineReader;
	if(!LineReader.OpenFile(pStorage->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_ALL)))
		return false;

	log_info("localization", "loaded '%s'", pFilename);
	if(Clear)
	{
		m_vStrings.clear();
		m_StringsHeap.Reset();
	}
	char aContext[512];
	char aOrigin[512];
	int Line = 0;
	while(const char *pLine = LineReader.Get())
	{
		Line++;
		if(!str_length(pLine))
			continue;

		if(pLine[0] == '#') // skip comments
			continue;

		if(pLine[0] == '[') // context
		{
			size_t Len = str_length(pLine);
			if(Len < 1 || pLine[Len - 1] != ']')
			{
				log_error("localization", "malformed context '%s' on line %d", pLine, Line);
				continue;
			}
			str_truncate(aContext, sizeof(aContext), pLine + 1, Len - 2);
			pLine = LineReader.Get();
			if(!pLine)
			{
				log_error("localization", "unexpected end of file after context line '%s' on line %d", aContext, Line);
				break;
			}
			Line++;
		}
		else
		{
			aContext[0] = '\0';
		}

		str_copy(aOrigin, pLine);
		const char *pReplacement = LineReader.Get();
		if(!pReplacement)
		{
			log_error("localization", "unexpected end of file after original '%s' on line %d", aOrigin, Line);
			break;
		}
		Line++;

		if(pReplacement[0] != '=' || pReplacement[1] != '=' || pReplacement[2] != ' ')
		{
			log_error("localization", "malformed replacement '%s' for original '%s' on line %d", pReplacement, aOrigin, Line);
			continue;
		}

		if(str_comp(pReplacement, "== ") == 0)
			continue;

		pReplacement += 3;
		const char *pReason;
		if(!IsValidTranslation(aOrigin, pReplacement, &pReason))
		{
			log_error("localization", "ignoring translation in '%s' for '%s' on line %d: %s", pFilename, aOrigin, Line, pReason);
			continue;
		}
		AddString(aOrigin, pReplacement, aContext);
	}
	std::sort(m_vStrings.begin(), m_vStrings.end());
	return true;
}

void CLocalizationDatabase::AddString(const char *pOrgStr, const char *pNewStr, const char *pContext)
{
	const unsigned Hash = str_quickhash(pOrgStr);
	const unsigned ContextHash = str_quickhash(pContext);

	// AddString is called while a language file is being loaded, i.e. before
	// m_vStrings is sorted at the end of Load. Calling FindString here would run
	// std::equal_range on an unsorted range, which is undefined behaviour and
	// aborts when the libstdc++ debug mode (enabled in Debug builds) checks that
	// the range is partitioned. Insert in sorted position instead, so the range
	// stays partitioned and duplicates (e.g. when loading another language file
	// on top of an existing one) are skipped. AddString must only be used while
	// loading.
	const CString String(Hash, ContextHash, nullptr);
	auto It = std::lower_bound(m_vStrings.begin(), m_vStrings.end(), String);
	if(It == m_vStrings.end() || !(*It == String))
		m_vStrings.insert(It, CString(Hash, ContextHash, m_StringsHeap.StoreString(*pNewStr ? pNewStr : pOrgStr)));
}

const char *CLocalizationDatabase::FindString(unsigned Hash, unsigned ContextHash) const
{
	CString String;
	String.m_Hash = Hash;
	String.m_ContextHash = ContextHash;
	String.m_pReplacement = nullptr;
	auto Range1 = std::equal_range(m_vStrings.begin(), m_vStrings.end(), String);
	if(std::distance(Range1.first, Range1.second) == 1)
		return Range1.first->m_pReplacement;

	const unsigned DefaultHash = str_quickhash("");
	if(ContextHash != DefaultHash)
	{
		// Do another lookup with the default context hash
		String.m_ContextHash = DefaultHash;
		auto Range2 = std::equal_range(m_vStrings.begin(), m_vStrings.end(), String);
		if(std::distance(Range2.first, Range2.second) == 1)
			return Range2.first->m_pReplacement;
	}

	return nullptr;
}

CLocalizationDatabase g_Localization;
