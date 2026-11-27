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
	"https://raw.githubusercontent.com/KoG-teeworlds/kog-client-updates/published/",
};

static const char *const ALLOWED_NOTES_PREFIXES[] = {
	"https://client.kog.tw/",
	"https://github.com/KoG-teeworlds/",
};

static const char *const FORBIDDEN_EXTENSIONS[] = {".exe", ".dll", ".so", ".dylib", ".sh", ".bat", ".cmd", ".ps1", ".app", ".jar", ".py"};

bool IsKnownChannel(const char *pChannel)
{
	return str_comp(pChannel, CHANNEL_STABLE) == 0 || str_comp(pChannel, CHANNEL_CANARY) == 0 || str_comp(pChannel, CHANNEL_DEV) == 0;
}

int64_t MaxValiditySeconds(const char *pChannel)
{
	if(str_comp(pChannel, CHANNEL_DEV) == 0)
		return 14ll * 24 * 3600;
	if(str_comp(pChannel, CHANNEL_CANARY) == 0)
		return 30ll * 24 * 3600;
	return 90ll * 24 * 3600;
}

int64_t SManifest::TotalSize() const
{
	int64_t Total = m_HasClient ? m_Client.m_Size : 0;
	for(const SFile &File : m_Files)
		Total += File.m_Size;
	return Total;
}

static void SetError(char *pError, size_t ErrorSize, const char *pMessage, const char *pDetail = "")
{
	if(pError && ErrorSize > 0)
		str_format(pError, static_cast<int>(ErrorSize), "%s%s", pMessage, pDetail);
}

static bool IsZero(const unsigned char *pKey)
{
	static const unsigned char ZERO[32] = {};
	return memcmp(pKey, ZERO, sizeof(ZERO)) == 0;
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
	for(int i = 0; i < NumKeys; i++)
	{
		if(memcmp(pKeys[i].m_aKey, aKeyId, sizeof(aKeyId)) != 0 || IsZero(pKeys[i].m_aKey))
			continue;
		if(crypto_ed25519_check(aSignature, pKeys[i].m_aKey, static_cast<const uint8_t *>(pData), Size) == 0)
			return true;
	}
	return false;
}

static bool Base64Decode(const char *pText, size_t Length, std::vector<unsigned char> *pOut)
{
	unsigned Value = 0;
	int Bits = 0;
	bool Padding = false;
	for(size_t i = 0; i < Length; i++)
	{
		const char c = pText[i];
		if(c == '\n' || c == '\r')
			continue;
		if(c == '=')
		{
			Padding = true;
			continue;
		}
		int Index;
		if(c >= 'A' && c <= 'Z')
			Index = c - 'A';
		else if(c >= 'a' && c <= 'z')
			Index = c - 'a' + 26;
		else if(c >= '0' && c <= '9')
			Index = c - '0' + 52;
		else if(c == '+')
			Index = 62;
		else if(c == '/')
			Index = 63;
		else
			return false;
		if(Padding)
			return false;
		Value = (Value << 6) | static_cast<unsigned>(Index);
		Bits += 6;
		if(Bits >= 8)
		{
			Bits -= 8;
			pOut->push_back((Value >> Bits) & 0xff);
		}
	}
	return true;
}

namespace
{
struct SReader
{
	const unsigned char *m_pData;
	size_t m_Size;

	bool ReadByte(unsigned char *pOut)
	{
		if(m_Size < 1)
			return false;
		*pOut = *m_pData++;
		m_Size--;
		return true;
	}
	bool ReadU32(uint32_t *pOut)
	{
		if(m_Size < 4)
			return false;
		*pOut = ((uint32_t)m_pData[0] << 24) | ((uint32_t)m_pData[1] << 16) | ((uint32_t)m_pData[2] << 8) | m_pData[3];
		m_pData += 4;
		m_Size -= 4;
		return true;
	}
	bool ReadString(const unsigned char **ppOut, size_t *pLength)
	{
		uint32_t Length;
		if(!ReadU32(&Length) || Length > m_Size)
			return false;
		*ppOut = m_pData;
		*pLength = Length;
		m_pData += Length;
		m_Size -= Length;
		return true;
	}
};
}

static bool StringIs(const unsigned char *pData, size_t Length, const char *pString)
{
	return Length == strlen(pString) && memcmp(pData, pString, Length) == 0;
}

static void PutU32(std::vector<unsigned char> *pOut, uint32_t Value)
{
	pOut->push_back(Value >> 24);
	pOut->push_back(static_cast<uint8_t>(Value >> 16));
	pOut->push_back(static_cast<uint8_t>(Value >> 8));
	pOut->push_back(static_cast<uint8_t>(Value));
}

static void PutString(std::vector<unsigned char> *pOut, const void *pData, size_t Length)
{
	PutU32(pOut, (uint32_t)Length);
	pOut->insert(pOut->end(), static_cast<const unsigned char *>(pData), static_cast<const unsigned char *>(pData) + Length);
}

bool VerifyOwnerSignature(const void *pData, size_t Size, const char *pArmored, size_t Length, const SOwnerKey *pKeys, int NumKeys, bool AllowSoftwareKey)
{
	static const char BEGIN[] = "-----BEGIN SSH SIGNATURE-----";
	static const char END[] = "-----END SSH SIGNATURE-----";
	std::string Text(pArmored, Length);
	const size_t Begin = Text.find(BEGIN);
	const size_t End = Text.find(END);
	if(Begin != 0 || End == std::string::npos)
		return false;
	for(size_t i = End + sizeof(END) - 1; i < Text.size(); i++)
	{
		if(Text[i] != '\n' && Text[i] != '\r')
			return false;
	}
	std::vector<unsigned char> Blob;
	if(!Base64Decode(Text.data() + sizeof(BEGIN) - 1, End - (sizeof(BEGIN) - 1), &Blob))
		return false;

	SReader Reader{Blob.data(), Blob.size()};
	const unsigned char *pField;
	size_t FieldLength;
	if(Reader.m_Size < 6 || memcmp(Reader.m_pData, "SSHSIG", 6) != 0)
		return false;
	Reader.m_pData += 6;
	Reader.m_Size -= 6;
	uint32_t Version;
	if(!Reader.ReadU32(&Version) || Version != 1)
		return false;

	if(!Reader.ReadString(&pField, &FieldLength))
		return false;
	SReader Key{pField, FieldLength};
	const unsigned char *pKeyType;
	size_t KeyTypeLength;
	const unsigned char *pKeyBytes;
	size_t KeyBytesLength;
	if(!Key.ReadString(&pKeyType, &KeyTypeLength) || !Key.ReadString(&pKeyBytes, &KeyBytesLength) || KeyBytesLength != 32)
		return false;
	const bool Sk = StringIs(pKeyType, KeyTypeLength, "sk-ssh-ed25519@openssh.com");
	if(!Sk && !(AllowSoftwareKey && StringIs(pKeyType, KeyTypeLength, "ssh-ed25519")))
		return false;
	const unsigned char *pApplication = nullptr;
	size_t ApplicationLength = 0;
	if(Sk && (!Key.ReadString(&pApplication, &ApplicationLength) || Key.m_Size != 0))
		return false;
	if(!Sk && Key.m_Size != 0)
		return false;

	if(!Reader.ReadString(&pField, &FieldLength) || !StringIs(pField, FieldLength, OWNER_NAMESPACE))
		return false;
	const unsigned char *pReserved;
	size_t ReservedLength;
	if(!Reader.ReadString(&pReserved, &ReservedLength))
		return false;
	if(!Reader.ReadString(&pField, &FieldLength) || !StringIs(pField, FieldLength, "sha512"))
		return false;

	if(!Reader.ReadString(&pField, &FieldLength) || Reader.m_Size != 0)
		return false;
	SReader Signature{pField, FieldLength};
	const unsigned char *pSignatureType;
	size_t SignatureTypeLength;
	const unsigned char *pSignatureBytes;
	size_t SignatureBytesLength;
	if(!Signature.ReadString(&pSignatureType, &SignatureTypeLength) || SignatureTypeLength != KeyTypeLength || memcmp(pSignatureType, pKeyType, KeyTypeLength) != 0)
		return false;
	if(!Signature.ReadString(&pSignatureBytes, &SignatureBytesLength) || SignatureBytesLength != 64)
		return false;
	unsigned char Flags = 0;
	uint32_t Counter = 0;
	if(Sk && (!Signature.ReadByte(&Flags) || !Signature.ReadU32(&Counter)))
		return false;
	if(Signature.m_Size != 0)
		return false;
	if(Sk && (Flags & 1) == 0)
		return false;

	const SOwnerKey *pConfigured = nullptr;
	for(int i = 0; i < NumKeys && !pConfigured; i++)
	{
		if(IsZero(pKeys[i].m_aKey) || memcmp(pKeys[i].m_aKey, pKeyBytes, 32) != 0)
			continue;
		if(Sk && !StringIs(pApplication, ApplicationLength, pKeys[i].m_pApplication))
			continue;
		pConfigured = &pKeys[i];
	}
	if(!pConfigured)
		return false;

	unsigned char aHash[64];
	crypto_sha512(aHash, static_cast<const uint8_t *>(pData), Size);
	std::vector<unsigned char> Signed;
	static const char MAGIC[] = "SSHSIG";
	Signed.insert(Signed.end(), MAGIC, MAGIC + 6);
	PutString(&Signed, OWNER_NAMESPACE, strlen(OWNER_NAMESPACE));
	PutString(&Signed, pReserved, ReservedLength);
	PutString(&Signed, "sha512", 6);
	PutString(&Signed, aHash, sizeof(aHash));

	std::vector<unsigned char> Message;
	if(Sk)
	{
		const SHA256_DIGEST Application = sha256(pApplication, ApplicationLength);
		const SHA256_DIGEST Inner = sha256(Signed.data(), Signed.size());
		Message.insert(Message.end(), Application.data, Application.data + sizeof(Application.data));
		Message.push_back(Flags);
		PutU32(&Message, Counter);
		Message.insert(Message.end(), Inner.data, Inner.data + sizeof(Inner.data));
	}
	else
	{
		Message = Signed;
	}
	return crypto_ed25519_check(pSignatureBytes, pConfigured->m_aKey, Message.data(), Message.size()) == 0;
}

ESignatureResult VerifyManifestSignatures(const void *pManifest, size_t Size, const char *pCiText, size_t CiLength, const char *pOwnerText, size_t OwnerLength, const SChannelKeys *pKeys, bool AllowSoftwareOwnerKey)
{
	if(pKeys == nullptr || pKeys->m_NumCiKeys + pKeys->m_NumBackupCiKeys == 0)
		return ESignatureResult::NO_KEYS;
	unsigned char aSignature[64];
	unsigned char aKeyId[4];
	if(!ParseSignatureFile(pCiText, CiLength, aSignature, aKeyId))
		return ESignatureResult::MALFORMED_CI;
	if(!VerifySignature(pManifest, Size, aSignature, aKeyId, pKeys->m_pCiKeys, pKeys->m_NumCiKeys) && !VerifySignature(pManifest, Size, aSignature, aKeyId, pKeys->m_pBackupCiKeys, pKeys->m_NumBackupCiKeys))
		return ESignatureResult::BAD_CI;
	if(pKeys->m_NumOwnerKeys == 0)
		return ESignatureResult::OK;
	if(OwnerLength == 0)
		return ESignatureResult::MISSING_OWNER;
	if(!VerifyOwnerSignature(pManifest, Size, pOwnerText, OwnerLength, pKeys->m_pOwnerKeys, pKeys->m_NumOwnerKeys, AllowSoftwareOwnerKey))
		return ESignatureResult::BAD_OWNER;
	return ESignatureResult::OK;
}

bool ParseVersion(const char *pVersion, SVersion *pOut)
{
	*pOut = SVersion();
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
		pOut->m_aParts[i] = Value;
		if(i < 2)
		{
			if(*p != '.')
				return false;
			p++;
		}
	}
	if(*p == '\0')
		return true;
	const char *pRest = str_startswith(p, "-canary.");
	pOut->m_Kind = 1;
	if(!pRest)
	{
		pRest = str_startswith(p, "-dev.");
		pOut->m_Kind = 0;
	}
	if(!pRest || *pRest == '\0')
		return false;
	int Digits = 0;
	while(*pRest >= '0' && *pRest <= '9')
	{
		pOut->m_Number = pOut->m_Number * 10 + (*pRest - '0');
		pRest++;
		if(++Digits > 9)
			return false;
	}
	return Digits > 0 && *pRest == '\0';
}

int CompareVersions(const SVersion &A, const SVersion &B)
{
	for(int i = 0; i < 3; i++)
	{
		if(A.m_aParts[i] != B.m_aParts[i])
			return A.m_aParts[i] < B.m_aParts[i] ? -1 : 1;
	}
	if(A.m_Kind != B.m_Kind)
		return A.m_Kind < B.m_Kind ? -1 : 1;
	if(A.m_Number != B.m_Number)
		return A.m_Number < B.m_Number ? -1 : 1;
	return 0;
}

int CompareVersions(const char *pA, const char *pB, bool *pOk)
{
	SVersion A;
	SVersion B;
	*pOk = ParseVersion(pA, &A) && ParseVersion(pB, &B);
	return *pOk ? CompareVersions(A, B) : 0;
}

const char *ChannelOfVersion(const char *pVersion)
{
	SVersion Version;
	if(!ParseVersion(pVersion, &Version))
		return nullptr;
	return Version.m_Kind == 0 ? CHANNEL_DEV : Version.m_Kind == 1 ? CHANNEL_CANARY : CHANNEL_STABLE;
}

bool IsAllowedPath(const char *pPath)
{
	if(!str_startswith(pPath, "data/") || str_length(pPath) > 200)
		return false;
	bool ComponentStart = true;
	int ComponentLength = 0;
	for(const char *p = pPath; *p; p++)
	{
		const unsigned char c = static_cast<unsigned char>(*p);
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

static bool IsAllowedLink(const char *pUrl, const char *const *ppPrefixes, size_t NumPrefixes)
{
	if(str_length(pUrl) > 400)
		return false;
	for(const char *p = pUrl; *p; p++)
	{
		if(static_cast<unsigned char>(*p) <= ' ' || *p == '\\' || *p == '"' || *p == '@' || *p == '?' || *p == '#')
			return false;
	}
	for(size_t i = 0; i < NumPrefixes; i++)
	{
		const char *pRest = str_startswith(pUrl, ppPrefixes[i]);
		if(pRest && *pRest != '\0' && !str_find(pRest, ".."))
			return true;
	}
	return false;
}

bool IsAllowedUrl(const char *pUrl)
{
	return IsAllowedLink(pUrl, ALLOWED_URL_PREFIXES, std::size(ALLOWED_URL_PREFIXES));
}

bool IsAllowedNotesUrl(const char *pUrl)
{
	return IsAllowedLink(pUrl, ALLOWED_NOTES_PREFIXES, std::size(ALLOWED_NOTES_PREFIXES));
}

bool IsPlainText(const char *pText, size_t MaxLength)
{
	const size_t Length = static_cast<size_t>(str_length(pText));
	if(Length == 0 || Length > MaxLength || !str_utf8_check(pText))
		return false;
	const unsigned char *p = reinterpret_cast<const unsigned char *>(pText);
	for(; *p; p++)
	{
		if(*p < 0x20 || *p == 0x7f || *p == '<' || *p == '>')
			return false;
		// direction overrides and isolates (U+200E, U+200F, U+202A to U+202E, U+2066 to U+2069)
		if(p[0] == 0xe2 && ((p[1] == 0x80 && (p[2] == 0x8e || p[2] == 0x8f || (p[2] >= 0xaa && p[2] <= 0xae))) || (p[1] == 0x81 && p[2] >= 0xa6 && p[2] <= 0xa9)))
			return false;
	}
	return true;
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
	std::string Url;
	if(!GetString(pObject, "url", &Url, 400) || !IsAllowedUrl(Url.c_str()))
	{
		SetError(pError, ErrorSize, "file entry has a missing or forbidden url");
		return false;
	}
	pOut->m_vUrls.push_back(Url);
	const json_value *pMirrors = json_object_get(pObject, "mirrors");
	if(pMirrors->type != json_none)
	{
		if(pMirrors->type != json_array || pMirrors->u.array.length + 1 > (unsigned)MAX_URLS)
		{
			SetError(pError, ErrorSize, "file entry has too many mirrors");
			return false;
		}
		for(unsigned int i = 0; i < pMirrors->u.array.length; i++)
		{
			const json_value *pMirror = pMirrors->u.array.values[i];
			if(pMirror->type != json_string || !IsAllowedUrl(pMirror->u.string.ptr))
			{
				SetError(pError, ErrorSize, "file entry has a forbidden mirror");
				return false;
			}
			pOut->m_vUrls.emplace_back(pMirror->u.string.ptr);
		}
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
		SetError(pError, ErrorSize, "list is not an array or has too many entries: ", pName);
		return false;
	}
	for(unsigned int i = 0; i < pList->u.array.length; i++)
	{
		SFile File;
		if(!ParseFile(pList->u.array.values[i], true, &File, pError, ErrorSize))
			return false;
		if(!pSeen->insert(File.m_Path).second)
		{
			SetError(pError, ErrorSize, "duplicate path ", File.m_Path.c_str());
			return false;
		}
		pOut->push_back(std::move(File));
	}
	return true;
}

static bool ParseOptionalFields(const json_value *pRoot, SManifest *pOut, char *pError, size_t ErrorSize)
{
	const json_value *pRollout = json_object_get(pRoot, "rollout_percent");
	if(pRollout->type != json_none)
	{
		if(pRollout->type != json_integer || pRollout->u.integer < 0 || pRollout->u.integer > 100)
		{
			SetError(pError, ErrorSize, "rollout_percent is not between 0 and 100");
			return false;
		}
		pOut->m_RolloutPercent = (int)pRollout->u.integer;
	}
	const json_value *pHalted = json_object_get(pRoot, "halted");
	if(pHalted->type != json_none)
	{
		if(pHalted->type != json_boolean)
		{
			SetError(pError, ErrorSize, "halted is not a boolean");
			return false;
		}
		pOut->m_Halted = pHalted->u.boolean != 0;
	}
	const json_value *pMinVersion = json_object_get(pRoot, "min_version");
	if(pMinVersion->type != json_none)
	{
		SVersion Min;
		if(pMinVersion->type != json_string || !ParseVersion(pMinVersion->u.string.ptr, &Min))
		{
			SetError(pError, ErrorSize, "min_version is not a version");
			return false;
		}
		pOut->m_MinVersion = pMinVersion->u.string.ptr;
	}
	const json_value *pNotes = json_object_get(pRoot, "notes_url");
	if(pNotes->type != json_none)
	{
		if(pNotes->type != json_string || !IsAllowedNotesUrl(pNotes->u.string.ptr))
		{
			SetError(pError, ErrorSize, "notes_url is forbidden");
			return false;
		}
		pOut->m_NotesUrl = pNotes->u.string.ptr;
	}
	const json_value *pChanges = json_object_get(pRoot, "changes");
	if(pChanges->type != json_none)
	{
		if(pChanges->type != json_array || pChanges->u.array.length > (unsigned)MAX_CHANGES)
		{
			SetError(pError, ErrorSize, "changes has too many entries");
			return false;
		}
		for(unsigned int i = 0; i < pChanges->u.array.length; i++)
		{
			const json_value *pChange = pChanges->u.array.values[i];
			if(pChange->type != json_string || !IsPlainText(pChange->u.string.ptr, MAX_CHANGE_LENGTH))
			{
				SetError(pError, ErrorSize, "a change is not plain text of at most 120 characters");
				return false;
			}
			pOut->m_vChanges.emplace_back(pChange->u.string.ptr);
		}
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
		if(!GetString(pRoot, "channel", &pOut->m_Channel, 32) || !GetString(pRoot, "version", &pOut->m_Version, 32) || !IsKnownChannel(pOut->m_Channel.c_str()))
		{
			SetError(pError, ErrorSize, "channel or version missing");
			break;
		}
		const char *pVersionChannel = ChannelOfVersion(pOut->m_Version.c_str());
		if(pVersionChannel == nullptr || pOut->m_Channel != pVersionChannel)
		{
			SetError(pError, ErrorSize, "version does not belong to the channel");
			break;
		}
		if(!GetInt(pRoot, "sequence", &pOut->m_Sequence) || pOut->m_Sequence < 1 || !GetInt(pRoot, "issued", &pOut->m_Issued) || !GetInt(pRoot, "expires", &pOut->m_Expires))
		{
			SetError(pError, ErrorSize, "sequence, issued or expires missing");
			break;
		}
		if(pOut->m_Expires <= pOut->m_Issued || pOut->m_Expires - pOut->m_Issued > MaxValiditySeconds(pOut->m_Channel.c_str()))
		{
			SetError(pError, ErrorSize, "validity period is longer than the channel allows");
			break;
		}
		if(!ParseOptionalFields(pRoot, pOut, pError, ErrorSize))
			break;

		std::set<std::string> Seen;
		if(!ParseFileList(pRoot, "files", &Seen, &pOut->m_Files, pError, ErrorSize))
			break;

		const json_value *pPlatformEntry = json_object_get(json_object_get(pRoot, "platforms"), pPlatform);
		if(pPlatformEntry->type != json_object)
		{
			SetError(pError, ErrorSize, "no entry for platform ", pPlatform);
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

int Bucket(const char *pInstallId)
{
	const SHA256_DIGEST Digest = sha256(pInstallId, static_cast<size_t>(str_length(pInstallId)));
	const uint32_t Value = ((uint32_t)Digest.data[0] << 24) | ((uint32_t)Digest.data[1] << 16) | ((uint32_t)Digest.data[2] << 8) | Digest.data[3];
	return (int)(Value % 100);
}

bool UpdateRequired(const SManifest &Manifest, const char *pCurrentVersion)
{
	if(Manifest.m_MinVersion.empty() || Manifest.m_Halted)
		return false;
	bool Ok = false;
	return CompareVersions(Manifest.m_MinVersion.c_str(), pCurrentVersion, &Ok) > 0 && Ok;
}

EVerdict Decide(const SManifest &Manifest, const SDecideInput &Input)
{
	if(Manifest.m_Channel != Input.m_pChannel)
		return EVerdict::WRONG_CHANNEL;
	if(Input.m_Now > Manifest.m_Expires)
		return EVerdict::EXPIRED;
	if(Manifest.m_Issued - Input.m_Now > CLOCK_SKEW_SECONDS)
		return EVerdict::NOT_YET_VALID;
	if(Manifest.m_Sequence < Input.m_LastSequence)
		return EVerdict::ROLLBACK;

	bool VersionsOk = false;
	const int Compared = CompareVersions(Manifest.m_Version.c_str(), Input.m_pCurrentVersion, &VersionsOk);
	const char *pCurrentChannel = ChannelOfVersion(Input.m_pCurrentVersion);
	if(!VersionsOk || pCurrentChannel == nullptr)
		return EVerdict::BAD_VERSION;
	if(Manifest.m_Version == Input.m_pBadVersion)
		return EVerdict::KNOWN_BAD;
	if(Manifest.m_Halted)
		return EVerdict::HALTED;

	const bool Switching = Manifest.m_Channel != pCurrentChannel;
	if(Switching && Input.m_SwitchConfirmed)
		return EVerdict::APPLY;
	if(Compared <= 0)
		return Switching ? EVerdict::NEEDS_CONFIRMATION : EVerdict::UP_TO_DATE;
	if(Input.m_Bucket >= Manifest.m_RolloutPercent && !UpdateRequired(Manifest, Input.m_pCurrentVersion) && !Switching)
		return EVerdict::NOT_IN_ROLLOUT;
	return EVerdict::APPLY;
}
}
