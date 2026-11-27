#include "kog_keystore.h"

#include <base/detect.h>
#include <base/str.h>

#include <engine/shared/kog_login.h>
#include <engine/storage.h>

#include <cstdint>
#include <cstring>

#if defined(CONF_PLATFORM_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <wincred.h>
#endif

namespace KogKeyStore {

namespace {

constexpr const char *FILENAME = "kog_device.key";

void Path(IStorage *pStorage, char *pBuf, size_t Size)
{
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, FILENAME, pBuf, Size);
}

#if defined(CONF_PLATFORM_MACOS)

CFMutableDictionaryRef LegacyQuery()
{
	CFMutableDictionaryRef pQuery = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFStringRef pService = CFStringCreateWithCString(nullptr, "KoG Client device key", kCFStringEncodingUTF8);
	CFStringRef pAccount = CFStringCreateWithCString(nullptr, "device", kCFStringEncodingUTF8);
	CFDictionarySetValue(pQuery, kSecClass, kSecClassGenericPassword);
	CFDictionarySetValue(pQuery, kSecAttrService, pService);
	CFDictionarySetValue(pQuery, kSecAttrAccount, pAccount);
	CFRelease(pService);
	CFRelease(pAccount);
	return pQuery;
}

bool LegacyLoad(char *pBuf, size_t Size)
{
	CFMutableDictionaryRef pQuery = LegacyQuery();
	CFDictionarySetValue(pQuery, kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(pQuery, kSecMatchLimit, kSecMatchLimitOne);
	CFTypeRef pResult = nullptr;
	const OSStatus Status = SecItemCopyMatching(pQuery, &pResult);
	CFRelease(pQuery);
	if(Status != errSecSuccess || !pResult)
		return false;
	CFDataRef pData = static_cast<CFDataRef>(pResult);
	const size_t Length = CFDataGetLength(pData);
	const bool Fits = Length + 1 <= Size;
	if(Fits)
	{
		CFDataGetBytes(pData, CFRangeMake(0, Length), reinterpret_cast<UInt8 *>(pBuf));
		pBuf[Length] = '\0';
	}
	// SecItemCopyMatching hands out a private copy, so overwriting it does not touch the keychain item
	KogLogin::Wipe(const_cast<UInt8 *>(CFDataGetBytePtr(pData)), Length);
	CFRelease(pResult);
	return Fits;
}

bool LegacyRemove()
{
	CFMutableDictionaryRef pQuery = LegacyQuery();
	const OSStatus Status = SecItemDelete(pQuery);
	CFRelease(pQuery);
	return Status == errSecSuccess || Status == errSecItemNotFound;
}

constexpr KogKeyFile::FLegacyLoad LEGACY_LOAD = LegacyLoad;
constexpr KogKeyFile::FLegacyRemove LEGACY_REMOVE = LegacyRemove;

#elif defined(CONF_FAMILY_WINDOWS)

const wchar_t TARGET[] = L"KoG Client device key";

bool LegacyLoad(char *pBuf, size_t Size)
{
	PCREDENTIALW pCredential = nullptr;
	if(!CredReadW(TARGET, CRED_TYPE_GENERIC, 0, &pCredential))
		return false;
	const size_t Length = pCredential->CredentialBlobSize;
	const bool Fits = Length + 1 <= Size;
	if(Fits)
	{
		memcpy(pBuf, pCredential->CredentialBlob, Length);
		pBuf[Length] = '\0';
	}
	KogLogin::Wipe(pCredential->CredentialBlob, Length);
	CredFree(pCredential);
	return Fits;
}

bool LegacyRemove()
{
	return CredDeleteW(TARGET, CRED_TYPE_GENERIC, 0) == TRUE || GetLastError() == ERROR_NOT_FOUND;
}

constexpr KogKeyFile::FLegacyLoad LEGACY_LOAD = LegacyLoad;
constexpr KogKeyFile::FLegacyRemove LEGACY_REMOVE = LegacyRemove;

#else

// Other systems always had the file
constexpr KogKeyFile::FLegacyLoad LEGACY_LOAD = nullptr;
constexpr KogKeyFile::FLegacyRemove LEGACY_REMOVE = nullptr;

#endif

} // namespace

KogKeyFile::ELoad Load(IStorage *pStorage, const char *pPassphrase, char *pBuf, size_t Size)
{
	char aPath[IO_MAX_PATH_LENGTH];
	Path(pStorage, aPath, sizeof(aPath));
	return KogKeyFile::Load(aPath, pPassphrase, pBuf, Size);
}

KogKeyFile::ESave Save(IStorage *pStorage, const char *pSecret, const char *pNewPassphrase, const char *pCurrentPassphrase)
{
	char aPath[IO_MAX_PATH_LENGTH];
	Path(pStorage, aPath, sizeof(aPath));
	return KogKeyFile::Save(aPath, pSecret, pNewPassphrase, pCurrentPassphrase);
}

bool Remove(IStorage *pStorage)
{
	char aPath[IO_MAX_PATH_LENGTH];
	Path(pStorage, aPath, sizeof(aPath));
	bool Ok = KogKeyFile::Remove(aPath);
	// A key that was never moved out of the old store would otherwise come back at the next start
	if(LEGACY_REMOVE)
		Ok = LEGACY_REMOVE() && Ok;
	return Ok;
}

bool MigrateLegacy(IStorage *pStorage)
{
	char aPath[IO_MAX_PATH_LENGTH];
	Path(pStorage, aPath, sizeof(aPath));
	return KogKeyFile::Migrate(aPath, LEGACY_LOAD, LEGACY_REMOVE);
}

} // namespace KogKeyStore
