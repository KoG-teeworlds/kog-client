#include <base/hash.h>
#include <base/str.h>

#include <engine/external/monocypher/monocypher-ed25519.h>
#include <engine/shared/kog_update.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace KogUpdate;

namespace
{
const char SHA_A[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char SHA_B[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char URL_PREFIX[] = "https://github.com/KoG-teeworlds/kog-client/releases/download/v0.2.0/";
const char APPLICATION[] = "ssh:";
const int64_t NOW = 1500000;

struct SKeyPair
{
	unsigned char m_aSecret[64];
	unsigned char m_aPublic[32];
};

SKeyPair MakeKey(unsigned char SeedByte)
{
	SKeyPair Key;
	unsigned char aSeed[32];
	memset(aSeed, SeedByte, sizeof(aSeed));
	crypto_ed25519_key_pair(Key.m_aSecret, Key.m_aPublic, aSeed);
	return Key;
}

std::string Hex(const unsigned char *pData, size_t Length)
{
	static const char DIGITS[] = "0123456789abcdef";
	std::string Out;
	for(size_t i = 0; i < Length; i++)
	{
		Out += DIGITS[pData[i] >> 4];
		Out += DIGITS[pData[i] & 15];
	}
	return Out;
}

std::string CiSignature(const SKeyPair &Key, const std::string &Data)
{
	unsigned char aSignature[64];
	crypto_ed25519_sign(aSignature, Key.m_aSecret, reinterpret_cast<const uint8_t *>(Data.data()), Data.size());
	return std::string("kog-update-sig-v1 ") + Hex(Key.m_aPublic, 4) + " " + Hex(aSignature, 64) + "\n";
}

void PutU32(std::vector<unsigned char> *pOut, uint32_t Value)
{
	for(int Shift = 24; Shift >= 0; Shift -= 8)
		pOut->push_back((Value >> Shift) & 0xff);
}

void PutString(std::vector<unsigned char> *pOut, const void *pData, size_t Length)
{
	PutU32(pOut, (uint32_t)Length);
	pOut->insert(pOut->end(), static_cast<const unsigned char *>(pData), static_cast<const unsigned char *>(pData) + Length);
}

void PutString(std::vector<unsigned char> *pOut, const std::string &String)
{
	PutString(pOut, String.data(), String.size());
}

std::string Base64(const std::vector<unsigned char> &Data)
{
	static const char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string Out;
	for(size_t i = 0; i < Data.size(); i += 3)
	{
		const unsigned Chunk = (Data[i] << 16) | (i + 1 < Data.size() ? Data[i + 1] << 8 : 0) | (i + 2 < Data.size() ? Data[i + 2] : 0);
		Out += TABLE[(Chunk >> 18) & 63];
		Out += TABLE[(Chunk >> 12) & 63];
		Out += i + 1 < Data.size() ? TABLE[(Chunk >> 6) & 63] : '=';
		Out += i + 2 < Data.size() ? TABLE[Chunk & 63] : '=';
	}
	return Out;
}

struct SOwnerOptions
{
	bool m_Sk = true;
	unsigned char m_Flags = 1;
	std::string m_Namespace = OWNER_NAMESPACE;
	std::string m_HashAlg = "sha512";
	std::string m_Application = APPLICATION;
};

std::string OwnerSignature(const SKeyPair &Key, const std::string &Data, const SOwnerOptions &Options = SOwnerOptions())
{
	const std::string KeyType = Options.m_Sk ? "sk-ssh-ed25519@openssh.com" : "ssh-ed25519";
	std::vector<unsigned char> PublicBlob;
	PutString(&PublicBlob, KeyType);
	PutString(&PublicBlob, Key.m_aPublic, 32);
	if(Options.m_Sk)
		PutString(&PublicBlob, Options.m_Application);

	unsigned char aHash[64];
	crypto_sha512(aHash, reinterpret_cast<const uint8_t *>(Data.data()), Data.size());
	std::vector<unsigned char> Signed = {'S', 'S', 'H', 'S', 'I', 'G'};
	PutString(&Signed, Options.m_Namespace);
	PutString(&Signed, "", 0);
	PutString(&Signed, Options.m_HashAlg);
	PutString(&Signed, aHash, sizeof(aHash));

	std::vector<unsigned char> Message;
	if(Options.m_Sk)
	{
		const SHA256_DIGEST Application = sha256(Options.m_Application.data(), Options.m_Application.size());
		const SHA256_DIGEST Inner = sha256(Signed.data(), Signed.size());
		Message.insert(Message.end(), Application.data, Application.data + 32);
		Message.push_back(Options.m_Flags);
		PutU32(&Message, 7);
		Message.insert(Message.end(), Inner.data, Inner.data + 32);
	}
	else
	{
		Message = Signed;
	}
	unsigned char aSignature[64];
	crypto_ed25519_sign(aSignature, Key.m_aSecret, Message.data(), Message.size());

	std::vector<unsigned char> SignatureBlob;
	PutString(&SignatureBlob, KeyType);
	PutString(&SignatureBlob, aSignature, 64);
	if(Options.m_Sk)
	{
		SignatureBlob.push_back(Options.m_Flags);
		PutU32(&SignatureBlob, 7);
	}

	std::vector<unsigned char> Blob = {'S', 'S', 'H', 'S', 'I', 'G'};
	PutU32(&Blob, 1);
	PutString(&Blob, PublicBlob.data(), PublicBlob.size());
	PutString(&Blob, Options.m_Namespace);
	PutString(&Blob, "", 0);
	PutString(&Blob, Options.m_HashAlg);
	PutString(&Blob, SignatureBlob.data(), SignatureBlob.size());

	std::string Armored = "-----BEGIN SSH SIGNATURE-----\n";
	const std::string Encoded = Base64(Blob);
	for(size_t i = 0; i < Encoded.size(); i += 70)
		Armored += Encoded.substr(i, 70) + "\n";
	return Armored + "-----END SSH SIGNATURE-----\n";
}

SOwnerKey OwnerKeyOf(const SKeyPair &Key, const char *pApplication = APPLICATION)
{
	SOwnerKey Owner;
	memcpy(Owner.m_aKey, Key.m_aPublic, 32);
	Owner.m_pApplication = pApplication;
	return Owner;
}

SPublicKey CiKeyOf(const SKeyPair &Key)
{
	SPublicKey Public;
	memcpy(Public.m_aKey, Key.m_aPublic, 32);
	return Public;
}

struct SFixture
{
	SKeyPair m_StableCi = MakeKey(1);
	SKeyPair m_StableCiBackup = MakeKey(2);
	SKeyPair m_OwnerKey = MakeKey(3);
	SKeyPair m_OwnerBackup = MakeKey(4);
	SKeyPair m_CanaryCi = MakeKey(5);
	SKeyPair m_DevCi = MakeKey(6);
	SPublicKey m_aStableCi[2] = {CiKeyOf(m_StableCi), CiKeyOf(m_StableCiBackup)};
	SOwnerKey m_aOwner[2] = {OwnerKeyOf(m_OwnerKey), OwnerKeyOf(m_OwnerBackup)};
	SPublicKey m_aCanaryCi[1] = {CiKeyOf(m_CanaryCi)};
	SPublicKey m_aDevCi[1] = {CiKeyOf(m_DevCi)};
	SPublicKey m_aBackupCi[1] = {CiKeyOf(m_StableCiBackup)};
	SChannelKeys m_Stable = {CHANNEL_STABLE, m_aStableCi, 1, m_aOwner, 2, m_aBackupCi, 1};
	SChannelKeys m_Canary = {CHANNEL_CANARY, m_aCanaryCi, 1, nullptr, 0};
	SChannelKeys m_Dev = {CHANNEL_DEV, m_aDevCi, 1, nullptr, 0};
};

struct SManifestOptions
{
	const char *m_pChannel = "stable";
	const char *m_pVersion = "0.2.0";
	int m_Sequence = 5;
	int64_t m_Issued = 1000000;
	int64_t m_Expires = 2000000;
	std::string m_Extra;
};

std::string TestManifest(const SManifestOptions &Options = SManifestOptions())
{
	char aBuf[3072];
	str_format(aBuf, sizeof(aBuf),
		"{\"format\":2,\"channel\":\"%s\",\"sequence\":%d,\"issued\":%lld,\"expires\":%lld,\"version\":\"%s\"%s,"
		"\"files\":[{\"path\":\"data/a.png\",\"sha256\":\"%s\",\"size\":10,\"url\":\"%sa\"}],"
		"\"platforms\":{\"linux-x86_64\":{\"client\":{\"sha256\":\"%s\",\"size\":100,\"url\":\"%sclient\"},"
		"\"files\":[{\"path\":\"data/b.png\",\"sha256\":\"%s\",\"size\":20,\"url\":\"%sb\"}]}},"
		"\"remove\":[\"data/old.png\"]}",
		Options.m_pChannel, Options.m_Sequence, (long long)Options.m_Issued, (long long)Options.m_Expires, Options.m_pVersion, Options.m_Extra.c_str(),
		SHA_A, URL_PREFIX, SHA_A, URL_PREFIX, SHA_B, URL_PREFIX);
	return aBuf;
}

bool Parse(const std::string &Json, SManifest *pOut, char *pError = nullptr, size_t ErrorSize = 0)
{
	return ParseManifest(Json.c_str(), Json.size(), "linux-x86_64", pOut, pError, ErrorSize);
}

ESignatureResult Verify(const std::string &Manifest, const std::string &Ci, const std::string &Owner, const SChannelKeys &Keys)
{
	return VerifyManifestSignatures(Manifest.data(), Manifest.size(), Ci.data(), Ci.size(), Owner.data(), Owner.size(), &Keys);
}

SDecideInput Input(const char *pChannel, const char *pCurrent, int64_t LastSequence = 0)
{
	SDecideInput In;
	In.m_pChannel = pChannel;
	In.m_pCurrentVersion = pCurrent;
	In.m_LastSequence = LastSequence;
	In.m_Now = NOW;
	return In;
}
}

TEST(KogUpdate, CiSignatureRoundTrip)
{
	const SKeyPair Key = MakeKey(9);
	const SPublicKey Public = CiKeyOf(Key);
	const std::string Manifest = TestManifest();
	const std::string Text = CiSignature(Key, Manifest);
	unsigned char aSignature[64];
	unsigned char aKeyId[4];
	ASSERT_TRUE(ParseSignatureFile(Text.data(), Text.size(), aSignature, aKeyId));
	EXPECT_TRUE(VerifySignature(Manifest.data(), Manifest.size(), aSignature, aKeyId, &Public, 1));

	std::string Tampered = Manifest;
	Tampered[Tampered.size() / 2] ^= 1;
	EXPECT_FALSE(VerifySignature(Tampered.data(), Tampered.size(), aSignature, aKeyId, &Public, 1));
	unsigned char aOtherId[4] = {1, 2, 3, 4};
	EXPECT_FALSE(VerifySignature(Manifest.data(), Manifest.size(), aSignature, aOtherId, &Public, 1));
	EXPECT_FALSE(VerifySignature(Manifest.data(), Manifest.size(), aSignature, aKeyId, &Public, 0));
	SPublicKey Zero = {};
	const unsigned char aZeroSignature[64] = {};
	const unsigned char aZeroId[4] = {};
	EXPECT_FALSE(VerifySignature("x", 1, aZeroSignature, aZeroId, &Zero, 1));
}

TEST(KogUpdate, SignatureFile)
{
	std::string Text = "kog-update-sig-v1 0a0b0c0d ";
	Text.append(128, 'e');
	unsigned char aSignature[64];
	unsigned char aKeyId[4];
	EXPECT_TRUE(ParseSignatureFile(Text.data(), Text.size(), aSignature, aKeyId));
	EXPECT_EQ(aKeyId[3], 0x0d);
	Text += "\n";
	EXPECT_TRUE(ParseSignatureFile(Text.data(), Text.size(), aSignature, aKeyId));
	Text.pop_back();
	Text.pop_back();
	EXPECT_FALSE(ParseSignatureFile(Text.data(), Text.size(), aSignature, aKeyId));
	EXPECT_FALSE(ParseSignatureFile("", 0, aSignature, aKeyId));
	const std::string Armored = OwnerSignature(MakeKey(3), "x");
	EXPECT_FALSE(ParseSignatureFile(Armored.data(), Armored.size(), aSignature, aKeyId));
}

TEST(KogUpdate, OwnerSignature)
{
	const SKeyPair Key = MakeKey(3);
	const SOwnerKey Owner = OwnerKeyOf(Key);
	const std::string Data = TestManifest();
	auto Check = [&](const std::string &Armored, const std::string &Signed = std::string(), bool Software = false) {
		const std::string &Message = Signed.empty() ? Data : Signed;
		return VerifyOwnerSignature(Message.data(), Message.size(), Armored.data(), Armored.size(), &Owner, 1, Software);
	};
	EXPECT_TRUE(Check(OwnerSignature(Key, Data)));

	std::string Tampered = Data;
	Tampered[3] ^= 1;
	EXPECT_FALSE(Check(OwnerSignature(Key, Data), Tampered));

	SOwnerOptions NoTouch;
	NoTouch.m_Flags = 0;
	EXPECT_FALSE(Check(OwnerSignature(Key, Data, NoTouch)));

	SOwnerOptions Touch;
	Touch.m_Flags = 5;
	EXPECT_TRUE(Check(OwnerSignature(Key, Data, Touch)));

	SOwnerOptions WrongNamespace;
	WrongNamespace.m_Namespace = "file";
	EXPECT_FALSE(Check(OwnerSignature(Key, Data, WrongNamespace)));

	SOwnerOptions WrongHash;
	WrongHash.m_HashAlg = "sha256";
	EXPECT_FALSE(Check(OwnerSignature(Key, Data, WrongHash)));

	SOwnerOptions WrongApplication;
	WrongApplication.m_Application = "ssh:other";
	EXPECT_FALSE(Check(OwnerSignature(Key, Data, WrongApplication)));

	EXPECT_FALSE(Check(OwnerSignature(MakeKey(4), Data)));

	SOwnerOptions Software;
	Software.m_Sk = false;
	const std::string SoftwareSignature = OwnerSignature(Key, Data, Software);
	EXPECT_FALSE(Check(SoftwareSignature));
	EXPECT_TRUE(Check(SoftwareSignature, std::string(), true));

	std::string Trailing = OwnerSignature(Key, Data) + "garbage";
	EXPECT_FALSE(Check(Trailing));
	std::string Broken = OwnerSignature(Key, Data);
	Broken[40] = '!';
	EXPECT_FALSE(Check(Broken));
	EXPECT_FALSE(Check(""));
	EXPECT_FALSE(Check("-----BEGIN SSH SIGNATURE-----\n-----END SSH SIGNATURE-----\n"));
	EXPECT_FALSE(Check(" " + OwnerSignature(Key, Data)));

	SOwnerKey Zero = {};
	Zero.m_pApplication = APPLICATION;
	const std::string Armored = OwnerSignature(Key, Data);
	EXPECT_FALSE(VerifyOwnerSignature(Data.data(), Data.size(), Armored.data(), Armored.size(), &Zero, 1, false));
}

TEST(KogUpdate, StableNeedsBothSignatures)
{
	const SFixture F;
	const std::string Manifest = TestManifest();
	const std::string Ci = CiSignature(F.m_StableCi, Manifest);
	const std::string Owner = OwnerSignature(F.m_OwnerKey, Manifest);

	EXPECT_EQ(Verify(Manifest, Ci, Owner, F.m_Stable), ESignatureResult::OK);
	EXPECT_EQ(Verify(Manifest, Ci, "", F.m_Stable), ESignatureResult::MISSING_OWNER);
	EXPECT_EQ(Verify(Manifest, "", Owner, F.m_Stable), ESignatureResult::MALFORMED_CI);
	EXPECT_EQ(Verify(Manifest, Owner, Ci, F.m_Stable), ESignatureResult::MALFORMED_CI);
	EXPECT_EQ(Verify(Manifest, CiSignature(F.m_DevCi, Manifest), Owner, F.m_Stable), ESignatureResult::BAD_CI);
	EXPECT_EQ(Verify(Manifest, Ci, OwnerSignature(F.m_StableCi, Manifest), F.m_Stable), ESignatureResult::BAD_OWNER);
	EXPECT_EQ(Verify(Manifest, Ci, OwnerSignature(F.m_DevCi, Manifest), F.m_Stable), ESignatureResult::BAD_OWNER);
	EXPECT_EQ(Verify(Manifest, CiSignature(F.m_StableCiBackup, Manifest), OwnerSignature(F.m_OwnerBackup, Manifest), F.m_Stable), ESignatureResult::OK);
	EXPECT_EQ(Verify(Manifest, CiSignature(F.m_StableCiBackup, Manifest), Owner, F.m_Stable), ESignatureResult::OK);

	std::string Tampered = Manifest;
	Tampered[10] ^= 1;
	EXPECT_EQ(Verify(Tampered, Ci, Owner, F.m_Stable), ESignatureResult::BAD_CI);
	const std::string Other = TestManifest(SManifestOptions{.m_Sequence = 6});
	EXPECT_EQ(Verify(Manifest, Ci, OwnerSignature(F.m_OwnerKey, Other), F.m_Stable), ESignatureResult::BAD_OWNER);

	EXPECT_EQ(VerifyManifestSignatures(Manifest.data(), Manifest.size(), Ci.data(), Ci.size(), Owner.data(), Owner.size(), nullptr), ESignatureResult::NO_KEYS);
	SChannelKeys None = {CHANNEL_STABLE, nullptr, 0, nullptr, 0};
	EXPECT_EQ(Verify(Manifest, Ci, Owner, None), ESignatureResult::NO_KEYS);
}

TEST(KogUpdate, EachChannelVerifiesOnlyItsOwnKeys)
{
	const SFixture F;
	struct SCase
	{
		const char *m_pChannel;
		const char *m_pVersion;
		const SChannelKeys *m_pKeys;
		const SKeyPair *m_pCi;
	};
	const SCase aCases[] = {
		{CHANNEL_STABLE, "1.0.0", &F.m_Stable, &F.m_StableCi},
		{CHANNEL_CANARY, "1.0.0-canary.4", &F.m_Canary, &F.m_CanaryCi},
		{CHANNEL_DEV, "1.0.0-dev.4", &F.m_Dev, &F.m_DevCi},
	};
	for(const SCase &Case : aCases)
	{
		SManifestOptions Options;
		Options.m_pChannel = Case.m_pChannel;
		Options.m_pVersion = Case.m_pVersion;
		Options.m_Expires = Options.m_Issued + 10 * 24 * 3600;
		const std::string Manifest = TestManifest(Options);
		SManifest Parsed;
		char aError[256] = "";
		ASSERT_TRUE(Parse(Manifest, &Parsed, aError, sizeof(aError))) << Case.m_pChannel << ": " << aError;
		const std::string Owner = Case.m_pKeys->m_NumOwnerKeys > 0 ? OwnerSignature(F.m_OwnerKey, Manifest) : std::string();

		EXPECT_EQ(Verify(Manifest, CiSignature(*Case.m_pCi, Manifest), Owner, *Case.m_pKeys), ESignatureResult::OK) << Case.m_pChannel;
		for(const SCase &Other : aCases)
		{
			if(&Other == &Case)
				continue;
			EXPECT_EQ(Verify(Manifest, CiSignature(*Other.m_pCi, Manifest), Owner, *Case.m_pKeys), ESignatureResult::BAD_CI) << Case.m_pChannel << " signed with the key of " << Other.m_pChannel;
			EXPECT_EQ(Verify(Manifest, CiSignature(*Case.m_pCi, Manifest), Owner, *Other.m_pKeys), ESignatureResult::BAD_CI) << Case.m_pChannel << " checked against " << Other.m_pChannel;
		}

		EXPECT_EQ(Decide(Parsed, Input(Case.m_pChannel, Case.m_pVersion, 0)), EVerdict::UP_TO_DATE);
		for(const SCase &Other : aCases)
		{
			if(&Other != &Case)
			{
				EXPECT_EQ(Decide(Parsed, Input(Other.m_pChannel, "0.0.1")), EVerdict::WRONG_CHANNEL) << Case.m_pChannel << " manifest for a client set to " << Other.m_pChannel;
			}
		}
		SDecideInput Expired = Input(Case.m_pChannel, "0.0.1");
		Expired.m_Now = Parsed.m_Expires + 1;
		EXPECT_EQ(Decide(Parsed, Expired), EVerdict::EXPIRED);
		EXPECT_EQ(Decide(Parsed, Input(Case.m_pChannel, "0.0.1", Parsed.m_Sequence + 1)), EVerdict::ROLLBACK);
		EXPECT_EQ(Decide(Parsed, Input(Case.m_pChannel, "0.0.1", Parsed.m_Sequence)), EVerdict::APPLY);
	}
}

TEST(KogUpdate, VersionsAndChannels)
{
	bool Ok;
	EXPECT_LT(CompareVersions("0.1.0", "0.2.0", &Ok), 0);
	EXPECT_GT(CompareVersions("0.10.0", "0.9.9", &Ok), 0);
	EXPECT_EQ(CompareVersions("1.2.3", "1.2.3", &Ok), 0);
	EXPECT_LT(CompareVersions("1.3.0-dev.9", "1.3.0-canary.1", &Ok), 0);
	EXPECT_LT(CompareVersions("1.3.0-canary.9", "1.3.0", &Ok), 0);
	EXPECT_GT(CompareVersions("1.3.0-dev.1", "1.2.9", &Ok), 0);
	EXPECT_GT(CompareVersions("1.3.0-canary.10", "1.3.0-canary.9", &Ok), 0);
	EXPECT_TRUE(Ok);
	for(const char *pBad : {"1.2", "1.2.3.4", "", "a.b.c", "1.2.3-beta.1", "1.2.3-canary", "1.2.3-canary.", "1.2.3-canary.x", "1.2.3-dev.1.2", "11111.0.0", "1.2.3-canary.1234567890"})
	{
		CompareVersions(pBad, "1.2.3", &Ok);
		EXPECT_FALSE(Ok) << pBad;
	}
	EXPECT_STREQ(ChannelOfVersion("1.2.3"), CHANNEL_STABLE);
	EXPECT_STREQ(ChannelOfVersion("1.2.3-canary.5"), CHANNEL_CANARY);
	EXPECT_STREQ(ChannelOfVersion("1.2.3-dev.5"), CHANNEL_DEV);
	EXPECT_EQ(ChannelOfVersion("x"), nullptr);
	EXPECT_TRUE(IsKnownChannel("dev"));
	EXPECT_FALSE(IsKnownChannel("test/x"));
	EXPECT_FALSE(IsKnownChannel(""));
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
	EXPECT_EQ(Manifest.TotalSize(), 130);
	ASSERT_EQ(Manifest.m_Remove.size(), 1u);
	EXPECT_EQ(Manifest.m_RolloutPercent, 100);
	EXPECT_FALSE(Manifest.m_Halted);
	EXPECT_TRUE(Manifest.m_MinVersion.empty());
	EXPECT_FALSE(ParseManifest(TestManifest().c_str(), TestManifest().size(), "windows-x86_64", &Manifest, aError, sizeof(aError)));
}

TEST(KogUpdate, ParseManifestRejects)
{
	SManifest Manifest;
	const std::string Json = TestManifest();
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
	EXPECT_FALSE(Parse(Replace("\"version\":\"0.2.0\"", "\"version\":\"0.2\""), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"version\":\"0.2.0\"", "\"version\":\"0.2.0-canary.1\""), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"channel\":\"stable\"", "\"channel\":\"test\""), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"channel\":\"stable\"", "\"channel\":\"Stable\""), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"sequence\":5", "\"sequence\":0"), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"expires\":2000000", "\"expires\":900000"), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"expires\":2000000", "\"expires\":99000000"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/a.png", "data/../a.png"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/a.png", "data/a.dll"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/b.png", "data/a.png"), &Manifest));
	EXPECT_FALSE(Parse(Replace("\"size\":10", "\"size\":0"), &Manifest));
	EXPECT_FALSE(Parse(Replace(SHA_A, "zz"), &Manifest));
	EXPECT_FALSE(Parse(Replace("data/old.png", "data/a.png"), &Manifest));
	for(const char *pUrl : {"http://github.com/KoG-teeworlds/kog-client/releases/download/x", "https://evil.example/x", "https://client.kog.tw.evil.example/x", "https://client.kog.tw/../x", "https://client.kog.tw/x y", "https://client.kog.tw/"})
	{
		std::string Copy = Json;
		const std::string From = std::string(URL_PREFIX) + "a";
		Copy.replace(Copy.find(From), From.size(), pUrl);
		EXPECT_FALSE(Parse(Copy, &Manifest)) << pUrl;
	}

	SManifestOptions Dev;
	Dev.m_pChannel = "dev";
	Dev.m_pVersion = "0.2.0-dev.1";
	Dev.m_Expires = Dev.m_Issued + 15 * 24 * 3600;
	EXPECT_FALSE(Parse(TestManifest(Dev), &Manifest));
	Dev.m_Expires = Dev.m_Issued + 14 * 24 * 3600;
	EXPECT_TRUE(Parse(TestManifest(Dev), &Manifest));
	SManifestOptions Canary;
	Canary.m_pChannel = "canary";
	Canary.m_pVersion = "0.2.0-canary.1";
	Canary.m_Expires = Canary.m_Issued + 31 * 24 * 3600;
	EXPECT_FALSE(Parse(TestManifest(Canary), &Manifest));
}

TEST(KogUpdate, OptionalFields)
{
	SManifest Manifest;
	auto WithExtra = [&](const char *pExtra) {
		SManifestOptions Options;
		Options.m_Extra = pExtra;
		return Parse(TestManifest(Options), &Manifest);
	};
	EXPECT_TRUE(WithExtra(",\"rollout_percent\":25,\"halted\":false,\"min_version\":\"0.1.0\",\"notes_url\":\"https://client.kog.tw/notes/0.2.0\",\"changes\":[\"Fixes a crash\",\"New menu\"],\"unknown\":{\"a\":1}"));
	EXPECT_EQ(Manifest.m_RolloutPercent, 25);
	EXPECT_EQ(Manifest.m_MinVersion, "0.1.0");
	EXPECT_EQ(Manifest.m_vChanges.size(), 2u);
	EXPECT_TRUE(WithExtra(",\"notes_url\":\"https://github.com/KoG-teeworlds/kog-client/releases/tag/v0.2.0\""));

	EXPECT_FALSE(WithExtra(",\"rollout_percent\":101"));
	EXPECT_FALSE(WithExtra(",\"rollout_percent\":-1"));
	EXPECT_FALSE(WithExtra(",\"rollout_percent\":\"50\""));
	EXPECT_FALSE(WithExtra(",\"halted\":1"));
	EXPECT_FALSE(WithExtra(",\"min_version\":\"x\""));
	EXPECT_FALSE(WithExtra(",\"notes_url\":\"https://evil.example/notes\""));
	EXPECT_FALSE(WithExtra(",\"notes_url\":\"https://client.kog.tw/a?b=c\""));
	EXPECT_FALSE(WithExtra(",\"changes\":[\"a\",\"b\",\"c\",\"d\"]"));
	EXPECT_FALSE(WithExtra(",\"changes\":[\"<b>bold</b>\"]"));
	EXPECT_FALSE(WithExtra(",\"changes\":[\"line\\nbreak\"]"));
	EXPECT_FALSE(WithExtra(",\"changes\":[\"\"]"));
	EXPECT_FALSE(WithExtra(",\"changes\":[1]"));
	EXPECT_FALSE(WithExtra((",\"changes\":[\"" + std::string(121, 'a') + "\"]").c_str()));
	EXPECT_TRUE(WithExtra((",\"changes\":[\"" + std::string(120, 'a') + "\"]").c_str()));
	EXPECT_FALSE(WithExtra(",\"changes\":[\"right to left \xe2\x80\xae text\"]"));
	EXPECT_TRUE(WithExtra(",\"changes\":[\"Umlaut \xc3\xa4 is fine\"]"));
	EXPECT_FALSE(WithExtra(",\"changes\":[\"bad \xff utf8\"]"));

	const std::string WithMirror = TestManifest();
	std::string Mirrored = WithMirror;
	const std::string From = std::string("\"url\":\"") + URL_PREFIX + "a\"";
	Mirrored.replace(Mirrored.find(From), From.size(), From + ",\"mirrors\":[\"https://client.kog.tw/files/a\"]");
	ASSERT_TRUE(Parse(Mirrored, &Manifest));
	EXPECT_EQ(Manifest.m_Files[0].m_vUrls.size(), 2u);
	std::string BadMirror = WithMirror;
	BadMirror.replace(BadMirror.find(From), From.size(), From + ",\"mirrors\":[\"https://evil.example/a\"]");
	EXPECT_FALSE(Parse(BadMirror, &Manifest));
	std::string TooMany = WithMirror;
	TooMany.replace(TooMany.find(From), From.size(), From + ",\"mirrors\":[\"https://client.kog.tw/a\",\"https://client.kog.tw/b\",\"https://client.kog.tw/c\"]");
	EXPECT_FALSE(Parse(TooMany, &Manifest));
}

TEST(KogUpdate, DecideRollout)
{
	SManifest Manifest;
	SManifestOptions Options;
	Options.m_Extra = ",\"rollout_percent\":30";
	ASSERT_TRUE(Parse(TestManifest(Options), &Manifest));
	SDecideInput In = Input("stable", "0.1.0");
	In.m_Bucket = 29;
	EXPECT_EQ(Decide(Manifest, In), EVerdict::APPLY);
	In.m_Bucket = 30;
	EXPECT_EQ(Decide(Manifest, In), EVerdict::NOT_IN_ROLLOUT);
	In.m_Bucket = 99;
	EXPECT_EQ(Decide(Manifest, In), EVerdict::NOT_IN_ROLLOUT);

	Options.m_Extra = ",\"rollout_percent\":0";
	ASSERT_TRUE(Parse(TestManifest(Options), &Manifest));
	In.m_Bucket = 0;
	EXPECT_EQ(Decide(Manifest, In), EVerdict::NOT_IN_ROLLOUT);

	Options.m_Extra = ",\"rollout_percent\":0,\"min_version\":\"0.2.0\"";
	ASSERT_TRUE(Parse(TestManifest(Options), &Manifest));
	EXPECT_EQ(Decide(Manifest, In), EVerdict::APPLY);
	EXPECT_TRUE(UpdateRequired(Manifest, "0.1.0"));
	EXPECT_FALSE(UpdateRequired(Manifest, "0.2.0"));
	EXPECT_FALSE(UpdateRequired(Manifest, "0.3.0"));

	for(int i = 0; i < 1000; i++)
	{
		const std::string Id = std::to_string(i) + "abc";
		EXPECT_GE(Bucket(Id.c_str()), 0);
		EXPECT_LT(Bucket(Id.c_str()), 100);
		EXPECT_EQ(Bucket(Id.c_str()), Bucket(Id.c_str()));
	}
	int aCounts[100] = {};
	for(int i = 0; i < 10000; i++)
		aCounts[Bucket(std::to_string(i).c_str())]++;
	for(int Count : aCounts)
		EXPECT_GT(Count, 40);
}

TEST(KogUpdate, DecideHaltAndBadVersion)
{
	SManifest Manifest;
	SManifestOptions Options;
	Options.m_Extra = ",\"halted\":true";
	ASSERT_TRUE(Parse(TestManifest(Options), &Manifest));
	EXPECT_EQ(Decide(Manifest, Input("stable", "0.1.0")), EVerdict::HALTED);
	EXPECT_EQ(Decide(Manifest, Input("stable", "0.2.0")), EVerdict::HALTED);

	Options.m_Extra = ",\"halted\":true,\"min_version\":\"0.2.0\"";
	ASSERT_TRUE(Parse(TestManifest(Options), &Manifest));
	EXPECT_FALSE(UpdateRequired(Manifest, "0.1.0"));

	Options.m_Extra = "";
	ASSERT_TRUE(Parse(TestManifest(Options), &Manifest));
	SDecideInput In = Input("stable", "0.1.0");
	In.m_pBadVersion = "0.2.0";
	EXPECT_EQ(Decide(Manifest, In), EVerdict::KNOWN_BAD);
	In.m_pBadVersion = "0.3.0";
	EXPECT_EQ(Decide(Manifest, In), EVerdict::APPLY);
	EXPECT_EQ(Decide(Manifest, Input("stable", "junk")), EVerdict::BAD_VERSION);
	EXPECT_EQ(Decide(Manifest, Input("stable", "0.2.0")), EVerdict::UP_TO_DATE);
	EXPECT_EQ(Decide(Manifest, Input("stable", "0.3.0")), EVerdict::UP_TO_DATE);
	SDecideInput Future = Input("stable", "0.1.0");
	Future.m_Now = 1000000 - CLOCK_SKEW_SECONDS - 1;
	EXPECT_EQ(Decide(Manifest, Future), EVerdict::NOT_YET_VALID);
	Future.m_Now = 1000000 - CLOCK_SKEW_SECONDS;
	EXPECT_EQ(Decide(Manifest, Future), EVerdict::APPLY);
}

TEST(KogUpdate, SwitchingChannels)
{
	SManifest Stable;
	SManifestOptions StableOptions;
	StableOptions.m_pVersion = "1.2.9";
	ASSERT_TRUE(Parse(TestManifest(StableOptions), &Stable));
	SManifest Canary;
	SManifestOptions CanaryOptions;
	CanaryOptions.m_pChannel = "canary";
	CanaryOptions.m_pVersion = "1.3.0-canary.7";
	ASSERT_TRUE(Parse(TestManifest(CanaryOptions), &Canary));

	SDecideInput ToStable = Input("stable", "1.3.0-canary.9");
	EXPECT_EQ(Decide(Stable, ToStable), EVerdict::NEEDS_CONFIRMATION);
	ToStable.m_SwitchConfirmed = true;
	EXPECT_EQ(Decide(Stable, ToStable), EVerdict::APPLY);

	SDecideInput ToCanary = Input("canary", "1.3.0");
	EXPECT_EQ(Decide(Canary, ToCanary), EVerdict::NEEDS_CONFIRMATION);
	ToCanary.m_SwitchConfirmed = true;
	EXPECT_EQ(Decide(Canary, ToCanary), EVerdict::APPLY);

	SDecideInput Upward = Input("canary", "1.2.0");
	Upward.m_Bucket = 99;
	EXPECT_EQ(Decide(Canary, Upward), EVerdict::APPLY);

	SDecideInput Within = Input("canary", "1.3.0-canary.9");
	Within.m_SwitchConfirmed = true;
	EXPECT_EQ(Decide(Canary, Within), EVerdict::UP_TO_DATE);
	EXPECT_EQ(Decide(Canary, Input("canary", "1.3.0-canary.7")), EVerdict::UP_TO_DATE);
	EXPECT_EQ(Decide(Canary, Input("canary", "1.3.0-canary.6")), EVerdict::APPLY);

	SManifest Halted;
	CanaryOptions.m_Extra = ",\"halted\":true";
	ASSERT_TRUE(Parse(TestManifest(CanaryOptions), &Halted));
	SDecideInput Confirmed = Input("canary", "1.3.0");
	Confirmed.m_SwitchConfirmed = true;
	EXPECT_EQ(Decide(Halted, Confirmed), EVerdict::HALTED);
}

TEST(KogUpdate, PathsAndUrls)
{
	EXPECT_TRUE(IsAllowedPath("data/a.png"));
	EXPECT_TRUE(IsAllowedPath("data/with space/a-b_c+d.txt"));
	for(const char *pBad : {"data/", "data", "data/a/", "data/a/../b", "data/C:/a", "data/a.SH", "other/a", "data//a", "data/.hidden"})
		EXPECT_FALSE(IsAllowedPath(pBad)) << pBad;
	EXPECT_TRUE(IsAllowedUrl("https://client.kog.tw/notes/0.2.0"));
	EXPECT_TRUE(IsAllowedUrl("https://raw.githubusercontent.com/KoG-teeworlds/kog-client-updates/published/stable/update.json"));
	EXPECT_TRUE(IsAllowedUrl("https://github.com/KoG-teeworlds/kog-client/releases/download/v0.2.0/x"));
	EXPECT_FALSE(IsAllowedUrl("https://kog-teeworlds.github.io/kog-client-updates/stable/update.json"));
	EXPECT_FALSE(IsAllowedUrl("https://raw.githubusercontent.com/KoG-teeworlds/other/published/a"));
	for(const char *pBad : {"https://client.kog.tw/a?b=c", "https://user@client.kog.tw/a", "https://github.com/KoG-teeworlds/other/releases/download/x", "http://client.kog.tw/a"})
		EXPECT_FALSE(IsAllowedUrl(pBad)) << pBad;
	EXPECT_TRUE(IsAllowedNotesUrl("https://github.com/KoG-teeworlds/kog-client/releases/tag/v1"));
	EXPECT_FALSE(IsAllowedNotesUrl("https://github.com/someone-else/x"));
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
	auto Trim = [](std::string Text) {
		while(!Text.empty() && (Text.back() == '\n' || Text.back() == '\r'))
			Text.pop_back();
		return Text;
	};
	const std::string Manifest = ReadWholeFile(Dir + "/update.json");
	const std::string Ci = ReadWholeFile(Dir + "/update.json.sig");
	const std::string Owner = ReadWholeFile(Dir + "/update.json.sigb");
	const std::string OwnerSoftware = ReadWholeFile(Dir + "/update.json.sigb.software");
	ASSERT_FALSE(Manifest.empty());
	ASSERT_FALSE(Owner.empty());

	SPublicKey Ci1;
	SOwnerKey OwnerKey;
	ASSERT_EQ(str_hex_decode(Ci1.m_aKey, sizeof(Ci1.m_aKey), Trim(ReadWholeFile(Dir + "/ci.pub")).c_str()), 0);
	ASSERT_EQ(str_hex_decode(OwnerKey.m_aKey, sizeof(OwnerKey.m_aKey), Trim(ReadWholeFile(Dir + "/owner.pub")).c_str()), 0);
	const std::string Application = Trim(ReadWholeFile(Dir + "/owner_application.txt"));
	OwnerKey.m_pApplication = Application.c_str();
	SChannelKeys Keys = {CHANNEL_STABLE, &Ci1, 1, &OwnerKey, 1};

	EXPECT_EQ(VerifyManifestSignatures(Manifest.data(), Manifest.size(), Ci.data(), Ci.size(), Owner.data(), Owner.size(), &Keys), ESignatureResult::OK);
	EXPECT_EQ(VerifyManifestSignatures(Manifest.data(), Manifest.size(), Ci.data(), Ci.size(), "", 0, &Keys), ESignatureResult::MISSING_OWNER);
	if(!OwnerSoftware.empty())
	{
		SOwnerKey SoftwareKey;
		ASSERT_EQ(str_hex_decode(SoftwareKey.m_aKey, sizeof(SoftwareKey.m_aKey), Trim(ReadWholeFile(Dir + "/owner_software.pub")).c_str()), 0);
		SoftwareKey.m_pApplication = "";
		EXPECT_TRUE(VerifyOwnerSignature(Manifest.data(), Manifest.size(), OwnerSoftware.data(), OwnerSoftware.size(), &SoftwareKey, 1, true));
		EXPECT_FALSE(VerifyOwnerSignature(Manifest.data(), Manifest.size(), OwnerSoftware.data(), OwnerSoftware.size(), &SoftwareKey, 1, false));
	}
	SManifest Parsed;
	char aError[256] = "";
	ASSERT_TRUE(ParseManifest(Manifest.data(), Manifest.size(), "linux-x86_64", &Parsed, aError, sizeof(aError))) << aError;
	EXPECT_EQ(Decide(Parsed, Input("stable", "0.1.0", 3)), EVerdict::APPLY);
}
