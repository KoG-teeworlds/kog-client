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
constexpr int64_t CLOCK_SKEW_SECONDS = 2ll * 24 * 3600;
constexpr int MAX_URLS = 3;
constexpr int MAX_CHANGES = 3;
constexpr size_t MAX_CHANGE_LENGTH = 120;

constexpr const char *CHANNEL_STABLE = "stable";
constexpr const char *CHANNEL_CANARY = "canary";
constexpr const char *CHANNEL_DEV = "dev";
constexpr const char *OWNER_NAMESPACE = "kog-client-release";

bool IsKnownChannel(const char *pChannel);
int64_t MaxValiditySeconds(const char *pChannel);

struct SPublicKey
{
	unsigned char m_aKey[32];
};

struct SOwnerKey
{
	unsigned char m_aKey[32];
	const char *m_pApplication;
};

// m_pCiKeys come from the infrastructure, m_pBackupCiKeys is the offline backup that only the owner holds
struct SChannelKeys
{
	const char *m_pChannel;
	const SPublicKey *m_pCiKeys;
	int m_NumCiKeys;
	const SOwnerKey *m_pOwnerKeys;
	int m_NumOwnerKeys;
	const SPublicKey *m_pBackupCiKeys = nullptr;
	int m_NumBackupCiKeys = 0;
};

struct SFile
{
	std::string m_Path;
	std::vector<std::string> m_vUrls;
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
	int m_RolloutPercent = 100;
	bool m_Halted = false;
	std::string m_MinVersion;
	std::string m_NotesUrl;
	std::vector<std::string> m_vChanges;
	bool m_HasClient = false;
	SFile m_Client;
	std::vector<SFile> m_Files;
	std::vector<std::string> m_Remove;

	int64_t TotalSize() const;
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
	HALTED,
	NOT_IN_ROLLOUT,
	KNOWN_BAD,
	NEEDS_CONFIRMATION,
};

enum class ESignatureResult
{
	OK,
	NO_KEYS,
	MALFORMED_CI,
	BAD_CI,
	MISSING_OWNER,
	BAD_OWNER,
};

// "kog-update-sig-v1 <8 hex: first 4 bytes of the public key> <128 hex: signature>"
bool ParseSignatureFile(const char *pText, size_t Length, unsigned char (&aSignature)[64], unsigned char (&aKeyId)[4]);
bool VerifySignature(const void *pData, size_t Size, const unsigned char (&aSignature)[64], const unsigned char (&aKeyId)[4], const SPublicKey *pKeys, int NumKeys);

// An OpenSSH signature (ssh-keygen -Y sign -n kog-client-release) of a security key, "sk-ssh-ed25519@openssh.com"
bool VerifyOwnerSignature(const void *pData, size_t Size, const char *pArmored, size_t Length, const SOwnerKey *pKeys, int NumKeys, bool AllowSoftwareKey);

// A channel with owner keys needs both signatures, one without needs the CI signature only
ESignatureResult VerifyManifestSignatures(const void *pManifest, size_t Size, const char *pCiText, size_t CiLength, const char *pOwnerText, size_t OwnerLength, const SChannelKeys *pKeys, bool AllowSoftwareOwnerKey = false);

struct SVersion
{
	int m_aParts[3] = {};
	int m_Kind = 2; // 0 dev, 1 canary, 2 stable
	int m_Number = 0;
};

bool ParseVersion(const char *pVersion, SVersion *pOut);
int CompareVersions(const SVersion &A, const SVersion &B);
int CompareVersions(const char *pA, const char *pB, bool *pOk);
const char *ChannelOfVersion(const char *pVersion);

bool ParseManifest(const char *pJson, size_t Length, const char *pPlatform, SManifest *pOut, char *pError, size_t ErrorSize);

struct SDecideInput
{
	const char *m_pChannel;
	const char *m_pCurrentVersion;
	int64_t m_LastSequence = 0;
	int64_t m_Now = 0;
	int m_Bucket = 0;
	bool m_SwitchConfirmed = false;
	const char *m_pBadVersion = "";
};

EVerdict Decide(const SManifest &Manifest, const SDecideInput &Input);
bool UpdateRequired(const SManifest &Manifest, const char *pCurrentVersion);
int Bucket(const char *pInstallId);

bool IsAllowedPath(const char *pPath);
bool IsAllowedUrl(const char *pUrl);
bool IsAllowedNotesUrl(const char *pUrl);
bool IsPlainText(const char *pText, size_t MaxLength);
}

#endif
