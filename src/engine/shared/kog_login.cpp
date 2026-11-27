#include "kog_login.h"

#include <base/detect.h>
#include <base/hash.h>
#include <base/mem.h>
#include <base/secure.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/external/monocypher/monocypher-ed25519.h>

#include <cstring>

namespace KogLogin {

static const char BASE64URL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

void Base64UrlEncode(char *pDst, size_t DstSize, const void *pData, size_t DataSize)
{
	const uint8_t *pIn = static_cast<const uint8_t *>(pData);
	size_t Out = 0;
	for(size_t i = 0; i < DataSize; i += 3)
	{
		const size_t Left = DataSize - i;
		const uint32_t Value = (uint32_t(pIn[i]) << 16) | (Left > 1 ? uint32_t(pIn[i + 1]) << 8 : 0) | (Left > 2 ? uint32_t(pIn[i + 2]) : 0);
		const size_t Chars = Left > 2 ? 4 : Left + 1;
		for(size_t j = 0; j < Chars; j++)
		{
			if(Out + 1 >= DstSize)
			{
				pDst[Out] = '\0';
				return;
			}
			pDst[Out++] = BASE64URL[(Value >> (18 - 6 * j)) & 63];
		}
	}
	if(DstSize > 0)
		pDst[Out] = '\0';
}

int Base64UrlDecode(void *pDst, size_t DstSize, const char *pSrc)
{
	const size_t Length = str_length(pSrc);
	if(Length % 4 == 1)
		return -1;
	uint8_t *pOut = static_cast<uint8_t *>(pDst);
	size_t Out = 0;
	uint32_t Value = 0;
	int Bits = 0;
	for(size_t i = 0; i < Length; i++)
	{
		const char *pPos = strchr(BASE64URL, pSrc[i]);
		if(!pPos || pSrc[i] == '\0')
			return -1;
		Value = ((Value << 6) | uint32_t(pPos - BASE64URL)) & 0xffffff;
		Bits += 6;
		if(Bits >= 8)
		{
			Bits -= 8;
			if(Out >= DstSize)
				return -1;
			pOut[Out++] = (Value >> Bits) & 0xff;
		}
	}
	if(Bits > 0 && (Value & ((1u << Bits) - 1)) != 0)
		return -1;
	return (int)Out;
}

void UrlEncode(char *pDst, size_t DstSize, const char *pSrc)
{
	static const char HEX[] = "0123456789ABCDEF";
	size_t Out = 0;
	for(; *pSrc; pSrc++)
	{
		const unsigned char c = *pSrc;
		const bool Plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
		if(Out + (Plain ? 1 : 3) + 1 > DstSize)
			break;
		if(Plain)
			pDst[Out++] = c;
		else
		{
			pDst[Out++] = '%';
			pDst[Out++] = HEX[c >> 4];
			pDst[Out++] = HEX[c & 15];
		}
	}
	if(DstSize > 0)
		pDst[Out] = '\0';
}

static int HexValue(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

bool UrlDecode(char *pDst, size_t DstSize, const char *pSrc, size_t SrcLength)
{
	size_t Out = 0;
	for(size_t i = 0; i < SrcLength; i++)
	{
		char c = pSrc[i];
		if(c == '%')
		{
			if(i + 2 >= SrcLength)
				return false;
			const int Hi = HexValue(pSrc[i + 1]);
			const int Lo = HexValue(pSrc[i + 2]);
			if(Hi < 0 || Lo < 0)
				return false;
			c = (char)(Hi * 16 + Lo);
			i += 2;
		}
		else if(c == '+')
			c = ' ';
		if(c == '\0' || Out + 1 >= DstSize)
			return false;
		pDst[Out++] = c;
	}
	pDst[Out] = '\0';
	return true;
}

void Wipe(void *pData, size_t Size)
{
	volatile uint8_t *p = static_cast<volatile uint8_t *>(pData);
	for(size_t i = 0; i < Size; i++)
		p[i] = 0;
}

void MakeVerifier(char (&aVerifier)[VERIFIER_LENGTH + 1])
{
	// 48 random bytes make 64 base64url characters, all from the unreserved set of RFC 7636
	uint8_t aRandom[48];
	secure_random_fill(aRandom, sizeof(aRandom));
	Base64UrlEncode(aVerifier, sizeof(aVerifier), aRandom, sizeof(aRandom));
	Wipe(aRandom, sizeof(aRandom));
}

void MakeState(char (&aState)[STATE_LENGTH + 1])
{
	uint8_t aRandom[24];
	secure_random_fill(aRandom, sizeof(aRandom));
	Base64UrlEncode(aState, sizeof(aState), aRandom, sizeof(aRandom));
	Wipe(aRandom, sizeof(aRandom));
}

void CodeChallenge(const char *pVerifier, char (&aChallenge)[CHALLENGE_LENGTH + 1])
{
	const SHA256_DIGEST Digest = sha256(pVerifier, str_length(pVerifier));
	Base64UrlEncode(aChallenge, sizeof(aChallenge), Digest.data, sizeof(Digest.data));
}

void CutDeviceName(char *pDst, size_t DstSize, const char *pSrc)
{
	char aCopy[512];
	str_copy(aCopy, pSrc);
	str_utf8_truncate(pDst, DstSize, str_utf8_skip_whitespaces(aCopy), MAX_DEVICE_NAME);
	str_utf8_trim_right(pDst);
}

void DefaultDeviceName(char *pBuf, size_t Size)
{
#if defined(CONF_PLATFORM_MACOS)
	CutDeviceName(pBuf, Size, "KoG Client (macOS)");
#elif defined(CONF_FAMILY_WINDOWS)
	CutDeviceName(pBuf, Size, "KoG Client (Windows)");
#elif defined(CONF_PLATFORM_LINUX)
	CutDeviceName(pBuf, Size, "KoG Client (Linux)");
#else
	CutDeviceName(pBuf, Size, "KoG Client");
#endif
}

bool BuildAuthorizeUrl(char *pBuf, size_t Size, const char *pAuthorizeEndpoint, const char *pRedirectUri, const char *pState, const char *pChallenge, const char *pDeviceName)
{
	char aRedirect[256], aScope[128], aDevice[512];
	UrlEncode(aRedirect, sizeof(aRedirect), pRedirectUri);
	UrlEncode(aScope, sizeof(aScope), SCOPE);
	UrlEncode(aDevice, sizeof(aDevice), pDeviceName);
	const int Length = str_format(pBuf, Size, "%s%cresponse_type=code&client_id=%s&redirect_uri=%s&scope=%s&state=%s&code_challenge=%s&code_challenge_method=S256&device_name=%s",
		pAuthorizeEndpoint, strchr(pAuthorizeEndpoint, '?') ? '&' : '?', CLIENT_ID, aRedirect, aScope, pState, pChallenge, aDevice);
	return Length > 0 && (size_t)Length < Size;
}

static bool ConstantTimeEquals(const char *pA, const char *pB)
{
	const size_t LengthA = str_length(pA);
	const size_t LengthB = str_length(pB);
	unsigned Diff = (unsigned)(LengthA ^ LengthB);
	for(size_t i = 0; i < LengthA; i++)
		Diff |= (unsigned char)pA[i] ^ (unsigned char)pB[LengthB ? i % LengthB : 0];
	return Diff == 0;
}

ECallback ParseCallback(const char *pRequest, size_t Length, const char *pExpectedState, char *pCode, size_t CodeSize, char *pError, size_t ErrorSize)
{
	if(pCode && CodeSize)
		pCode[0] = '\0';
	if(pError && ErrorSize)
		pError[0] = '\0';

	const char *pLineEnd = static_cast<const char *>(memchr(pRequest, '\n', Length));
	if(!pLineEnd)
		return ECallback::MALFORMED;
	size_t LineLength = pLineEnd - pRequest;
	if(LineLength > 0 && pRequest[LineLength - 1] == '\r')
		LineLength--;

	if(LineLength < 4 || str_comp_num(pRequest, "GET ", 4) != 0)
	{
		const bool HasSpace = memchr(pRequest, ' ', LineLength) != nullptr;
		return HasSpace ? ECallback::WRONG_METHOD : ECallback::MALFORMED;
	}
	const char *pTarget = pRequest + 4;
	const char *pLine = pRequest + LineLength;
	const char *pTargetEnd = static_cast<const char *>(memchr(pTarget, ' ', pLine - pTarget));
	if(!pTargetEnd || pLine - pTargetEnd < 9 || str_comp_num(pTargetEnd + 1, "HTTP/1.", 7) != 0)
		return ECallback::MALFORMED;

	const char *pQuery = static_cast<const char *>(memchr(pTarget, '?', pTargetEnd - pTarget));
	const size_t PathLength = (pQuery ? pQuery : pTargetEnd) - pTarget;
	if(PathLength != (size_t)str_length(CALLBACK_PATH) || str_comp_num(pTarget, CALLBACK_PATH, PathLength) != 0)
		return ECallback::WRONG_PATH;
	if(!pQuery)
		return ECallback::BAD_STATE;
	pQuery++;

	char aState[128] = "", aCode[MAX_CODE_LENGTH + 1] = "", aErr[128] = "";
	bool HaveState = false, HaveCode = false, HaveErr = false, Duplicate = false;
	const char *pPos = pQuery;
	while(pPos < pTargetEnd)
	{
		const char *pAmp = static_cast<const char *>(memchr(pPos, '&', pTargetEnd - pPos));
		const char *pEnd = pAmp ? pAmp : pTargetEnd;
		const char *pEq = static_cast<const char *>(memchr(pPos, '=', pEnd - pPos));
		if(pEq)
		{
			const size_t NameLength = pEq - pPos;
			char *pValue = nullptr;
			size_t ValueSize = 0;
			bool *pHave = nullptr;
			if(NameLength == 5 && str_comp_num(pPos, "state", 5) == 0)
			{
				pValue = aState;
				ValueSize = sizeof(aState);
				pHave = &HaveState;
			}
			else if(NameLength == 4 && str_comp_num(pPos, "code", 4) == 0)
			{
				pValue = aCode;
				ValueSize = sizeof(aCode);
				pHave = &HaveCode;
			}
			else if(NameLength == 5 && str_comp_num(pPos, "error", 5) == 0)
			{
				pValue = aErr;
				ValueSize = sizeof(aErr);
				pHave = &HaveErr;
			}
			if(pHave)
			{
				if(*pHave)
					Duplicate = true;
				*pHave = true;
				if(!UrlDecode(pValue, ValueSize, pEq + 1, pEnd - (pEq + 1)))
				{
					Wipe(aCode, sizeof(aCode));
					return ECallback::MALFORMED;
				}
			}
		}
		pPos = pEnd + 1;
	}

	ECallback Result = ECallback::CODE;
	if(Duplicate)
		Result = ECallback::MALFORMED;
	else if(!HaveState || !ConstantTimeEquals(aState, pExpectedState))
		Result = ECallback::BAD_STATE;
	else if(HaveErr)
	{
		if(pError && ErrorSize)
			str_copy(pError, aErr, ErrorSize);
		Result = ECallback::SERVER_ERROR;
	}
	else if(!HaveCode || aCode[0] == '\0')
		Result = ECallback::MISSING_CODE;
	else if(pCode && CodeSize)
		str_copy(pCode, aCode, CodeSize);
	Wipe(aCode, sizeof(aCode));
	return Result;
}

void GenerateSeed(uint8_t (&aSeed)[SEED_SIZE])
{
	secure_random_fill(aSeed, sizeof(aSeed));
}

void PublicKeyFromSeed(const uint8_t (&aSeed)[SEED_SIZE], uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE])
{
	uint8_t aSeedCopy[SEED_SIZE];
	uint8_t aSecret[64];
	mem_copy(aSeedCopy, aSeed, sizeof(aSeedCopy));
	crypto_ed25519_key_pair(aSecret, aPublicKey, aSeedCopy);
	Wipe(aSecret, sizeof(aSecret));
	Wipe(aSeedCopy, sizeof(aSeedCopy));
}

bool ParseUuid(const char *pText, uint8_t (&aUuid)[UUID_SIZE])
{
	if(str_length(pText) != 36)
		return false;
	size_t Out = 0;
	for(size_t i = 0; i < 36; i++)
	{
		if(i == 8 || i == 13 || i == 18 || i == 23)
		{
			if(pText[i] != '-')
				return false;
			continue;
		}
		const int Hi = HexValue(pText[i]);
		const int Lo = HexValue(pText[i + 1]);
		if(Hi < 0 || Lo < 0)
			return false;
		aUuid[Out++] = (uint8_t)(Hi * 16 + Lo);
		i++;
	}
	return Out == UUID_SIZE;
}

size_t BuildChallengeMessage(uint8_t *pOut, size_t OutSize, const char *pServerAddress, const uint8_t *pNonce, size_t NonceSize, const uint8_t (&aDeviceId)[UUID_SIZE], const uint8_t (&aSessionId)[UUID_SIZE])
{
	const size_t PrefixLength = str_length(CHALLENGE_PREFIX);
	const size_t AddressLength = str_length(pServerAddress);
	if(AddressLength == 0 || AddressLength > 255 || NonceSize != NONCE_SIZE)
		return 0;
	const size_t Total = PrefixLength + 1 + 1 + AddressLength + NONCE_SIZE + UUID_SIZE + UUID_SIZE;
	if(Total > OutSize)
		return 0;
	size_t Pos = 0;
	mem_copy(pOut + Pos, CHALLENGE_PREFIX, PrefixLength);
	Pos += PrefixLength;
	pOut[Pos++] = 0;
	pOut[Pos++] = (uint8_t)AddressLength;
	mem_copy(pOut + Pos, pServerAddress, AddressLength);
	Pos += AddressLength;
	mem_copy(pOut + Pos, pNonce, NONCE_SIZE);
	Pos += NONCE_SIZE;
	mem_copy(pOut + Pos, aDeviceId, UUID_SIZE);
	Pos += UUID_SIZE;
	mem_copy(pOut + Pos, aSessionId, UUID_SIZE);
	return Total;
}

enum
{
	MAX_MESSAGE = 512,
};

bool SignChallenge(const uint8_t (&aSeed)[SEED_SIZE], const char *pServerAddress, const uint8_t *pNonce, size_t NonceSize, const char *pDeviceId, const uint8_t (&aSessionId)[UUID_SIZE], uint8_t (&aSignature)[SIGNATURE_SIZE])
{
	uint8_t aDevice[UUID_SIZE];
	if(!ParseUuid(pDeviceId, aDevice))
		return false;
	uint8_t aMessage[MAX_MESSAGE];
	const size_t Length = BuildChallengeMessage(aMessage, sizeof(aMessage), pServerAddress, pNonce, NonceSize, aDevice, aSessionId);
	if(Length == 0)
		return false;
	uint8_t aSeedCopy[SEED_SIZE];
	uint8_t aSecret[64];
	uint8_t aPublic[PUBLIC_KEY_SIZE];
	mem_copy(aSeedCopy, aSeed, sizeof(aSeedCopy));
	crypto_ed25519_key_pair(aSecret, aPublic, aSeedCopy);
	crypto_ed25519_sign(aSignature, aSecret, aMessage, Length);
	Wipe(aSecret, sizeof(aSecret));
	Wipe(aSeedCopy, sizeof(aSeedCopy));
	return true;
}

bool VerifyChallenge(const uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE], const char *pServerAddress, const uint8_t *pNonce, size_t NonceSize, const char *pDeviceId, const uint8_t (&aSessionId)[UUID_SIZE], const uint8_t (&aSignature)[SIGNATURE_SIZE])
{
	uint8_t aDevice[UUID_SIZE];
	if(!ParseUuid(pDeviceId, aDevice))
		return false;
	uint8_t aMessage[MAX_MESSAGE];
	const size_t Length = BuildChallengeMessage(aMessage, sizeof(aMessage), pServerAddress, pNonce, NonceSize, aDevice, aSessionId);
	return Length != 0 && crypto_ed25519_check(aSignature, aPublicKey, aMessage, Length) == 0;
}

size_t BuildRegisterMessage(uint8_t *pOut, size_t OutSize, const char *pDeviceName)
{
	const size_t PrefixLength = str_length(REGISTER_PREFIX) + 1; // includes the 0 byte
	const size_t NameLength = str_length(pDeviceName);
	if(NameLength == 0 || PrefixLength + NameLength > OutSize)
		return 0;
	mem_copy(pOut, REGISTER_PREFIX, PrefixLength);
	mem_copy(pOut + PrefixLength, pDeviceName, NameLength);
	return PrefixLength + NameLength;
}

enum
{
	MAX_REGISTER_MESSAGE = 16 + 4 * MAX_DEVICE_NAME + 64,
};

bool SignRegistration(const uint8_t (&aSeed)[SEED_SIZE], const char *pDeviceName, char (&aProof)[PROOF_LENGTH + 1])
{
	uint8_t aMessage[MAX_REGISTER_MESSAGE];
	const size_t Length = BuildRegisterMessage(aMessage, sizeof(aMessage), pDeviceName);
	if(Length == 0)
		return false;
	uint8_t aSeedCopy[SEED_SIZE];
	uint8_t aSecret[64];
	uint8_t aPublic[PUBLIC_KEY_SIZE];
	uint8_t aSignature[SIGNATURE_SIZE];
	mem_copy(aSeedCopy, aSeed, sizeof(aSeedCopy));
	crypto_ed25519_key_pair(aSecret, aPublic, aSeedCopy);
	crypto_ed25519_sign(aSignature, aSecret, aMessage, Length);
	Wipe(aSecret, sizeof(aSecret));
	Wipe(aSeedCopy, sizeof(aSeedCopy));
	Base64UrlEncode(aProof, sizeof(aProof), aSignature, sizeof(aSignature));
	return true;
}

bool VerifyRegistration(const uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE], const char *pDeviceName, const char *pProof)
{
	uint8_t aMessage[MAX_REGISTER_MESSAGE];
	const size_t Length = BuildRegisterMessage(aMessage, sizeof(aMessage), pDeviceName);
	uint8_t aSignature[SIGNATURE_SIZE];
	if(Length == 0 || str_length(pProof) != PROOF_LENGTH || Base64UrlDecode(aSignature, sizeof(aSignature), pProof) != (int)SIGNATURE_SIZE)
		return false;
	return crypto_ed25519_check(aSignature, aPublicKey, aMessage, Length) == 0;
}

void Fingerprint(const uint8_t (&aPublicKey)[PUBLIC_KEY_SIZE], char (&aFingerprint)[FINGERPRINT_LENGTH + 1])
{
	char aHex[SHA256_MAXSTRSIZE];
	sha256_str(sha256(aPublicKey, sizeof(aPublicKey)), aHex, sizeof(aHex));
	mem_copy(aFingerprint, aHex, FINGERPRINT_LENGTH);
	aFingerprint[FINGERPRINT_LENGTH] = '\0';
}

ERegisterError ClassifyRegisterAnswer(int Status, const char *pErrorWord)
{
	if(Status == 201)
		return ERegisterError::NONE;
	if(Status == 409 && str_comp(pErrorWord, "device_limit") == 0)
		return ERegisterError::DEVICE_LIMIT;
	if(Status == 409 && str_comp(pErrorWord, "key_registered") == 0)
		return ERegisterError::KEY_REGISTERED;
	if(Status == 400 && str_comp(pErrorWord, "invalid_proof") == 0)
		return ERegisterError::INVALID_PROOF;
	if(Status == 429)
		return ERegisterError::RATE_LIMITED;
	if(Status == 401 || Status == 403)
		return ERegisterError::UNAUTHORIZED;
	return ERegisterError::OTHER;
}

int64_t RetryDelayMs(int Failures, ERegisterError Error)
{
	if(Failures <= 0)
		return 0;
	const int Doublings = Failures - 1 < 6 ? Failures - 1 : 6;
	int64_t Delay = int64_t(5000) << Doublings;
	if(Error == ERegisterError::RATE_LIMITED && Delay < 60 * 1000)
		Delay = 60 * 1000;
	return Delay < MAX_RETRY_MS ? Delay : MAX_RETRY_MS;
}

// Origin of an https/http URL in lower case, with the default port made explicit. False for anything odd.
static bool ParseOrigin(const char *pUrl, size_t UrlLength, char (&aScheme)[8], char (&aHost)[256], int *pPort, bool *pHasPath)
{
	const char *pRest;
	if(str_startswith(pUrl, "https://"))
	{
		str_copy(aScheme, "https");
		pRest = pUrl + 8;
		*pPort = 443;
	}
	else if(str_startswith(pUrl, "http://"))
	{
		str_copy(aScheme, "http");
		pRest = pUrl + 7;
		*pPort = 80;
	}
	else
		return false;
	const char *pEnd = pUrl + UrlLength;
	const char *pAuthorityEnd = pRest;
	while(pAuthorityEnd < pEnd && *pAuthorityEnd != '/' && *pAuthorityEnd != '?' && *pAuthorityEnd != '#')
		pAuthorityEnd++;
	*pHasPath = pAuthorityEnd < pEnd;
	for(const char *p = pUrl; p < pEnd; p++)
	{
		if((unsigned char)*p <= ' ' || (unsigned char)*p >= 0x7f || *p == '\\')
			return false;
	}
	for(const char *p = pRest; p < pAuthorityEnd; p++)
	{
		if(*p == '@')
			return false;
	}
	const char *pHostEnd = pAuthorityEnd;
	const char *pColon = nullptr;
	if(pRest < pAuthorityEnd && *pRest == '[')
	{
		const char *pClose = static_cast<const char *>(memchr(pRest, ']', pAuthorityEnd - pRest));
		if(!pClose)
			return false;
		pHostEnd = pClose + 1;
		if(pHostEnd < pAuthorityEnd)
		{
			if(*pHostEnd != ':')
				return false;
			pColon = pHostEnd;
		}
	}
	else
	{
		for(const char *p = pRest; p < pAuthorityEnd; p++)
		{
			if(*p == ':')
			{
				pColon = p;
				pHostEnd = p;
				break;
			}
		}
	}
	if(pHostEnd == pRest || (size_t)(pHostEnd - pRest) >= sizeof(aHost))
		return false;
	for(size_t i = 0; i < (size_t)(pHostEnd - pRest); i++)
	{
		const char c = pRest[i];
		aHost[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
	}
	aHost[pHostEnd - pRest] = '\0';
	if(pColon)
	{
		const char *pDigits = pColon + 1;
		if(pDigits == pAuthorityEnd || pAuthorityEnd - pDigits > 5)
			return false;
		int Port = 0;
		for(const char *p = pDigits; p < pAuthorityEnd; p++)
		{
			if(*p < '0' || *p > '9')
				return false;
			Port = Port * 10 + (*p - '0');
		}
		if(Port < 1 || Port > 65535)
			return false;
		*pPort = Port;
	}
	return true;
}

static bool OriginAllowedByList(const char *pScheme, const char *pHost, int Port, const char *pList)
{
	const char *p = pList;
	while(*p)
	{
		while(*p == ' ' || *p == ',')
			p++;
		const char *pEnd = p;
		while(*pEnd && *pEnd != ' ' && *pEnd != ',')
			pEnd++;
		if(pEnd > p)
		{
			char aScheme[8], aHost[256];
			int ListPort;
			bool HasPath;
			if(ParseOrigin(p, pEnd - p, aScheme, aHost, &ListPort, &HasPath) && !HasPath && str_comp(aScheme, "https") == 0 &&
				str_comp(aScheme, pScheme) == 0 && str_comp(aHost, pHost) == 0 && ListPort == Port)
				return true;
		}
		p = pEnd;
	}
	return false;
}

bool EndpointAllowed(const char *pBaseUrl, const char *pEndpoint, const char *pExtraOrigins)
{
	char aBaseScheme[8], aBaseHost[256], aScheme[8], aHost[256];
	int BasePort, Port;
	bool HasPath;
	if(!ParseOrigin(pBaseUrl, str_length(pBaseUrl), aBaseScheme, aBaseHost, &BasePort, &HasPath) ||
		!ParseOrigin(pEndpoint, str_length(pEndpoint), aScheme, aHost, &Port, &HasPath))
		return false;
	if(str_comp(aBaseScheme, aScheme) == 0 && str_comp(aBaseHost, aHost) == 0 && BasePort == Port)
		return true;
	if(str_comp(aScheme, "https") != 0)
		return false;
	const size_t HostLength = str_length(aHost);
	static const char SUFFIX[] = ".kog.tw";
	if(Port == 443 && HostLength > sizeof(SUFFIX) - 1 && str_comp(aHost + HostLength - (sizeof(SUFFIX) - 1), SUFFIX) == 0)
		return true;
	return OriginAllowedByList(aScheme, aHost, Port, pExtraOrigins);
}

bool EncodeKey(const SDeviceKey &Key, char *pBuf, size_t Size)
{
	char aSeed[64];
	Base64UrlEncode(aSeed, sizeof(aSeed), Key.m_aSeed, sizeof(Key.m_aSeed));
	const int Length = str_format(pBuf, Size, "v1\n%s\n%s", Key.m_aId, aSeed);
	Wipe(aSeed, sizeof(aSeed));
	return Length > 0 && (size_t)Length < Size;
}

bool DecodeKey(const char *pBlob, SDeviceKey *pKey)
{
	const char *pId = str_startswith(pBlob, "v1\n");
	if(!pId)
		return false;
	const char *pNewline = strchr(pId, '\n');
	if(!pNewline || pNewline == pId || (size_t)(pNewline - pId) >= sizeof(pKey->m_aId))
		return false;
	uint8_t aSeed[SEED_SIZE + 1];
	const int Bytes = Base64UrlDecode(aSeed, sizeof(aSeed), pNewline + 1);
	if(Bytes != SEED_SIZE)
	{
		Wipe(aSeed, sizeof(aSeed));
		return false;
	}
	str_truncate(pKey->m_aId, sizeof(pKey->m_aId), pId, pNewline - pId);
	mem_copy(pKey->m_aSeed, aSeed, SEED_SIZE);
	Wipe(aSeed, sizeof(aSeed));
	return true;
}

bool MayAnswerChallenge(const SJoinDecision &Decision)
{
	if(Decision.m_CertBad || (Decision.m_CertRequired && !Decision.m_CertVerified))
		return false;
	return Decision.m_SwitchOn && Decision.m_AutoLoginOn && Decision.m_HasDeviceKey && Decision.m_ServerIsKog && Decision.m_CapabilityNegotiated && !Decision.m_AlreadyLoggedIn && !Decision.m_AlreadyAnsweredThisJoin;
}

bool CanonicalAddress(const NETADDR &Address, char *pBuf, size_t Size)
{
	pBuf[0] = '\0';
	NETADDR Copy = Address;
	if((Copy.type & (NETTYPE_IPV6 | NETTYPE_WEBSOCKET_IPV6)) != 0)
	{
		static const uint8_t s_aMappedPrefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
		if(mem_comp(Copy.ip, s_aMappedPrefix, sizeof(s_aMappedPrefix)) == 0)
		{
			uint8_t aV4[4];
			mem_copy(aV4, Copy.ip + 12, sizeof(aV4));
			mem_zero(Copy.ip, sizeof(Copy.ip));
			mem_copy(Copy.ip, aV4, sizeof(aV4));
			Copy.type = NETTYPE_IPV4;
		}
	}
	else if((Copy.type & (NETTYPE_IPV4 | NETTYPE_WEBSOCKET_IPV4)) == 0)
	{
		return false;
	}
	if(Copy.port == 0)
		return false;
	char aBuf[NETADDR_MAXSTRSIZE];
	net_addr_str(&Copy, aBuf, sizeof(aBuf), true);
	if(str_length(aBuf) >= (int)Size)
		return false;
	str_copy(pBuf, aBuf, (int)Size);
	return true;
}

static const char PAGE_OK[] = "<!doctype html><html><head><meta charset=\"utf-8\"><title>KoG Client</title></head><body style=\"font-family:sans-serif;text-align:center;margin-top:20vh\"><h1>You can close this tab</h1><p>Go back to the game to finish.</p></body></html>";
static const char PAGE_NO[] = "<!doctype html><html><head><meta charset=\"utf-8\"><title>KoG Client</title></head><body style=\"font-family:sans-serif\"><p>Not found.</p></body></html>";

static bool TryListen(const char *pHost, int Port, NETSOCKET *pSocket)
{
	char aAddr[64];
	str_format(aAddr, sizeof(aAddr), "%s:%d", pHost, Port);
	NETADDR Addr;
	if(net_addr_from_str(&Addr, aAddr) != 0)
		return false;
	NETSOCKET Sock = net_tcp_create(Addr);
	if(!Sock)
		return false;
	if(net_tcp_listen(Sock, 4) != 0)
	{
		net_tcp_close(Sock);
		return false;
	}
	net_set_non_blocking(Sock);
	*pSocket = Sock;
	return true;
}

static int64_t NowTicks()
{
	return time_get_impl();
}

bool CLoopbackListener::Start(const char *pState, int64_t TimeoutMs)
{
	Stop();
	str_copy(m_aState, pState);
	m_aCode[0] = '\0';
	m_aError[0] = '\0';
	for(int Attempt = 0; Attempt < 20 && !m_Listen; Attempt++)
	{
		const int Port = 49152 + secure_rand_below(65535 - 49152);
		if(TryListen("127.0.0.1", Port, &m_Listen))
		{
			m_Port = Port;
			m_Ipv6 = false;
		}
	}
	for(int Attempt = 0; Attempt < 20 && !m_Listen; Attempt++)
	{
		const int Port = 49152 + secure_rand_below(65535 - 49152);
		if(TryListen("[::1]", Port, &m_Listen))
		{
			m_Port = Port;
			m_Ipv6 = true;
		}
	}
	if(!m_Listen)
	{
		m_State = EState::CLOSED;
		return false;
	}
	m_Deadline = NowTicks() + time_freq() * TimeoutMs / 1000;
	m_State = EState::LISTENING;
	return true;
}

void CLoopbackListener::RedirectUri(char *pBuf, size_t Size) const
{
	str_format(pBuf, Size, "http://%s:%d%s", m_Ipv6 ? "[::1]" : "127.0.0.1", m_Port, CALLBACK_PATH);
}

void CLoopbackListener::CloseConn(SConn &Conn)
{
	if(Conn.m_Socket)
		net_tcp_close(Conn.m_Socket);
	Conn.m_Socket = nullptr;
	Conn.m_Length = 0;
}

void CLoopbackListener::Stop()
{
	for(SConn &Conn : m_aConns)
		CloseConn(Conn);
	if(m_Listen)
		net_tcp_close(m_Listen);
	m_Listen = nullptr;
	Wipe(m_aState, sizeof(m_aState));
	if(m_State == EState::LISTENING)
		m_State = EState::CLOSED;
}

void CLoopbackListener::Answer(SConn &Conn, const char *pStatus, const char *pBody)
{
	char aHead[256];
	const int BodyLength = str_length(pBody);
	const int HeadLength = str_format(aHead, sizeof(aHead), "HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\n\r\n", pStatus, BodyLength);
	net_tcp_send(Conn.m_Socket, aHead, HeadLength);
	net_tcp_send(Conn.m_Socket, pBody, BodyLength);
}

void CLoopbackListener::HandleRequest(SConn &Conn)
{
	char aError[128];
	char aCode[MAX_CODE_LENGTH + 1];
	const ECallback Result = ParseCallback(Conn.m_aRequest, Conn.m_Length, m_aState, aCode, sizeof(aCode), aError, sizeof(aError));
	switch(Result)
	{
	case ECallback::CODE:
		str_copy(m_aCode, aCode);
		Answer(Conn, "200 OK", PAGE_OK);
		m_State = EState::GOT_CODE;
		break;
	case ECallback::SERVER_ERROR:
		str_copy(m_aError, aError);
		Answer(Conn, "200 OK", PAGE_OK);
		m_State = EState::SERVER_ERROR;
		break;
	case ECallback::BAD_STATE:
	case ECallback::MISSING_CODE:
	case ECallback::MALFORMED:
		Answer(Conn, "400 Bad Request", PAGE_NO);
		break;
	case ECallback::WRONG_METHOD:
		Answer(Conn, "405 Method Not Allowed", PAGE_NO);
		break;
	case ECallback::WRONG_PATH:
		Answer(Conn, "404 Not Found", PAGE_NO);
		break;
	}
	Wipe(aCode, sizeof(aCode));
	Wipe(Conn.m_aRequest, Conn.m_Length);
	CloseConn(Conn);
	if(m_State == EState::GOT_CODE || m_State == EState::SERVER_ERROR)
	{
		// the first matching request is the only one
		for(SConn &Other : m_aConns)
			CloseConn(Other);
		net_tcp_close(m_Listen);
		m_Listen = nullptr;
		Wipe(m_aState, sizeof(m_aState));
	}
}

CLoopbackListener::EState CLoopbackListener::Poll()
{
	if(m_State != EState::LISTENING)
		return m_State;

	const int64_t Now = NowTicks();
	if(Now > m_Deadline)
	{
		Stop();
		m_State = EState::TIMED_OUT;
		return m_State;
	}

	for(int i = 0; i < MAX_CONNECTIONS; i++)
	{
		SConn *pFree = nullptr;
		for(SConn &Conn : m_aConns)
		{
			if(!Conn.m_Socket)
			{
				pFree = &Conn;
				break;
			}
		}
		if(!pFree)
			break;
		NETSOCKET New = nullptr;
		NETADDR From;
		if(net_tcp_accept(m_Listen, &New, &From) < 0 || !New)
			break;
		pFree->m_Socket = New;
		net_set_non_blocking(New);
		pFree->m_Length = 0;
		pFree->m_Deadline = Now + time_freq() * 5;
	}

	for(SConn &Conn : m_aConns)
	{
		if(!Conn.m_Socket)
			continue;
		if(Now > Conn.m_Deadline)
		{
			CloseConn(Conn);
			continue;
		}
		while(Conn.m_Socket && net_socket_read_wait(Conn.m_Socket, std::chrono::nanoseconds(0)) > 0)
		{
			const int Bytes = net_tcp_recv(Conn.m_Socket, Conn.m_aRequest + Conn.m_Length, (int)(sizeof(Conn.m_aRequest) - Conn.m_Length));
			if(Bytes <= 0)
			{
				CloseConn(Conn);
				break;
			}
			Conn.m_Length += Bytes;
			if(memchr(Conn.m_aRequest, '\n', Conn.m_Length) || Conn.m_Length >= sizeof(Conn.m_aRequest))
			{
				HandleRequest(Conn);
				break;
			}
		}
		if(m_State != EState::LISTENING)
			break;
	}
	return m_State;
}

} // namespace KogLogin
