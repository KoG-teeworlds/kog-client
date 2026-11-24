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
#else
#include <base/io.h>
#endif

#if defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_MACOS)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace KogKeyStore {

#if defined(CONF_PLATFORM_MACOS)

static const char SERVICE[] = "KoG Client device key";
static const char ACCOUNT[] = "device";

EBackend Backend()
{
	return EBackend::KEYCHAIN;
}

static CFMutableDictionaryRef Query()
{
	CFMutableDictionaryRef pQuery = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFStringRef pService = CFStringCreateWithCString(nullptr, SERVICE, kCFStringEncodingUTF8);
	CFStringRef pAccount = CFStringCreateWithCString(nullptr, ACCOUNT, kCFStringEncodingUTF8);
	CFDictionarySetValue(pQuery, kSecClass, kSecClassGenericPassword);
	CFDictionarySetValue(pQuery, kSecAttrService, pService);
	CFDictionarySetValue(pQuery, kSecAttrAccount, pAccount);
	CFRelease(pService);
	CFRelease(pAccount);
	return pQuery;
}

bool Save(IStorage *pStorage, const char *pSecret)
{
	CFMutableDictionaryRef pQuery = Query();
	CFDataRef pData = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(pSecret), str_length(pSecret));
	CFMutableDictionaryRef pUpdate = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(pUpdate, kSecValueData, pData);
	OSStatus Status = SecItemUpdate(pQuery, pUpdate);
	if(Status == errSecItemNotFound)
	{
		CFDictionarySetValue(pQuery, kSecValueData, pData);
		Status = SecItemAdd(pQuery, nullptr);
	}
	CFRelease(pUpdate);
	CFRelease(pData);
	CFRelease(pQuery);
	return Status == errSecSuccess;
}

bool Load(IStorage *pStorage, char *pBuf, size_t Size)
{
	CFMutableDictionaryRef pQuery = Query();
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
	CFRelease(pResult);
	return Fits;
}

bool Remove(IStorage *pStorage)
{
	CFMutableDictionaryRef pQuery = Query();
	const OSStatus Status = SecItemDelete(pQuery);
	CFRelease(pQuery);
	return Status == errSecSuccess || Status == errSecItemNotFound;
}

#elif defined(CONF_FAMILY_WINDOWS)

static const wchar_t TARGET[] = L"KoG Client device key";

EBackend Backend()
{
	return EBackend::CREDENTIAL_MANAGER;
}

bool Save(IStorage *pStorage, const char *pSecret)
{
	CREDENTIALW Credential = {};
	Credential.Type = CRED_TYPE_GENERIC;
	Credential.TargetName = const_cast<LPWSTR>(TARGET);
	Credential.CredentialBlobSize = (DWORD)str_length(pSecret);
	Credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(pSecret));
	Credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	Credential.UserName = const_cast<LPWSTR>(L"device");
	return CredWriteW(&Credential, 0) == TRUE;
}

bool Load(IStorage *pStorage, char *pBuf, size_t Size)
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

bool Remove(IStorage *pStorage)
{
	return CredDeleteW(TARGET, CRED_TYPE_GENERIC, 0) == TRUE || GetLastError() == ERROR_NOT_FOUND;
}

#else

static const char FILENAME[] = "kog_device.key";

EBackend Backend()
{
	return EBackend::FILE;
}

bool Save(IStorage *pStorage, const char *pSecret)
{
	char aPath[IO_MAX_PATH_LENGTH];
	pStorage->GetCompletePath(IStorage::TYPE_SAVE, FILENAME, aPath, sizeof(aPath));
#if defined(CONF_FAMILY_UNIX)
	const int Fd = open(aPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if(Fd < 0)
		return false;
	fchmod(Fd, 0600);
	const size_t Length = str_length(pSecret);
	const bool Ok = write(Fd, pSecret, Length) == (ssize_t)Length;
	close(Fd);
	return Ok;
#else
	IOHANDLE File = pStorage->OpenFile(FILENAME, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	const size_t Length = str_length(pSecret);
	const bool Ok = io_write(File, pSecret, Length) == Length;
	io_close(File);
	return Ok;
#endif
}

bool Load(IStorage *pStorage, char *pBuf, size_t Size)
{
	IOHANDLE File = pStorage->OpenFile(FILENAME, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	const size_t Length = io_read(File, pBuf, Size - 1);
	io_close(File);
	pBuf[Length] = '\0';
	return Length > 0;
}

bool Remove(IStorage *pStorage)
{
	if(!pStorage->FileExists(FILENAME, IStorage::TYPE_SAVE))
		return true;
	return pStorage->RemoveFile(FILENAME, IStorage::TYPE_SAVE);
}

#endif

} // namespace KogKeyStore
