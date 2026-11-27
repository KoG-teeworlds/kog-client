#ifndef ENGINE_SHARED_KOG_KEYFILE_H
#define ENGINE_SHARED_KOG_KEYFILE_H

#include <cstddef>

// The file that holds the device key, plain or locked with a passphrase. See docs/LOGIN.md.
// Plain: the blob of KogLogin::EncodeKey. Locked: "KOGK", version, KDF id, Argon2id memory and passes, salt, nonce,
// then the XChaCha20-Poly1305 tag and ciphertext of the blob; the header is authenticated.
namespace KogKeyFile {

enum
{
	MIN_PASSPHRASE = 8,
	MAX_PASSPHRASE = 128,
	MAX_BLOB = 256,
};

enum class ELoad
{
	OK,
	MISSING,
	LOCKED, // locked, and no passphrase was given
	WRONG_PASSPHRASE, // also a file that was changed: the two cannot be told apart
	CORRUPT,
};

enum class ESave
{
	OK,
	FAILED, // also: the file there cannot be read; Remove it first
	LOCKED, // the file there is locked and no passphrase was given: it is left alone
	WRONG_PASSPHRASE, // the same, for the wrong one
	PASSPHRASE_INVALID, // shorter than MIN_PASSPHRASE or longer than MAX_PASSPHRASE
};

// pPassphrase may be null or empty. pBlob may be null to only look at the file: a locked file is then LOCKED, without
// the cost of the key derivation.
ELoad Load(const char *pPath, const char *pPassphrase, char *pBlob, size_t Size);

// Writes pBlob, locked if pNewPassphrase is not empty. A file that is already there and locked is only replaced if
// pCurrentPassphrase opens it, so a wrong passphrase never costs the key. The file is written next to the target with
// mode 0600 where the system has modes, synced, then renamed over it.
ESave Save(const char *pPath, const char *pBlob, const char *pNewPassphrase, const char *pCurrentPassphrase);

bool Remove(const char *pPath);

// Takes a key out of an older store (the macOS Keychain, the Windows Credential Manager) into the file, then removes it
// there. Only if there is no file yet. True if a key was moved.
using FLegacyLoad = bool (*)(char *pBuf, size_t Size);
using FLegacyRemove = bool (*)();
bool Migrate(const char *pPath, FLegacyLoad pfnLoad, FLegacyRemove pfnRemove);

} // namespace KogKeyFile

#endif
