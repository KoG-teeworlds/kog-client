#ifndef ENGINE_SHARED_KOG_UPDATE_H
#define ENGINE_SHARED_KOG_UPDATE_H

#include <base/hash.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KogUpdate
{

	constexpr int MANIFEST_FORMAT = 2;
	constexpr size_t MAX_MANIFEST_SIZE = 4 * 1024 * 1024;
	constexpr size_t MAX_FILES = 4096;
	constexpr int64_t MAX_FILE_SIZE = 512ll * 1024 * 1024;
	constexpr int64_t MAX_VALIDITY_SECONDS = 90ll * 24 * 3600;
	constexpr int64_t CLOCK_SKEW_SECONDS = 2ll * 24 * 3600;

	struct SPublicKey
	{
		unsigned char m_aKey[32];
	};

	struct SFile
	{
		std::string m_Path;
		std::string m_Url;
		SHA256_DIGEST m_Sha256 = {};
		int64_t m_Size = 0;
	};

	struct SManifest
	{
		std::string m_Channel;
		std::string m_Version;
		int64_t m_Sequence = 0;
		int64_t m_Issued = 0;
		int64_t m_Expires = 0;
		bool m_HasClient = false;
		SFile m_Client;
		std::vector<SFile> m_Files;
		std::vector<std::string> m_Remove;
	};

	enum class EVerdict
	{
		APPLY,
		UP_TO_DATE,
		EXPIRED,
		NOT_YET_VALID,
		ROLLBACK,
		WRONG_CHANNEL,
		BAD_VERSION,
	};

	// "kog-update-sig-v1 <8 hex: first 4 bytes of the public key> <128 hex: signature>"
	bool ParseSignatureFile(const char *pText, size_t Length, unsigned char (&aSignature)[64], unsigned char (&aKeyId)[4]);
	bool VerifySignature(const void *pData, size_t Size, const unsigned char (&aSignature)[64], const unsigned char (&aKeyId)[4], const SPublicKey *pKeys, int NumKeys);

	bool ParseManifest(const char *pJson, size_t Length, const char *pPlatform, SManifest *pOut, char *pError, size_t ErrorSize);
	EVerdict Decide(const SManifest &Manifest, const char *pChannel, const char *pCurrentVersion, int64_t LastSequence, int64_t Now);

	int CompareVersions(const char *pA, const char *pB, bool *pOk);
	bool IsAllowedPath(const char *pPath);
	bool IsAllowedUrl(const char *pUrl);

}

#endif
