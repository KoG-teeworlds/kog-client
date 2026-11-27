#include "kog_keyfile.h"

#include "kog_login.h"

#include <base/detect.h>
#include <base/fs.h>
#include <base/io.h>
#include <base/mem.h>
#include <base/secure.h>
#include <base/str.h>

#include <engine/external/monocypher/monocypher.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

#if defined(CONF_FAMILY_UNIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace KogKeyFile {

namespace {

constexpr char MAGIC[4] = {'K', 'O', 'G', 'K'};
constexpr uint8_t VERSION = 1;
constexpr uint8_t KDF_ARGON2ID = 1;
constexpr size_t SALT_SIZE = 16;
constexpr size_t NONCE_SIZE = 24;
constexpr size_t MAC_SIZE = 16;
constexpr size_t SALT_OFFSET = 16;
constexpr size_t NONCE_OFFSET = SALT_OFFSET + SALT_SIZE;
constexpr size_t HEADER_SIZE = NONCE_OFFSET + NONCE_SIZE;

// 64 MiB and 3 passes: about a quarter of a second on a desktop. A file may ask for no more than the limits below, so
// a changed file cannot make the start-up wait or run out of memory.
constexpr uint32_t WRITE_BLOCKS = 64 * 1024;
constexpr uint32_t WRITE_PASSES = 3;
constexpr uint32_t MIN_BLOCKS = 8 * 1024;
constexpr uint32_t MAX_BLOCKS = 256 * 1024;
constexpr uint32_t MAX_PASSES = 8;

void PutU32(uint8_t *pOut, uint32_t Value)
{
	for(int i = 0; i < 4; i++)
		pOut[i] = (Value >> (8 * i)) & 0xff;
}

uint32_t GetU32(const uint8_t *pIn)
{
	uint32_t Value = 0;
	for(int i = 0; i < 4; i++)
		Value |= (uint32_t)pIn[i] << (8 * i);
	return Value;
}

bool Derive(uint8_t (&aKey)[32], const char *pPassphrase, const uint8_t *pSalt, uint32_t Blocks, uint32_t Passes)
{
	void *pWork = malloc((size_t)Blocks * 1024);
	if(!pWork)
		return false;
	const crypto_argon2_config Config = {CRYPTO_ARGON2_ID, Blocks, Passes, 1};
	const crypto_argon2_inputs Inputs = {(const uint8_t *)pPassphrase, pSalt, (uint32_t)str_length(pPassphrase), (uint32_t)SALT_SIZE};
	crypto_argon2(aKey, sizeof(aKey), pWork, Config, Inputs, crypto_argon2_no_extras);
	free(pWork);
	return true;
}

bool ReadAll(const char *pPath, std::vector<uint8_t> &Data, bool *pExists)
{
	IOHANDLE File = io_open(pPath, IOFLAG_READ);
	*pExists = File != nullptr;
	if(!File)
		return false;
	uint8_t aChunk[256];
	size_t Length;
	// A key file is far smaller than this
	while(Data.size() < 4096 && (Length = io_read(File, aChunk, sizeof(aChunk))) > 0)
		Data.insert(Data.end(), aChunk, aChunk + Length);
	io_close(File);
	return true;
}

bool IsLocked(const std::vector<uint8_t> &Data)
{
	return Data.size() >= sizeof(MAGIC) && mem_comp(Data.data(), MAGIC, sizeof(MAGIC)) == 0;
}

ELoad Open(const std::vector<uint8_t> &Data, const char *pPassphrase, char *pBlob, size_t Size)
{
	if(!IsLocked(Data))
	{
		if(Data.empty() || Data.size() >= MAX_BLOB)
			return ELoad::CORRUPT;
		if(pBlob)
		{
			if(Data.size() + 1 > Size)
				return ELoad::CORRUPT;
			mem_copy(pBlob, Data.data(), Data.size());
			pBlob[Data.size()] = '\0';
		}
		return ELoad::OK;
	}
	if(Data.size() < HEADER_SIZE + MAC_SIZE + 1 || Data.size() > HEADER_SIZE + MAC_SIZE + MAX_BLOB || Data[4] != VERSION || Data[5] != KDF_ARGON2ID)
		return ELoad::CORRUPT;
	const uint32_t Blocks = GetU32(&Data[8]);
	const uint32_t Passes = GetU32(&Data[12]);
	if(Blocks < MIN_BLOCKS || Blocks > MAX_BLOCKS || Passes < 1 || Passes > MAX_PASSES)
		return ELoad::CORRUPT;
	if(!pBlob || !pPassphrase || pPassphrase[0] == '\0')
		return ELoad::LOCKED;
	if(str_length(pPassphrase) > MAX_PASSPHRASE)
		return ELoad::WRONG_PASSPHRASE;

	const size_t TextSize = Data.size() - HEADER_SIZE - MAC_SIZE;
	if(TextSize + 1 > Size)
		return ELoad::CORRUPT;
	uint8_t aKey[32];
	if(!Derive(aKey, pPassphrase, &Data[SALT_OFFSET], Blocks, Passes))
		return ELoad::CORRUPT;
	const int Result = crypto_aead_unlock((uint8_t *)pBlob, &Data[HEADER_SIZE], aKey, &Data[NONCE_OFFSET], Data.data(), HEADER_SIZE, &Data[HEADER_SIZE + MAC_SIZE], TextSize);
	crypto_wipe(aKey, sizeof(aKey));
	if(Result != 0)
		return ELoad::WRONG_PASSPHRASE;
	pBlob[TextSize] = '\0';
	return ELoad::OK;
}

// A new file next to the target, so a link or file planted at the final name is replaced, never followed
bool WriteNew(const char *pPath, const uint8_t *pData, size_t Size)
{
#if defined(CONF_FAMILY_UNIX)
	(void)unlink(pPath);
#if defined(O_NOFOLLOW)
	const int NoFollow = O_NOFOLLOW;
#else
	const int NoFollow = 0;
#endif
	const int Fd = open(pPath, O_WRONLY | O_CREAT | O_EXCL | NoFollow | O_CLOEXEC, 0600);
	if(Fd < 0)
		return false;
	bool Ok = true;
	while(Size > 0 && Ok)
	{
		const ssize_t Written = write(Fd, pData, Size);
		if(Written <= 0)
			Ok = false;
		else
		{
			pData += Written;
			Size -= static_cast<size_t>(Written);
		}
	}
	Ok = Ok && fsync(Fd) == 0;
	return close(Fd) == 0 && Ok;
#else
	// The save folder is in the profile of this Windows user; the file takes over its access list
	(void)fs_remove(pPath);
	IOHANDLE File = io_open(pPath, IOFLAG_WRITE);
	if(!File)
		return false;
	bool Ok = io_write(File, pData, Size) == Size;
	Ok = io_sync(File) == 0 && Ok;
	return io_close(File) == 0 && Ok;
#endif
}

} // namespace

ELoad Load(const char *pPath, const char *pPassphrase, char *pBlob, size_t Size)
{
	std::vector<uint8_t> Data;
	bool Exists;
	const bool Read = ReadAll(pPath, Data, &Exists);
	if(!Exists)
		return ELoad::MISSING;
	const ELoad Result = Read ? Open(Data, pPassphrase, pBlob, Size) : ELoad::CORRUPT;
	if(Result != ELoad::OK && pBlob && Size > 0)
		KogLogin::Wipe(pBlob, Size);
	if(!Data.empty())
		KogLogin::Wipe(Data.data(), Data.size());
	return Result;
}

ESave Save(const char *pPath, const char *pBlob, const char *pNewPassphrase, const char *pCurrentPassphrase)
{
	const bool Lock = pNewPassphrase && pNewPassphrase[0] != '\0';
	if(Lock && (str_length(pNewPassphrase) < MIN_PASSPHRASE || str_length(pNewPassphrase) > MAX_PASSPHRASE))
		return ESave::PASSPHRASE_INVALID;
	const size_t BlobSize = static_cast<size_t>(str_length(pBlob));
	if(BlobSize == 0 || BlobSize >= MAX_BLOB)
		return ESave::FAILED;

	char aOld[MAX_BLOB];
	const ELoad Existing = Load(pPath, pCurrentPassphrase, aOld, sizeof(aOld));
	KogLogin::Wipe(aOld, sizeof(aOld));
	if(Existing == ELoad::LOCKED)
		return ESave::LOCKED;
	if(Existing == ELoad::WRONG_PASSPHRASE)
		return ESave::WRONG_PASSPHRASE;
	// A file that cannot be read may still be a key that something damaged: it goes only by Remove
	if(Existing == ELoad::CORRUPT)
		return ESave::FAILED;

	std::vector<uint8_t> Out;
	if(Lock)
	{
		Out.resize(HEADER_SIZE + MAC_SIZE + BlobSize);
		mem_copy(Out.data(), MAGIC, sizeof(MAGIC));
		Out[4] = VERSION;
		Out[5] = KDF_ARGON2ID;
		Out[6] = Out[7] = 0;
		PutU32(&Out[8], WRITE_BLOCKS);
		PutU32(&Out[12], WRITE_PASSES);
		secure_random_fill(&Out[SALT_OFFSET], SALT_SIZE + NONCE_SIZE);
		uint8_t aKey[32];
		if(!Derive(aKey, pNewPassphrase, &Out[SALT_OFFSET], WRITE_BLOCKS, WRITE_PASSES))
			return ESave::FAILED;
		crypto_aead_lock(&Out[HEADER_SIZE + MAC_SIZE], &Out[HEADER_SIZE], aKey, &Out[NONCE_OFFSET], Out.data(), HEADER_SIZE, (const uint8_t *)pBlob, BlobSize);
		crypto_wipe(aKey, sizeof(aKey));
	}
	else
		Out.assign(pBlob, pBlob + BlobSize);

	char aTmp[1024];
	str_format(aTmp, sizeof(aTmp), "%s.tmp", pPath);
	bool Ok = WriteNew(aTmp, Out.data(), Out.size());
	KogLogin::Wipe(Out.data(), Out.size());
	if(Ok)
		Ok = fs_rename(aTmp, pPath) == 0;
	if(!Ok)
		(void)fs_remove(aTmp);
	return Ok ? ESave::OK : ESave::FAILED;
}

bool Remove(const char *pPath)
{
	return !fs_is_file(pPath) || fs_remove(pPath) == 0;
}

bool Migrate(const char *pPath, FLegacyLoad pfnLoad, FLegacyRemove pfnRemove)
{
	if(!pfnLoad || fs_is_file(pPath))
		return false;
	char aBlob[MAX_BLOB];
	KogLogin::SDeviceKey Key;
	bool Moved = pfnLoad(aBlob, sizeof(aBlob)) && KogLogin::DecodeKey(aBlob, &Key) && Save(pPath, aBlob, nullptr, nullptr) == ESave::OK;
	if(Moved)
	{
		// The old copy goes only once the new one reads back
		char aBack[MAX_BLOB];
		Moved = Load(pPath, nullptr, aBack, sizeof(aBack)) == ELoad::OK && str_comp(aBack, aBlob) == 0;
		KogLogin::Wipe(aBack, sizeof(aBack));
		if(Moved && pfnRemove)
			(void)pfnRemove();
	}
	KogLogin::Wipe(aBlob, sizeof(aBlob));
	KogLogin::Wipe(&Key, sizeof(Key));
	return Moved;
}

} // namespace KogKeyFile
