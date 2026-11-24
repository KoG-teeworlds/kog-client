#ifndef ENGINE_CLIENT_KOG_KEYSTORE_H
#define ENGINE_CLIENT_KOG_KEYSTORE_H

#include <cstddef>

class IStorage;

// Holds the one secret of this device (the private device key) outside of the config:
// macOS Keychain, Windows Credential Manager, and on every other platform a file with mode 0600 in the save directory.
namespace KogKeyStore {

enum class EBackend
{
	KEYCHAIN,
	CREDENTIAL_MANAGER,
	FILE,
};

EBackend Backend();
bool Save(IStorage *pStorage, const char *pSecret);
// False if there is none
bool Load(IStorage *pStorage, char *pBuf, size_t Size);
bool Remove(IStorage *pStorage);

} // namespace KogKeyStore

#endif
