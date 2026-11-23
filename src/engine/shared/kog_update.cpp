#include "kog_update.h"

#include <base/str.h>

#include <engine/external/monocypher/monocypher-ed25519.h>
#include <engine/shared/json.h>

#include <cstring>
#include <set>

namespace KogUpdate
{

	static const char *const ALLOWED_URL_PREFIXES[] = {
		"https://github.com/KoG-teeworlds/kog-client/releases/download/",
		"https://client.kog.tw/",
	};

	static const char *const FORBIDDEN_EXTENSIONS[] = {".exe", ".dll", ".so", ".dylib", ".sh", ".bat", ".cmd", ".ps1", ".app", ".jar", ".py"};

	static void SetError(char *pError, size_t ErrorSize, const char *pFormat, const char *pArg = "")
	{
		if(pError && ErrorSize > 0)
			str_format(pError, ErrorSize, pFormat, pArg);
	}

	bool ParseSignatureFile(const char *pText, size_t Length, unsigned char (&aSignature)[64], unsigned char (&aKeyId)[4])
	{
		static const char PREFIX[] = "kog-update-sig-v1 ";
		const size_t PrefixLength = sizeof(PREFIX) - 1;
		while(Length > 0 && (pText[Length - 1] == '\n' || pText[Length - 1] == '\r'))
			Length--;
		if(Length != PrefixLength + 8 + 1 + 128 || memcmp(pText, PREFIX, PrefixLength) != 0 || pText[PrefixLength + 8] != ' ')
			return false;
		char aKeyIdHex[9];
		memcpy(aKeyIdHex, pText + PrefixLength, 8);
		aKeyIdHex[8] = '\0';
		char aSignatureHex[129];
		memcpy(aSignatureHex, pText + PrefixLength + 9, 128);
		aSignatureHex[128] = '\0';
		return str_hex_decode(aKeyId, sizeof(aKeyId), aKeyIdHex) == 0 && str_hex_decode(aSignature, sizeof(aSignature), aSignatureHex) == 0;
	}

	bool VerifySignature(const void *pData, size_t Size, const unsigned char (&aSignature)[64], const unsigned char (&aKeyId)[4], const SPublicKey *pKeys, int NumKeys)
	{
		static const unsigned char ZERO_KEY[32] = {};
		for(int i = 0; i < NumKeys; i++)
		{
			if(memcmp(pKeys[i].m_aKey, aKeyId, sizeof(aKeyId)) != 0 || memcmp(pKeys[i].m_aKey, ZERO_KEY, sizeof(ZERO_KEY)) == 0)
				continue;
			if(crypto_ed25519_check(aSignature, pKeys[i].m_aKey, static_cast<const uint8_t *>(pData), Size) == 0)
				return true;
		}
		return false;
	}

	static bool ParseVersion(const char *pVersion, int (&aParts)[3])
	{
		const char *p = pVersion;
		for(int i = 0; i < 3; i++)
		{
			if(*p < '0' || *p > '9')
				return false;
			int Value = 0;
			int Digits = 0;
			while(*p >= '0' && *p <= '9')
			{
				Value = Value * 10 + (*p - '0');
				p++;
				if(++Digits > 4)
					return false;
			}
			aParts[i] = Value;
			if(i < 2)
			{
				if(*p != '.')
					return false;
				p++;
			}
		}
		return *p == '\0';
	}

	int CompareVersions(const char *pA, const char *pB, bool *pOk)
	{
		int aA[3];
		int aB[3];
		*pOk = ParseVersion(pA, aA) && ParseVersion(pB, aB);
		if(!*pOk)
			return 0;
		for(int i = 0; i < 3; i++)
		{
			if(aA[i] != aB[i])
				return aA[i] < aB[i] ? -1 : 1;
		}
		return 0;
	}

	bool IsAllowedPath(const char *pPath)
	{
		if(!str_startswith(pPath, "data/") || str_length(pPath) > 200)
			return false;
		bool ComponentStart = true;
		int ComponentLength = 0;
		for(const char *p = pPath; *p; p++)
		{
			const unsigned char c = *p;
			if(c == '/')
			{
				if(ComponentLength == 0)
					return false;
				ComponentStart = true;
				ComponentLength = 0;
				continue;
			}
			const bool NameChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == ' ' || c == '+';
			if(!NameChar || (ComponentStart && c == '.'))
				return false;
			ComponentStart = false;
			ComponentLength++;
		}
		if(ComponentLength == 0)
			return false;
		for(const char *pExtension : FORBIDDEN_EXTENSIONS)
		{
			if(str_endswith_nocase(pPath, pExtension))
				return false;
		}
		return true;
	}

	bool IsAllowedUrl(const char *pUrl)
	{
		if(str_length(pUrl) > 400)
			return false;
		for(const char *p = pUrl; *p; p++)
		{
			if(static_cast<unsigned char>(*p) <= ' ' || *p == '\\' || *p == '"' || *p == '@' || *p == '?' || *p == '#')
				return false;
		}
		for(const char *pPrefix : ALLOWED_URL_PREFIXES)
		{
			const char *pRest = str_startswith(pUrl, pPrefix);
			if(pRest && *pRest != '\0' && !str_find(pRest, ".."))
				return true;
		}
		return false;
	}

	static bool GetString(const json_value *pObject, const char *pName, std::string *pOut, size_t MaxLength)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		if(pValue->type != json_string || pValue->u.string.length == 0 || pValue->u.string.length > MaxLength)
			return false;
		*pOut = pValue->u.string.ptr;
		return true;
	}

	static bool GetInt(const json_value *pObject, const char *pName, int64_t *pOut)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		if(pValue->type != json_integer)
			return false;
		*pOut = pValue->u.integer;
		return true;
	}

	static bool ParseFile(const json_value *pObject, bool NeedPath, SFile *pOut, char *pError, size_t ErrorSize)
	{
		if(pObject->type != json_object)
		{
			SetError(pError, ErrorSize, "file entry is not an object");
			return false;
		}
		if(NeedPath)
		{
			if(!GetString(pObject, "path", &pOut->m_Path, 200) || !IsAllowedPath(pOut->m_Path.c_str()))
			{
				SetError(pError, ErrorSize, "file entry has a missing or forbidden path");
				return false;
			}
		}
		if(!GetString(pObject, "url", &pOut->m_Url, 400) || !IsAllowedUrl(pOut->m_Url.c_str()))
		{
			SetError(pError, ErrorSize, "file entry has a missing or forbidden url");
			return false;
		}
		std::string Sha256;
		if(!GetString(pObject, "sha256", &Sha256, 64) || Sha256.size() != 64 || sha256_from_str(&pOut->m_Sha256, Sha256.c_str()) != 0)
		{
			SetError(pError, ErrorSize, "file entry has a missing or invalid sha256");
			return false;
		}
		if(!GetInt(pObject, "size", &pOut->m_Size) || pOut->m_Size < 1 || pOut->m_Size > MAX_FILE_SIZE)
		{
			SetError(pError, ErrorSize, "file entry has a missing or invalid size");
			return false;
		}
		return true;
	}

	static bool ParseFileList(const json_value *pObject, const char *pName, std::set<std::string> *pSeen, std::vector<SFile> *pOut, char *pError, size_t ErrorSize)
	{
		const json_value *pList = json_object_get(pObject, pName);
		if(pList->type == json_none)
			return true;
		if(pList->type != json_array || pList->u.array.length + pOut->size() > MAX_FILES)
		{
			SetError(pError, ErrorSize, "\"%s\" is not an array or has too many entries", pName);
			return false;
		}
		for(unsigned int i = 0; i < pList->u.array.length; i++)
		{
			SFile File;
			if(!ParseFile(pList->u.array.values[i], true, &File, pError, ErrorSize))
				return false;
			if(!pSeen->insert(File.m_Path).second)
			{
				SetError(pError, ErrorSize, "duplicate path %s", File.m_Path.c_str());
				return false;
			}
			pOut->push_back(std::move(File));
		}
		return true;
	}

	bool ParseManifest(const char *pJson, size_t Length, const char *pPlatform, SManifest *pOut, char *pError, size_t ErrorSize)
	{
		*pOut = SManifest();
		if(Length == 0 || Length > MAX_MANIFEST_SIZE)
		{
			SetError(pError, ErrorSize, "manifest has an invalid size");
			return false;
		}
		json_value *pRoot = JsonParse(pJson, Length);
		if(pRoot == nullptr || pRoot->type != json_object)
		{
			json_value_free(pRoot);
			SetError(pError, ErrorSize, "manifest is not a JSON object");
			return false;
		}

		bool Success = false;
		do
		{
			int64_t Format = 0;
			if(!GetInt(pRoot, "format", &Format) || Format != MANIFEST_FORMAT)
			{
				SetError(pError, ErrorSize, "unsupported manifest format");
				break;
			}
			if(!GetString(pRoot, "channel", &pOut->m_Channel, 32) || !GetString(pRoot, "version", &pOut->m_Version, 32))
			{
				SetError(pError, ErrorSize, "channel or version missing");
				break;
			}
			bool VersionOk = false;
			CompareVersions(pOut->m_Version.c_str(), pOut->m_Version.c_str(), &VersionOk);
			if(!VersionOk)
			{
				SetError(pError, ErrorSize, "version is not MAJOR.MINOR.PATCH");
				break;
			}
			if(!GetInt(pRoot, "sequence", &pOut->m_Sequence) || pOut->m_Sequence < 1 || !GetInt(pRoot, "issued", &pOut->m_Issued) || !GetInt(pRoot, "expires", &pOut->m_Expires))
			{
				SetError(pError, ErrorSize, "sequence, issued or expires missing");
				break;
			}
			if(pOut->m_Expires <= pOut->m_Issued || pOut->m_Expires - pOut->m_Issued > MAX_VALIDITY_SECONDS)
			{
				SetError(pError, ErrorSize, "validity period is not between 0 and 90 days");
				break;
			}

			std::set<std::string> Seen;
			if(!ParseFileList(pRoot, "files", &Seen, &pOut->m_Files, pError, ErrorSize))
				break;

			const json_value *pPlatformEntry = json_object_get(json_object_get(pRoot, "platforms"), pPlatform);
			if(pPlatformEntry->type != json_object)
			{
				SetError(pError, ErrorSize, "no entry for platform %s", pPlatform);
				break;
			}
			const json_value *pClient = json_object_get(pPlatformEntry, "client");
			if(pClient->type != json_none)
			{
				if(!ParseFile(pClient, false, &pOut->m_Client, pError, ErrorSize))
					break;
				pOut->m_HasClient = true;
			}
			if(!ParseFileList(pPlatformEntry, "files", &Seen, &pOut->m_Files, pError, ErrorSize))
				break;

			const json_value *pRemove = json_object_get(pRoot, "remove");
			if(pRemove->type != json_none)
			{
				if(pRemove->type != json_array || pRemove->u.array.length > MAX_FILES)
				{
					SetError(pError, ErrorSize, "\"remove\" is not an array or has too many entries");
					break;
				}
				bool RemoveOk = true;
				for(unsigned int i = 0; i < pRemove->u.array.length && RemoveOk; i++)
				{
					const json_value *pEntry = pRemove->u.array.values[i];
					RemoveOk = pEntry->type == json_string && IsAllowedPath(pEntry->u.string.ptr) && Seen.count(pEntry->u.string.ptr) == 0;
					if(RemoveOk)
						pOut->m_Remove.emplace_back(pEntry->u.string.ptr);
				}
				if(!RemoveOk)
				{
					SetError(pError, ErrorSize, "\"remove\" has a forbidden path");
					break;
				}
			}
			Success = true;
		} while(false);

		json_value_free(pRoot);
		if(!Success)
			*pOut = SManifest();
		return Success;
	}

	EVerdict Decide(const SManifest &Manifest, const char *pChannel, const char *pCurrentVersion, int64_t LastSequence, int64_t Now)
	{
		if(Manifest.m_Channel != pChannel)
			return EVerdict::WRONG_CHANNEL;
		if(Now > Manifest.m_Expires)
			return EVerdict::EXPIRED;
		if(Manifest.m_Issued - Now > CLOCK_SKEW_SECONDS)
			return EVerdict::NOT_YET_VALID;
		if(Manifest.m_Sequence < LastSequence)
			return EVerdict::ROLLBACK;
		bool VersionsOk = false;
		const int Compared = CompareVersions(Manifest.m_Version.c_str(), pCurrentVersion, &VersionsOk);
		if(!VersionsOk)
			return EVerdict::BAD_VERSION;
		return Compared > 0 ? EVerdict::APPLY : EVerdict::UP_TO_DATE;
	}

}
