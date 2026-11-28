#include <engine/shared/kog_login_wire.h>
#include <engine/shared/kog_trust.h>
#include <engine/shared/packer.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using namespace KogTrust;

// The vectors come from tools of the platform: the node certificate is the one that edgeservice (Rust) prints for the
// root "test-root-secret" (the same bytes as in BusCertTest of web2); the game server certificate, the signature of the
// challenge and the trust list are made with the same keys by the script in docs/SERVER-CERTIFICATES.md.
namespace {

const char *const CA_PUB = "f964b32aa7912988bf2f11cba9604836b47ee0dfb6c9c75e0574be78071c5757";
const char *const NODE_CERT = "04000000656467652000000039f39d51bc7ce1e7616c3223a02fc4962dcbbd041e2d965643c674dc2d32d25e02000000636100286bee000000003e0465b7bec7fedaa54b15a0c4e096cb2c079191592dc17cbeeefc4e130228906d5469b9c01db498ece3df8ae1651bdbdee388683cb8ed14a9221d2348763a07";
const char *const GS_CERT = "0a0000006b6f672d746573742d31200000002152f8d19b791d24453242e15f2eab6cb7cffa7b6a5ed30097960e069881db12040000006564676500286bee000000004b4e85542b5f427c10730b275c9ec1f0d91d845f3838a7b6932abed7edd4056b247326b397628f60f3ef376806397fae84046cf970ee001e5caf4a175cdb8909";
const char *const SERVER_SIGNATURE = "afc69056f1f074cda23a801453b126b57e32f13e0c79ee14fb22c6283301516492ac17e0889d5dbd27e3a285aaa5fa5e5008f2da029ed4c08ce13647f09aba0d";
const char *const GS_PUB = "2152f8d19b791d24453242e15f2eab6cb7cffa7b6a5ed30097960e069881db12";
const char *const BUNDLE = R"JSON({"v":1,"payload":"eyJ2IjoxLCJlbnZpcm9ubWVudCI6InByb2R1Y3Rpb24iLCJzZXEiOjcsImlzc3VlZF9hdCI6MTgwMDAwMDAwMCwiZXhwaXJlc19hdCI6MTgwMDE3MjgwMCwiYW5jaG9ycyI6eyJ2MSI6IitXU3pLcWVSS1lpL0x4SExxV0JJTnJSKzROKzJ5Y2RlQlhTK2VBY2NWMWM9In0sInJldm9rZWQiOlt7InN1YmplY3RfaWQiOiJrb2ctcmV2b2tlZC0xIiwicHVia2V5X3NoYTI1NiI6IjJkNzExNjQyYjcyNmIwNDQwMTYyN2NhOWZiYWMzMmY1Yzg1MzBmYjE5MDNjYzRkYjAyMjU4NzE3OTIxYTQ4ODEiLCJzaW5jZSI6MTc5OTk5MDAwMH1dfQ==","sig":{"v1":"EsjzaL56aaTBaPPfcHS0kUgukp0Sfp2XvmmCEsnba0vSEo7YsCYS8pSGXHB8m4aK8EXv7IHRsAWf/PUgkS7aDw=="}})JSON";
const char *const ADDRESS = "203.0.113.7:8303";
const uint64_t NOW = 1800000100;

std::vector<uint8_t> Hex(const std::string &Text)
{
	std::vector<uint8_t> Out;
	for(size_t i = 0; i + 1 < Text.size(); i += 2)
		Out.push_back((uint8_t)std::stoi(Text.substr(i, 2), nullptr, 16));
	return Out;
}

SAnchors Anchors()
{
	const char *apKeys[] = {"", CA_PUB};
	return AnchorsFromHex(apKeys, 2);
}

std::vector<uint8_t> Chain()
{
	std::vector<uint8_t> Chain = Hex(NODE_CERT);
	const std::vector<uint8_t> Gs = Hex(GS_CERT);
	Chain.insert(Chain.end(), Gs.begin(), Gs.end());
	return Chain;
}

struct SChallengeFields
{
	uint8_t m_aNonce[32];
	uint8_t m_aSession[16];
	uint8_t m_aSignature[64];
	SChallengeFields()
	{
		for(int i = 0; i < 32; i++)
			m_aNonce[i] = (uint8_t)i;
		for(int i = 0; i < 16; i++)
			m_aSession[i] = (uint8_t)(16 + i);
		const std::vector<uint8_t> Sig = Hex(SERVER_SIGNATURE);
		memcpy(m_aSignature, Sig.data(), 64);
	}
};

} // namespace

TEST(KogTrust, ParsesTheNodeCertificateOfTheRustVector)
{
	const std::vector<uint8_t> Wire = Hex(NODE_CERT);
	SCert Cert;
	size_t Used = 0;
	ASSERT_TRUE(ParseCert(Wire.data(), Wire.size(), &Cert, &Used));
	EXPECT_EQ(Used, Wire.size());
	EXPECT_EQ(Cert.m_Subject, "edge");
	EXPECT_EQ(Cert.m_Issuer, "ca");
	EXPECT_EQ(Cert.m_NotAfter, 4000000000u);
}

TEST(KogTrust, TheChainVerifiesUnderThePinnedKey)
{
	const std::vector<uint8_t> Wire = Chain();
	SCert Game;
	EXPECT_EQ(VerifyChain(Anchors(), Wire.data(), Wire.size(), NOW, &Game), EChain::OK);
	EXPECT_EQ(Game.m_Subject, "kog-test-1");
	const std::vector<uint8_t> Pub = Hex(GS_PUB);
	EXPECT_EQ(memcmp(Game.m_aPubkey, Pub.data(), 32), 0);
}

TEST(KogTrust, NothingPinnedIsNotAnAnswer)
{
	const std::vector<uint8_t> Wire = Chain();
	SCert Game;
	const char *apKeys[] = {"", "zz", "00"};
	EXPECT_EQ(VerifyChain(AnchorsFromHex(apKeys, 3), Wire.data(), Wire.size(), NOW, &Game), EChain::NO_ANCHOR);
}

TEST(KogTrust, AnotherKeyDoesNotVerifyTheChain)
{
	const std::vector<uint8_t> Wire = Chain();
	SCert Game;
	const char *apKeys[] = {"2152f8d19b791d24453242e15f2eab6cb7cffa7b6a5ed30097960e069881db12"};
	EXPECT_EQ(VerifyChain(AnchorsFromHex(apKeys, 1), Wire.data(), Wire.size(), NOW, &Game), EChain::NODE_NOT_BY_ANCHOR);
}

TEST(KogTrust, ATamperedCertificateFails)
{
	for(size_t Position : {10u, 40u, 100u})
	{
		std::vector<uint8_t> Wire = Chain();
		Wire[Position] ^= 1;
		SCert Game;
		EXPECT_NE(VerifyChain(Anchors(), Wire.data(), Wire.size(), NOW, &Game), EChain::OK) << Position;
	}
	// the game server certificate on its own, or twice, is not a chain
	const std::vector<uint8_t> Gs = Hex(GS_CERT);
	SCert Game;
	EXPECT_NE(VerifyChain(Anchors(), Gs.data(), Gs.size(), NOW, &Game), EChain::OK);
	std::vector<uint8_t> Cut = Chain();
	Cut.pop_back();
	EXPECT_EQ(VerifyChain(Anchors(), Cut.data(), Cut.size(), NOW, &Game), EChain::MALFORMED);
	std::vector<uint8_t> Long = Chain();
	Long.push_back(0);
	EXPECT_EQ(VerifyChain(Anchors(), Long.data(), Long.size(), NOW, &Game), EChain::MALFORMED);
}

TEST(KogTrust, ExpiryWithADayOfSkew)
{
	const std::vector<uint8_t> Wire = Chain();
	SCert Game;
	EXPECT_EQ(VerifyChain(Anchors(), Wire.data(), Wire.size(), 4000000000u + 3600, &Game), EChain::OK);
	EXPECT_EQ(VerifyChain(Anchors(), Wire.data(), Wire.size(), 4000000000u + CLOCK_SKEW_SECONDS + 1, &Game), EChain::EXPIRED);
}

TEST(KogTrust, TheServerSignatureIsBoundToAddressNonceSessionAndTime)
{
	const SChallengeFields F;
	uint8_t aKey[32];
	memcpy(aKey, Hex(GS_PUB).data(), 32);
	uint8_t aSig[64];
	memcpy(aSig, F.m_aSignature, 64);
	EXPECT_TRUE(VerifyServerSignature(aKey, ADDRESS, F.m_aNonce, 32, F.m_aSession, 30000, aSig));
	EXPECT_FALSE(VerifyServerSignature(aKey, "203.0.113.7:8304", F.m_aNonce, 32, F.m_aSession, 30000, aSig)) << "relayed from another address";
	EXPECT_FALSE(VerifyServerSignature(aKey, ADDRESS, F.m_aNonce, 32, F.m_aSession, 30001, aSig));
	uint8_t aOther[32];
	memcpy(aOther, F.m_aNonce, 32);
	aOther[0] ^= 1;
	EXPECT_FALSE(VerifyServerSignature(aKey, ADDRESS, aOther, 32, F.m_aSession, 30000, aSig));
	uint8_t aSession[16];
	memcpy(aSession, F.m_aSession, 16);
	aSession[15] ^= 1;
	EXPECT_FALSE(VerifyServerSignature(aKey, ADDRESS, F.m_aNonce, 32, aSession, 30000, aSig));
	uint8_t aBad[64];
	memcpy(aBad, aSig, 64);
	aBad[3] ^= 1;
	EXPECT_FALSE(VerifyServerSignature(aKey, ADDRESS, F.m_aNonce, 32, F.m_aSession, 30000, aBad));
}

TEST(KogTrust, BuildsTheSignedBytes)
{
	const SChallengeFields F;
	uint8_t aOut[256];
	const size_t Size = BuildServerSigned(aOut, sizeof(aOut), ADDRESS, F.m_aNonce, 32, F.m_aSession, 30000);
	ASSERT_EQ(Size, 14u + 1 + 16 + 32 + 16 + 4);
	EXPECT_EQ(memcmp(aOut, "KoG-server-v1", 14), 0); // with the 0 byte
	EXPECT_EQ(aOut[14], 16);
	EXPECT_EQ(aOut[Size - 4], 0);
	EXPECT_EQ(aOut[Size - 2], 0x75);
	EXPECT_EQ(aOut[Size - 1], 0x30);
	EXPECT_EQ(BuildServerSigned(aOut, sizeof(aOut), "", F.m_aNonce, 32, F.m_aSession, 30000), 0u);
	EXPECT_EQ(BuildServerSigned(aOut, 20, ADDRESS, F.m_aNonce, 32, F.m_aSession, 30000), 0u);
	EXPECT_EQ(BuildServerSigned(aOut, sizeof(aOut), ADDRESS, F.m_aNonce, 31, F.m_aSession, 30000), 0u);
}

TEST(KogTrust, TheTrustListIsReadAndRevocationsApply)
{
	SBundle Bundle;
	ASSERT_EQ(ParseBundle(Anchors(), "production", BUNDLE, strlen(BUNDLE), NOW, 0, &Bundle), EBundle::OK);
	EXPECT_EQ(Bundle.m_Seq, 7);
	ASSERT_EQ(Bundle.m_Revoked.size(), 1u);
	EXPECT_EQ(Bundle.m_Revoked[0].m_Subject, "kog-revoked-1");

	SCert Game;
	const std::vector<uint8_t> Wire = Chain();
	ASSERT_EQ(VerifyChain(Anchors(), Wire.data(), Wire.size(), NOW, &Game), EChain::OK);
	EXPECT_FALSE(IsRevoked(Bundle, Game));
	Bundle.m_Revoked.push_back({"kog-test-1", ""});
	EXPECT_TRUE(IsRevoked(Bundle, Game));
	Bundle.m_Revoked.clear();
	// by the hash of the key, whatever the subject
	Bundle.m_Revoked.push_back({"", "3097e2dee2cb4a34b53840cdb705aed71067c36f68db0e0f559c3f3fa043315f"});
	EXPECT_TRUE(IsRevoked(Bundle, Game));
}

TEST(KogTrust, TheTrustListRefusals)
{
	SBundle Bundle;
	const size_t Size = strlen(BUNDLE);
	EXPECT_EQ(ParseBundle(Anchors(), "canary", BUNDLE, Size, NOW, 0, &Bundle), EBundle::WRONG_ENVIRONMENT);
	EXPECT_EQ(ParseBundle(Anchors(), "production", BUNDLE, Size, 1800172801, 0, &Bundle), EBundle::EXPIRED);
	EXPECT_EQ(ParseBundle(Anchors(), "production", BUNDLE, Size, NOW, 8, &Bundle), EBundle::NOT_NEWER);
	EXPECT_EQ(ParseBundle(Anchors(), "production", BUNDLE, Size, NOW, 7, &Bundle), EBundle::OK);
	const char *apOther[] = {"2152f8d19b791d24453242e15f2eab6cb7cffa7b6a5ed30097960e069881db12"};
	EXPECT_EQ(ParseBundle(AnchorsFromHex(apOther, 1), "production", BUNDLE, Size, NOW, 0, &Bundle), EBundle::BAD_SIGNATURE);
	const SAnchors None;
	EXPECT_EQ(ParseBundle(None, "production", BUNDLE, Size, NOW, 0, &Bundle), EBundle::NO_ANCHOR);
	EXPECT_EQ(ParseBundle(Anchors(), "production", "", 0, NOW, 0, &Bundle), EBundle::MALFORMED);
	EXPECT_EQ(ParseBundle(Anchors(), "production", "{}", 2, NOW, 0, &Bundle), EBundle::MALFORMED);
	// a changed payload: the signature no longer fits
	std::string Changed = BUNDLE;
	const size_t At = Changed.find("payload\":\"") + 12;
	Changed[At] = Changed[At] == 'A' ? 'B' : 'A';
	EXPECT_NE(ParseBundle(Anchors(), "production", Changed.c_str(), Changed.size(), NOW, 0, &Bundle), EBundle::OK);
}

TEST(KogTrust, AListThatIsTooOldIsNotUsed)
{
	SBundle Bundle;
	ASSERT_EQ(ParseBundle(Anchors(), "production", BUNDLE, strlen(BUNDLE), NOW, 0, &Bundle), EBundle::OK);
	EXPECT_TRUE(BundleUsable(Bundle, (int64_t)NOW - 60, NOW));
	EXPECT_TRUE(BundleUsable(Bundle, (int64_t)NOW - MAX_BUNDLE_AGE_SECONDS, NOW));
	EXPECT_FALSE(BundleUsable(Bundle, (int64_t)NOW - MAX_BUNDLE_AGE_SECONDS - 1, NOW));
	EXPECT_FALSE(BundleUsable(Bundle, 0, NOW));
	EXPECT_FALSE(BundleUsable(Bundle, (int64_t)NOW + 60, NOW)) << "fetched in the future: the clock went back";
	EXPECT_FALSE(BundleUsable(Bundle, (int64_t)NOW - 60, 1800172801));
}

TEST(KogTrust, Base64)
{
	uint8_t aOut[16];
	EXPECT_EQ(Base64Decode(aOut, sizeof(aOut), "TWFu", 4), 3);
	EXPECT_EQ(Base64Decode(aOut, sizeof(aOut), "TWE=", 4), 2);
	EXPECT_EQ(Base64Decode(aOut, sizeof(aOut), "TQ==", 4), 1);
	EXPECT_EQ(Base64Decode(aOut, sizeof(aOut), "TQ=", 3), -1);
	EXPECT_EQ(Base64Decode(aOut, sizeof(aOut), "T=Q=", 4), -1);
	EXPECT_EQ(Base64Decode(aOut, sizeof(aOut), "TW-u", 4), -1);
	EXPECT_EQ(Base64Decode(aOut, 2, "TWFu", 4), -1);
}

TEST(KogLoginWire, ReadsTheCertificateFieldsOfRevision2)
{
	const SChallengeFields F;
	const std::vector<uint8_t> Wire = Chain();
	CPacker Packer;
	Packer.Reset();
	Packer.AddInt(2);
	Packer.AddRaw(F.m_aNonce, 32);
	Packer.AddRaw(F.m_aSession, 16);
	Packer.AddInt(30000);
	Packer.AddInt((int)Wire.size());
	Packer.AddRaw(Wire.data(), (int)Wire.size());
	Packer.AddRaw(F.m_aSignature, 64);
	CUnpacker Unpacker;
	Unpacker.Reset(Packer.Data(), Packer.Size());
	KogLoginWire::SChallenge Challenge;
	ASSERT_TRUE(KogLoginWire::UnpackChallenge(&Unpacker, &Challenge));
	EXPECT_EQ(Challenge.m_Chain, Wire);
	EXPECT_EQ(memcmp(Challenge.m_aServerSignature, F.m_aSignature, 64), 0);
}

TEST(KogLoginWire, ARevision1ChallengeHasNoCertificate)
{
	const SChallengeFields F;
	CPacker Packer;
	Packer.Reset();
	Packer.AddInt(1);
	Packer.AddRaw(F.m_aNonce, 32);
	Packer.AddRaw(F.m_aSession, 16);
	Packer.AddInt(30000);
	CUnpacker Unpacker;
	Unpacker.Reset(Packer.Data(), Packer.Size());
	KogLoginWire::SChallenge Challenge;
	ASSERT_TRUE(KogLoginWire::UnpackChallenge(&Unpacker, &Challenge));
	EXPECT_TRUE(Challenge.m_Chain.empty());
}

TEST(KogLoginWire, ACutCertificateIsTheSameAsNone)
{
	const SChallengeFields F;
	const std::vector<uint8_t> Wire = Chain();
	for(int Declared : {0, -1, KogLoginWire::MAX_CHAIN_SIZE + 1, (int)Wire.size()})
	{
		CPacker Packer;
		Packer.Reset();
		Packer.AddInt(2);
		Packer.AddRaw(F.m_aNonce, 32);
		Packer.AddRaw(F.m_aSession, 16);
		Packer.AddInt(30000);
		Packer.AddInt(Declared);
		// the data is shorter than declared, or the signature is missing
		Packer.AddRaw(Wire.data(), (int)Wire.size() / 2);
		CUnpacker Unpacker;
		Unpacker.Reset(Packer.Data(), Packer.Size());
		KogLoginWire::SChallenge Challenge;
		ASSERT_TRUE(KogLoginWire::UnpackChallenge(&Unpacker, &Challenge)) << Declared;
		EXPECT_TRUE(Challenge.m_Chain.empty()) << Declared;
	}
}
