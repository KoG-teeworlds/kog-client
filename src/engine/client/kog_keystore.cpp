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
#include <cstdio>
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
	CFMutableDataRef pData = CFDataCreateMutable(nullptr, 0);
	CFDataAppendBytes(pData, reinterpret_cast<const UInt8 *>(pSecret), str_length(pSecret));
	CFMutableDictionaryRef pUpdate = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(pUpdate, kSecValueData, pData);
	// Never in a backup or on another device, and only while the Mac is unlocked
	CFDictionarySetValue(pUpdate, kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
	OSStatus Status = SecItemUpdate(pQuery, pUpdate);
	if(Status == errSecItemNotFound)
	{
		CFDictionarySetValue(pQuery, kSecValueData, pData);
		CFDictionarySetValue(pQuery, kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
		Status = SecItemAdd(pQuery, nullptr);
	}
	KogLogin::Wipe(CFDataGetMutableBytePtr(pData), CFDataGetLength(pData));
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
	// SecItemCopyMatching hands out a private copy, so overwriting it does not touch the keychain item
	KogLogin::Wipe(const_cast<UInt8 *>(CFDataGetBytePtr(pData)), Length);
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
	// Not CRED_PERSIST_ENTERPRISE: that one roams with the domain profile. LOCAL_MACHINE keeps it on this PC across
	// logons; SESSION would lose the login at every sign-out. The credential is readable by this Windows user only.
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
	// A new file next to the target, so a link or file planted at the final name is replaced, never followed
	char aTmp[IO_MAX_PATH_LENGTH];
	str_format(aTmp, sizeof(aTmp), "%s.tmp", aPath);
	unlink(aTmp);
#if defined(O_NOFOLLOW)
	const int NoFollow = O_NOFOLLOW;
#else
	const int NoFollow = 0;
#endif
	const int Fd = open(aTmp, O_WRONLY | O_CREAT | O_EXCL | NoFollow | O_CLOEXEC, 0600);
	if(Fd < 0)
		return false;
	const char *pData = pSecret;
	size_t Left = str_length(pSecret);
	bool Ok = true;
	while(Left > 0 && Ok)
	{
		const ssize_t Written = write(Fd, pData, Left);
		if(Written <= 0)
			Ok = false;
		else
		{
			pData += Written;
			Left -= Written;
		}
	}
	Ok = Ok && fsync(Fd) == 0;
	Ok = close(Fd) == 0 && Ok;
	if(Ok)
		Ok = rename(aTmp, aPath) == 0;
	if(!Ok)
		unlink(aTmp);
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
