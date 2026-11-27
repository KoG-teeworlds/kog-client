#ifndef ENGINE_CLIENT_KOG_KEYSTORE_H
#define ENGINE_CLIENT_KOG_KEYSTORE_H

#include <engine/shared/kog_keyfile.h>

#include <cstddef>

class IStorage;

// Holds the one secret of this device (the private device key) outside of the config: the file kog_device.key in the
// save folder of this client, the same on every system, optionally locked with a passphrase. See docs/LOGIN.md.
namespace KogKeyStore {

KogKeyFile::ELoad Load(IStorage *pStorage, const char *pPassphrase, char *pBuf, size_t Size);
KogKeyFile::ESave Save(IStorage *pStorage, const char *pSecret, const char *pNewPassphrase, const char *pCurrentPassphrase);
bool Remove(IStorage *pStorage);

// Moves a key that an earlier build put into the macOS Keychain or the Windows Credential Manager into the file. To be
// removed one release after this one.
bool MigrateLegacy(IStorage *pStorage);

} // namespace KogKeyStore

#endif
