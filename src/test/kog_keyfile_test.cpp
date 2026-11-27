#include "test.h"

#include <base/detect.h>
#include <base/fs.h>
#include <base/io.h>
#include <base/str.h>

#include <engine/shared/kog_keyfile.h>
#include <engine/shared/kog_login.h>

#include <gtest/gtest.h>

#if defined(CONF_FAMILY_UNIX)
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <string>
#include <vector>

using namespace KogKeyFile;

namespace {

const char BLOB[] = "v1\n0b9d6e7a-1111-4222-8333-444455556666\nAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
const char PASS[] = "correct horse battery";

class CKeyFileTest : public ::testing::Test
{
protected:
	CTestInfo m_Info;
	char m_aPath[IO_MAX_PATH_LENGTH];
	char m_aTmp[IO_MAX_PATH_LENGTH];

	void SetUp() override
	{
		m_Info.Filename(m_aPath, sizeof(m_aPath), ".key");
		str_format(m_aTmp, sizeof(m_aTmp), "%s.tmp", m_aPath);
		Cleanup();
	}
	void TearDown() override { Cleanup(); }
	void Cleanup()
	{
		(void)fs_remove(m_aPath);
		(void)fs_remove(m_aTmp);
	}
	std::vector<uint8_t> ReadFile()
	{
		std::vector<uint8_t> Data;
		IOHANDLE File = io_open(m_aPath, IOFLAG_READ);
		if(!File)
			return Data;
		uint8_t aBuf[512];
		size_t Length;
		while((Length = io_read(File, aBuf, sizeof(aBuf))) > 0)
			Data.insert(Data.end(), aBuf, aBuf + Length);
		io_close(File);
		return Data;
	}
	void WriteFile(const std::vector<uint8_t> &Data)
	{
		IOHANDLE File = io_open(m_aPath, IOFLAG_WRITE);
		ASSERT_TRUE(File);
		if(!Data.empty())
			io_write(File, Data.data(), Data.size());
		io_close(File);
	}
};

bool g_LegacyHasKey = false;
bool g_LegacyRemoved = false;

bool LegacyLoad(char *pBuf, size_t Size)
{
	if(!g_LegacyHasKey)
		return false;
	str_copy(pBuf, BLOB, Size);
	return true;
}

bool LegacyRemove()
{
	g_LegacyRemoved = true;
	g_LegacyHasKey = false;
	return true;
}

bool LegacyGarbage(char *pBuf, size_t Size)
{
	str_copy(pBuf, "not a key", Size);
	return true;
}

} // namespace

TEST_F(CKeyFileTest, PlainRoundTrip)
{
	char aBlob[MAX_BLOB];
	EXPECT_EQ(Load(m_aPath, nullptr, aBlob, sizeof(aBlob)), ELoad::MISSING);
	ASSERT_EQ(Save(m_aPath, BLOB, nullptr, nullptr), ESave::OK);
	ASSERT_EQ(Load(m_aPath, nullptr, aBlob, sizeof(aBlob)), ELoad::OK);
	EXPECT_STREQ(aBlob, BLOB);
	KogLogin::SDeviceKey Key;
	EXPECT_TRUE(KogLogin::DecodeKey(aBlob, &Key));
	EXPECT_FALSE(fs_is_file(m_aTmp));
}

#if defined(CONF_FAMILY_UNIX)
TEST_F(CKeyFileTest, OnlyTheOwnerCanReadTheFile)
{
	ASSERT_EQ(Save(m_aPath, BLOB, nullptr, nullptr), ESave::OK);
	struct stat Stat;
	ASSERT_EQ(stat(m_aPath, &Stat), 0);
	EXPECT_EQ(Stat.st_mode & 0777, 0600u);
	ASSERT_EQ(Save(m_aPath, BLOB, PASS, nullptr), ESave::OK);
	ASSERT_EQ(stat(m_aPath, &Stat), 0);
	EXPECT_EQ(Stat.st_mode & 0777, 0600u);
}

TEST_F(CKeyFileTest, ALinkAtTheTempNameIsNotFollowed)
{
	char aTarget[IO_MAX_PATH_LENGTH];
	m_Info.Filename(aTarget, sizeof(aTarget), ".target");
	(void)fs_remove(aTarget);
	ASSERT_EQ(symlink(aTarget, m_aTmp), 0);
	ASSERT_EQ(Save(m_aPath, BLOB, nullptr, nullptr), ESave::OK);
	EXPECT_FALSE(fs_is_file(aTarget));
}
#endif

TEST_F(CKeyFileTest, PassphraseRoundTrip)
{
	ASSERT_EQ(Save(m_aPath, BLOB, PASS, nullptr), ESave::OK);
	const std::vector<uint8_t> Data = ReadFile();
	const std::string Text(Data.begin(), Data.end());
	EXPECT_EQ(Text.find("0b9d6e7a"), std::string::npos);
	EXPECT_EQ(Text.find("v1\n"), std::string::npos);

	char aBlob[MAX_BLOB];
	EXPECT_EQ(Load(m_aPath, nullptr, aBlob, sizeof(aBlob)), ELoad::LOCKED);
	EXPECT_EQ(Load(m_aPath, "", aBlob, sizeof(aBlob)), ELoad::LOCKED);
	EXPECT_EQ(Load(m_aPath, PASS, nullptr, 0), ELoad::LOCKED);
	ASSERT_EQ(Load(m_aPath, PASS, aBlob, sizeof(aBlob)), ELoad::OK);
	EXPECT_STREQ(aBlob, BLOB);
}

TEST_F(CKeyFileTest, TwoSavesDoNotShareSaltOrNonce)
{
	ASSERT_EQ(Save(m_aPath, BLOB, PASS, nullptr), ESave::OK);
	const std::vector<uint8_t> First = ReadFile();
	ASSERT_EQ(Save(m_aPath, BLOB, PASS, PASS), ESave::OK);
	const std::vector<uint8_t> Second = ReadFile();
	ASSERT_EQ(First.size(), Second.size());
	EXPECT_TRUE(std::equal(First.begin(), First.begin() + 16, Second.begin()));
	EXPECT_FALSE(std::equal(First.begin() + 16, First.begin() + 40, Second.begin() + 16));
	EXPECT_FALSE(std::equal(First.begin() + 40, First.begin() + 64, Second.begin() + 40));
}

TEST_F(CKeyFileTest, WrongPassphrase)
{
	ASSERT_EQ(Save(m_aPath, BLOB, PASS, nullptr), ESave::OK);
	char aBlob[MAX_BLOB];
	EXPECT_EQ(Load(m_aPath, "wrong horse battery", aBlob, sizeof(aBlob)), ELoad::WRONG_PASSPHRASE);
	EXPECT_EQ(aBlob[0], '\0');
	const std::string TooLong(MAX_PASSPHRASE + 1, 'a');
	EXPECT_EQ(Load(m_aPath, TooLong.c_str(), aBlob, sizeof(aBlob)), ELoad::WRONG_PASSPHRASE);
}

TEST_F(CKeyFileTest, AWrongPassphraseNeverReplacesTheKey)
{
	ASSERT_EQ(Save(m_aPath, BLOB, PASS, nullptr), ESave::OK);
	const std::vector<uint8_t> Before = ReadFile();
	const char NEW_BLOB[] = "v1\n11111111-1111-4222-8333-444455556666\nBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";

	EXPECT_EQ(Save(m_aPath, NEW_BLOB, nullptr, nullptr), ESave::LOCKED);
	EXPECT_EQ(Save(m_aPath, NEW_BLOB, nullptr, "wrong horse battery"), ESave::WRONG_PASSPHRASE);
	EXPECT_EQ(Save(m_aPath, NEW_BLOB, "another passphrase", "wrong horse battery"), ESave::WRONG_PASSPHRASE);
	EXPECT_EQ(ReadFile(), Before);

	ASSERT_EQ(Save(m_aPath, BLOB, "another passphrase", PASS), ESave::OK);
	char aBlob[MAX_BLOB];
	EXPECT_EQ(Load(m_aPath, PASS, aBlob, sizeof(aBlob)), ELoad::WRONG_PASSPHRASE);
	ASSERT_EQ(Load(m_aPath, "another passphrase", aBlob, sizeof(aBlob)), ELoad::OK);
	ASSERT_EQ(Save(m_aPath, BLOB, nullptr, "another passphrase"), ESave::OK);
	ASSERT_EQ(Load(m_aPath, nullptr, aBlob, sizeof(aBlob)), ELoad::OK);
	EXPECT_STREQ(aBlob, BLOB);
}

TEST_F(CKeyFileTest, PassphraseLength)
{
	EXPECT_EQ(Save(m_aPath, BLOB, "short", nullptr), ESave::PASSPHRASE_INVALID);
	EXPECT_EQ(Save(m_aPath, BLOB, std::string(MAX_PASSPHRASE + 1, 'a').c_str(), nullptr), ESave::PASSPHRASE_INVALID);
	EXPECT_FALSE(fs_is_file(m_aPath));
	EXPECT_EQ(Save(m_aPath, BLOB, std::string(MAX_PASSPHRASE, 'a').c_str(), nullptr), ESave::OK);
}

TEST_F(CKeyFileTest, ChangedFile)
{
	ASSERT_EQ(Save(m_aPath, BLOB, PASS, nullptr), ESave::OK);
	const std::vector<uint8_t> Good = ReadFile();
	char aBlob[MAX_BLOB];

	// Salt, nonce, tag and ciphertext: the tag does not match
	for(size_t Index : {(size_t)20, (size_t)40, (size_t)60, Good.size() - 1})
	{
		std::vector<uint8_t> Bad = Good;
		Bad[Index] ^= 1;
		WriteFile(Bad);
		EXPECT_EQ(Load(m_aPath, PASS, aBlob, sizeof(aBlob)), ELoad::WRONG_PASSPHRASE) << Index;
	}
	// The cost in the header is part of what is authenticated
	{
		std::vector<uint8_t> Bad = Good;
		Bad[12] = 2;
		WriteFile(Bad);
		EXPECT_EQ(Load(m_aPath, PASS, aBlob, sizeof(aBlob)), ELoad::WRONG_PASSPHRASE);
	}
	// A cost beyond the limits is refused before any work is done
	{
		std::vector<uint8_t> Bad = Good;
		Bad[10] = 0xff;
		WriteFile(Bad);
		EXPECT_EQ(Load(m_aPath, PASS, aBlob, sizeof(aBlob)), ELoad::CORRUPT);
		Bad = Good;
		Bad[12] = 100;
		WriteFile(Bad);
		EXPECT_EQ(Load(m_aPath, PASS, aBlob, sizeof(aBlob)), ELoad::CORRUPT);
	}
	for(size_t Size : {(size_t)4, (size_t)50, Good.size() - 20})
	{
		WriteFile(std::vector<uint8_t>(Good.begin(), Good.begin() + Size));
		EXPECT_NE(Load(m_aPath, PASS, aBlob, sizeof(aBlob)), ELoad::OK) << Size;
	}
	{
		std::vector<uint8_t> Bad = Good;
		Bad[4] = 9;
		WriteFile(Bad);
		EXPECT_EQ(Load(m_aPath, PASS, aBlob, sizeof(aBlob)), ELoad::CORRUPT);
	}
	WriteFile({});
	EXPECT_EQ(Load(m_aPath, nullptr, aBlob, sizeof(aBlob)), ELoad::CORRUPT);
}

TEST_F(CKeyFileTest, ADamagedFileIsNotReplaced)
{
	ASSERT_EQ(Save(m_aPath, BLOB, PASS, nullptr), ESave::OK);
	std::vector<uint8_t> Bad = ReadFile();
	Bad.resize(50);
	WriteFile(Bad);
	EXPECT_EQ(Save(m_aPath, BLOB, nullptr, nullptr), ESave::FAILED);
	EXPECT_EQ(ReadFile(), Bad);
	EXPECT_TRUE(Remove(m_aPath));
	EXPECT_FALSE(fs_is_file(m_aPath));
	EXPECT_EQ(Save(m_aPath, BLOB, nullptr, nullptr), ESave::OK);
}

TEST_F(CKeyFileTest, RemoveWithoutFile)
{
	EXPECT_TRUE(Remove(m_aPath));
}

TEST_F(CKeyFileTest, MigrationMovesTheKeyOnce)
{
	g_LegacyHasKey = true;
	g_LegacyRemoved = false;
	ASSERT_TRUE(Migrate(m_aPath, LegacyLoad, LegacyRemove));
	EXPECT_TRUE(g_LegacyRemoved);
	char aBlob[MAX_BLOB];
	ASSERT_EQ(Load(m_aPath, nullptr, aBlob, sizeof(aBlob)), ELoad::OK);
	EXPECT_STREQ(aBlob, BLOB);
	EXPECT_FALSE(Migrate(m_aPath, LegacyLoad, LegacyRemove));
}

TEST_F(CKeyFileTest, MigrationLeavesAFileAlone)
{
	ASSERT_EQ(Save(m_aPath, BLOB, PASS, nullptr), ESave::OK);
	const std::vector<uint8_t> Before = ReadFile();
	g_LegacyHasKey = true;
	g_LegacyRemoved = false;
	EXPECT_FALSE(Migrate(m_aPath, LegacyLoad, LegacyRemove));
	EXPECT_FALSE(g_LegacyRemoved);
	EXPECT_EQ(ReadFile(), Before);
}

TEST_F(CKeyFileTest, MigrationWithoutOldKeyOrWithGarbage)
{
	g_LegacyHasKey = false;
	g_LegacyRemoved = false;
	EXPECT_FALSE(Migrate(m_aPath, LegacyLoad, LegacyRemove));
	EXPECT_FALSE(Migrate(m_aPath, LegacyGarbage, LegacyRemove));
	EXPECT_FALSE(Migrate(m_aPath, nullptr, nullptr));
	EXPECT_FALSE(g_LegacyRemoved);
	EXPECT_FALSE(fs_is_file(m_aPath));
}
