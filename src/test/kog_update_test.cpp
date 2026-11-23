#include <base/str.h>

#include <engine/external/monocypher/monocypher-ed25519.h>
#include <engine/shared/kog_update.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace KogUpdate;

static const char SHA_A[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static const char SHA_B[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
static const char URL_PREFIX[] = "https://github.com/KoG-teeworlds/kog-client/releases/download/v0.2.0/";

static std::string TestManifest(int Sequence = 5, int64_t Issued = 1000000, int64_t Expires = 2000000, const char *pVersion = "0.2.0", const char *pExtraFile = "")
{
	char aBuf[2048];
	str_format(aBuf, sizeof(aBuf),
		"{\"format\":2,\"channel\":\"stable\",\"sequence\":%d,\"issued\":%lld,\"expires\":%lld,\"version\":\"%s\","
		"\"files\":[{\"path\":\"data/a.png\",\"sha256\":\"%s\",\"size\":10,\"url\":\"%sa\"}%s],"
		"\"platforms\":{\"linux-x86_64\":{\"client\":{\"sha256\":\"%s\",\"size\":100,\"url\":\"%sclient\"},"
		"\"files\":[{\"path\":\"data/b.png\",\"sha256\":\"%s\",\"size\":20,\"url\":\"%sb\"}]}},"
		"\"remove\":[\"data/old.png\"]}",
		Sequence, (long long)Issued, (long long)Expires, pVersion, SHA_A, URL_PREFIX, pExtraFile, SHA_B, URL_PREFIX, SHA_A, URL_PREFIX);
	return aBuf;
}

static bool Parse(const std::string &Json, SManifest *pOut, char *pError = nullptr, size_t ErrorSize = 0)
{
	return ParseManifest(Json.c_str(), Json.size(), "linux-x86_64", pOut, pError, ErrorSize);
}

TEST(KogUpdate, SignatureRoundTrip)
{
	unsigned char aSeed[32];
	memset(aSeed, 7, sizeof(aSeed));
	unsigned char aSecret[64];
	SPublicKey Key;
	crypto_ed25519_key_pair(aSecret, Key.m_aKey, aSeed);

	const std::string Manifest = TestManifest();
	unsigned char aSignature[64];
	crypto_ed25519_sign(aSignature, aSecret, reinterpret_cast<const uint8_t *>(Manifest.data()), Manifest.size());
	unsigned char aKeyId[4];
	memcpy(aKeyId, Key.m_aKey, sizeof(aKeyId));

	EXPECT_TRUE(VerifySignature(Manifest.data(), Manifest.size(), aSignature, aKeyId, &Key, 1));

	std::string Tampered = Manifest;
	Tampered[Tampered.size() / 2] ^= 1;
	EXPECT_FALSE(VerifySignature(Tampered.data(), Tampered.size(), aSignature, aKeyId, &Key, 1));

	unsigned char aBadSignature[64];
	memcpy(aBadSignature, aSignature, sizeof(aBadSignature));
	aBadSignature[10] ^= 1;
	EXPECT_FALSE(VerifySignature(Manifest.data(), Manifest.size(), aBadSignature, aKeyId, &Key, 1));

	unsigned char aOtherId[4] = {1, 2, 3, 4};
	EXPECT_FALSE(VerifySignature(Manifest.data(), Manifest.size(), aSignature, aOtherId, &Key, 1));
	EXPECT_FALSE(VerifySignature(Manifest.data(), Manifest.size(), aSignature, aKeyId, &Key, 0));

	SPublicKey Other = Key;
	Other.m_aKey[20] ^= 1;
	EXPECT_FALSE(VerifySignature(Manifest.data(), Manifest.size(), aSignature, aKeyId, &Other, 1));
}

TEST(KogUpdate, ZeroKeyAcceptsNothing)
{
	SPublicKey Zero = {};
	const unsigned char aSignature[64] = {};
	const unsigned char aKeyId[4] = {};
	const char aData[] = "x";
	EXPECT_FALSE(VerifySignature(aData, 1, aSignature, aKeyId, &Zero, 1));
}

TEST(KogUpdate, SignatureFile)
{
	std::string Text = "kog-update-sig-v1 0a0b0c0d ";
	Text.append(128, 'e');
	unsigned char aSignature[64];
	unsigned char aKeyId[4];
	EXPECT_TRUE(ParseSignatureFile(Text.data(), Text.size(), aSignature, aKeyId));
	EXPECT_EQ(aKeyId[0], 0x0a);
	EXPECT_EQ(aKeyId[3], 0x0d);
	EXPECT_EQ(aSignature[0], 0xee);
	Text += "\n";
	EXPECT_TRUE(ParseSignatureFile(Text.data(), Text.size(), aSignature, aKeyId));
	Text.pop_back();
	Text.pop_back();
	EXPECT_FALSE(ParseSignatureFile(Text.data(), Text.size(), aSignature, aKeyId));
	std::string Wrong = "kog-update-sig-v2 0a0b0c0d ";
	Wrong.append(128, 'e');
	EXPECT_FALSE(ParseSignatureFile(Wrong.data(), Wrong.size(), aSignature, aKeyId));
	std::string NotHex = "kog-update-sig-v1 0a0b0c0d ";
	NotHex.append(127, 'e');
	NotHex += 'g';
	EXPECT_FALSE(ParseSignatureFile(NotHex.data(), NotHex.size(), aSignature, aKeyId));
	EXPECT_FALSE(ParseSignatureFile("", 0, aSignature, aKeyId));
}

TEST(KogUpdate, ParseManifest)
{
	SManifest Manifest;
	char aError[256] = "";
	ASSERT_TRUE(Parse(TestManifest(), &Manifest, aError, sizeof(aError))) << aError;
	EXPECT_EQ(Manifest.m_Version, "0.2.0");
	EXPECT_EQ(Manifest.m_Sequence, 5);
	EXPECT_EQ(Manifest.m_Files.size(), 2u);
	EXPECT_TRUE(Manifest.m_HasClient);
	EXPECT_EQ(Manifest.m_Client.m_Size, 100);
	ASSERT_EQ(Manifest.m_Remove.size(), 1u);
	EXPECT_EQ(Manifest.m_Remove[0], "data/old.png");
	EXPECT_EQ(Manifest.m_Files[0].m_Sha256.data[0], 0xaa);

	EXPECT_FALSE(ParseManifest(TestManifest().c_str(), TestManifest().size(), "windows-x86_64", &Manifest, aError, sizeof(aError)));
}

TEST(KogUpdate, ParseManifestRejects)
{
	SManifest Manifest;
	std::string Json = TestManifest();
	auto Replace = [&](const char *pFrom, const char *pTo) {
		std::string Copy = Json;
		const size_t Pos = Copy.find(pFrom);
		EXPECT_NE(Pos, std::string::npos) << pFrom;
		return Copy.replace(Pos, strlen(pFrom), pTo);
	};
	EXPECT_FALSE(Parse("", &Manifest));
	EXPECT_FALSE(Parse("[]", &Manifest));
	EXPECT_FALSE(Parse("{", &Manifest));
	EXPECT_FALSE(Parse(Replace("\"format\":2", "\"format\":3"), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"format\":2", "\"format\":\"2\""), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"version\":\"0.2.0\"", "\"version\":\"0.2\""), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"version\":\"0.2.0\"", "\"version\":\"0.2.0-beta\""), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"sequence\":5", "\"sequence\":0"), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"expires\":2000000", "\"expires\":900000"), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"expires\":2000000", "\"expires\":99000000"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/a.png", "data/../a.png"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/a.png", "/data/a.png"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/a.png", "data//a.png"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/a.png", "data/.hidden"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/a.png", "data/a.dll"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/a.png", "other/a.png"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/a.png", "data\\\\a.png"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/b.png", "data/a.png"), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"size\":10", "\"size\":0"), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"size\":10", "\"size\":999999999999"), &Manifest));
	EXPECT_FALSE(Parse(Replace(SHA_A, "zz"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/old.png", "data/a.png"), &Manifest));

	std::string BadUrl = Json;
	for(const char *pUrl : {"http://github.com/KoG-teeworlds/kog-client/releases/download/x", "https://evil.example/x", "https://github.com/KoG-teeworlds/other/releases/download/x", "https://client.kog.tw.evil.example/x", "https://client.kog.tw/../x", "https://client.kog.tw/x y", "https://client.kog.tw/"})
	{
		std::string Copy = Json;
		const std::string From = std::string(URL_PREFIX) + "a";
		const size_t Pos = Copy.find(From);
		ASSERT_NE(Pos, std::string::npos);
		Copy.replace(Pos, From.size(), pUrl);
		EXPECT_FALSE(Parse(Copy, &Manifest)) << pUrl;
	}
}

TEST(KogUpdate, PathsAndUrls)
{
	EXPECT_TRUE(IsAllowedPath("data/a.png"));
	EXPECT_TRUE(IsAllowedPath("data/skins/warpaint.json"));
	EXPECT_TRUE(IsAllowedPath("data/with space/a-b_c+d.txt"));
	EXPECT_FALSE(IsAllowedPath("data/"));
	EXPECT_FALSE(IsAllowedPath("data"));
	EXPECT_FALSE(IsAllowedPath("data/a/"));
	EXPECT_FALSE(IsAllowedPath("data/a/../b"));
	EXPECT_FALSE(IsAllowedPath("data/C:/a"));
	EXPECT_FALSE(IsAllowedPath("data/a.SH"));
	EXPECT_FALSE(IsAllowedPath("data/ä.png"));
	EXPECT_FALSE(IsAllowedPath((std::string("data/") + std::string(300, 'a') + ".png").c_str()));
	EXPECT_TRUE(IsAllowedUrl("https://client.kog.tw/notes/0.2.0"));
	EXPECT_TRUE(IsAllowedUrl("https://github.com/KoG-teeworlds/kog-client/releases/download/v0.2.0/DDNet-linux-x86_64"));
	EXPECT_FALSE(IsAllowedUrl("https://client.kog.tw/a?b=c"));
	EXPECT_FALSE(IsAllowedUrl("https://user@client.kog.tw/a"));
}

TEST(KogUpdate, Versions)
{
	bool Ok;
	EXPECT_LT(CompareVersions("0.1.0", "0.2.0", &Ok), 0);
	EXPECT_TRUE(Ok);
	EXPECT_GT(CompareVersions("0.10.0", "0.9.9", &Ok), 0);
	EXPECT_EQ(CompareVersions("1.2.3", "1.2.3", &Ok), 0);
	EXPECT_TRUE(Ok);
	CompareVersions("1.2", "1.2.3", &Ok);
	EXPECT_FALSE(Ok);
	CompareVersions("1.2.3", "a.b.c", &Ok);
	EXPECT_FALSE(Ok);
	CompareVersions("1.2.3.4", "1.2.3", &Ok);
	EXPECT_FALSE(Ok);
	CompareVersions("", "1.2.3", &Ok);
	EXPECT_FALSE(Ok);
	CompareVersions("11111.0.0", "1.2.3", &Ok);
	EXPECT_FALSE(Ok);
}

TEST(KogUpdate, Decide)
{
	SManifest Manifest;
	ASSERT_TRUE(Parse(TestManifest(), &Manifest));
	const int64_t Now = 1500000;
	EXPECT_EQ(Decide(Manifest, "stable", "0.1.0", 5, Now), EVerdict::APPLY);
	EXPECT_EQ(Decide(Manifest, "stable", "0.1.0", 0, Now), EVerdict::APPLY);
	EXPECT_EQ(Decide(Manifest, "stable", "0.2.0", 5, Now), EVerdict::UP_TO_DATE);
	EXPECT_EQ(Decide(Manifest, "stable", "0.3.0", 5, Now), EVerdict::UP_TO_DATE);
	EXPECT_EQ(Decide(Manifest, "stable", "0.1.0", 6, Now), EVerdict::ROLLBACK);
	EXPECT_EQ(Decide(Manifest, "beta", "0.1.0", 5, Now), EVerdict::WRONG_CHANNEL);
	EXPECT_EQ(Decide(Manifest, "stable", "0.1.0", 5, 2000001), EVerdict::EXPIRED);
	EXPECT_EQ(Decide(Manifest, "stable", "0.1.0", 5, 2000000), EVerdict::APPLY);
	EXPECT_EQ(Decide(Manifest, "stable", "0.1.0", 5, 1000000 - CLOCK_SKEW_SECONDS - 1), EVerdict::NOT_YET_VALID);
	EXPECT_EQ(Decide(Manifest, "stable", "0.1.0", 5, 1000000 - CLOCK_SKEW_SECONDS), EVerdict::APPLY);
	EXPECT_EQ(Decide(Manifest, "stable", "junk", 5, Now), EVerdict::BAD_VERSION);
}

static std::string ReadWholeFile(const std::string &Path)
{
	std::string Result;
	FILE *pFile = fopen(Path.c_str(), "rb");
	if(!pFile)
		return Result;
	char aBuf[4096];
	size_t Read;
	while((Read = fread(aBuf, 1, sizeof(aBuf), pFile)) > 0)
		Result.append(aBuf, Read);
	fclose(pFile);
	return Result;
}

TEST(KogUpdate, ScriptFixture)
{
	const char *pDir = getenv("KOG_UPDATE_FIXTURE_DIR");
	if(!pDir)
		GTEST_SKIP() << "KOG_UPDATE_FIXTURE_DIR is not set";
	const std::string Dir = pDir;
	const std::string Manifest = ReadWholeFile(Dir + "/update.json");
	const std::string SignatureText = ReadWholeFile(Dir + "/update.json.sig");
	std::string PublicHex = ReadWholeFile(Dir + "/pubkey.hex");
	while(!PublicHex.empty() && (PublicHex.back() == '\n' || PublicHex.back() == '\r'))
		PublicHex.pop_back();
	ASSERT_FALSE(Manifest.empty());

	SPublicKey Key;
	ASSERT_EQ(str_hex_decode(Key.m_aKey, sizeof(Key.m_aKey), PublicHex.c_str()), 0);
	unsigned char aSignature[64];
	unsigned char aKeyId[4];
	ASSERT_TRUE(ParseSignatureFile(SignatureText.data(), SignatureText.size(), aSignature, aKeyId));
	EXPECT_TRUE(VerifySignature(Manifest.data(), Manifest.size(), aSignature, aKeyId, &Key, 1));

	SManifest Parsed;
	char aError[256] = "";
	ASSERT_TRUE(ParseManifest(Manifest.data(), Manifest.size(), "linux-x86_64", &Parsed, aError, sizeof(aError))) << aError;
	EXPECT_EQ(Decide(Parsed, "stable", "0.1.0", 3, Parsed.m_Issued + 1), EVerdict::APPLY);
	EXPECT_EQ(Decide(Parsed, "stable", "0.1.0", 5, Parsed.m_Issued + 1), EVerdict::ROLLBACK);
	EXPECT_TRUE(Parsed.m_HasClient);
	EXPECT_FALSE(Parsed.m_Files.empty());
}
