#include <engine/shared/kog_login_wire.h>
#include <engine/shared/packer.h>
#include <engine/shared/uuid_manager.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using namespace KogLoginWire;

namespace {

std::string Uuid(const char *pName)
{
	char aBuf[UUID_MAXSTRSIZE];
	FormatUuid(CalculateUuid(pName), aBuf, sizeof(aBuf));
	return aBuf;
}

void Challenge(CPacker &Packer, int Revision, int NonceBytes, int SessionBytes, int TtlMs)
{
	Packer.Reset();
	Packer.AddInt(Revision);
	const std::string Nonce(NonceBytes, 'n');
	const std::string Session(SessionBytes, 's');
	Packer.AddRaw(Nonce.data(), NonceBytes);
	Packer.AddRaw(Session.data(), SessionBytes);
	Packer.AddInt(TtlMs);
}

bool Read(const CPacker &Packer, SChallenge *pChallenge)
{
	CUnpacker Unpacker;
	Unpacker.Reset(Packer.Data(), Packer.Size());
	return UnpackChallenge(&Unpacker, pChallenge);
}

bool ReadResult(const CPacker &Packer, SResult *pResult)
{
	CUnpacker Unpacker;
	Unpacker.Reset(Packer.Data(), Packer.Size());
	return UnpackResult(&Unpacker, pResult);
}

} // namespace

TEST(KogLoginWire, MessageNames)
{
	// the same names as docs/protocol/login-proof.md of the server
	EXPECT_EQ(Uuid("kog-login-challenge@kog.tw"), "94b7086e-74f6-3900-aa26-aad134f8ecc2");
	EXPECT_EQ(Uuid("kog-login-proof@kog.tw"), "0ae97ccb-2c36-3560-acc8-d1657d4bdfa5");
	EXPECT_EQ(Uuid("kog-login-result@kog.tw"), "bab7d0ee-46be-3049-96f3-109b517969ab");
}

TEST(KogLoginWire, ReadsTheChallengeOfTheWorkedExample)
{
	// the fields of the example on the page of the server: nonce 00 .. 1f, session id 10 .. 1f, 30000 ms
	CPacker Packer;
	Packer.Reset();
	Packer.AddInt(1);
	uint8_t aNonce[NONCE_SIZE], aSession[ID_SIZE];
	for(int i = 0; i < NONCE_SIZE; i++)
		aNonce[i] = i;
	for(int i = 0; i < ID_SIZE; i++)
		aSession[i] = 0x10 + i;
	Packer.AddRaw(aNonce, sizeof(aNonce));
	Packer.AddRaw(aSession, sizeof(aSession));
	const unsigned char aTtl[] = {0xb0, 0xd4, 0x03};
	Packer.AddRaw(aTtl, sizeof(aTtl));

	SChallenge Read1;
	ASSERT_TRUE(Read(Packer, &Read1));
	EXPECT_EQ(Read1.m_Revision, 1);
	EXPECT_EQ(Read1.m_TtlMs, 30000);
	EXPECT_EQ(memcmp(Read1.m_aNonce, aNonce, sizeof(aNonce)), 0);
	EXPECT_EQ(memcmp(Read1.m_aSessionId, aSession, sizeof(aSession)), 0);
}

TEST(KogLoginWire, ANewerRevisionIsReadAndTheRestIgnored)
{
	CPacker Packer;
	Challenge(Packer, 4, 32, 16, 30000);
	Packer.AddString("a field of a later revision", 0);
	SChallenge Result;
	ASSERT_TRUE(Read(Packer, &Result));
	EXPECT_EQ(Result.m_Revision, 4);
}

TEST(KogLoginWire, MalformedChallengesAreNotSigned)
{
	SChallenge Result;
	CPacker Packer;
	Challenge(Packer, 0, 32, 16, 30000);
	EXPECT_FALSE(Read(Packer, &Result)) << "revision 0";
	Challenge(Packer, 1, 31, 16, 30000);
	EXPECT_FALSE(Read(Packer, &Result)) << "nonce too short";
	Challenge(Packer, 1, 32, 15, 30000);
	EXPECT_FALSE(Read(Packer, &Result)) << "session id too short";
	Challenge(Packer, 1, 32, 16, 0);
	EXPECT_FALSE(Read(Packer, &Result)) << "no time";
	Challenge(Packer, 1, 32, 16, MIN_TTL_MS - 1);
	EXPECT_FALSE(Read(Packer, &Result)) << "too short a time";
	Challenge(Packer, 1, 32, 16, MAX_TTL_MS + 1);
	EXPECT_FALSE(Read(Packer, &Result)) << "too long a time";
	Challenge(Packer, 1, 32, 16, -5);
	EXPECT_FALSE(Read(Packer, &Result)) << "negative time";
	Packer.Reset();
	Packer.AddInt(1);
	EXPECT_FALSE(Read(Packer, &Result)) << "only the revision";
	Packer.Reset();
	EXPECT_FALSE(Read(Packer, &Result)) << "nothing";
	EXPECT_EQ(Result.m_Revision, 0);

	Challenge(Packer, 1, 32, 16, MIN_TTL_MS);
	EXPECT_TRUE(Read(Packer, &Result));
	Challenge(Packer, 1, 32, 16, MAX_TTL_MS);
	EXPECT_TRUE(Read(Packer, &Result));
}

TEST(KogLoginWire, PacksTheProof)
{
	uint8_t aDevice[ID_SIZE], aSignature[SIGNATURE_SIZE];
	for(int i = 0; i < ID_SIZE; i++)
		aDevice[i] = 0xd0 + i;
	for(int i = 0; i < SIGNATURE_SIZE; i++)
		aSignature[i] = i;
	CPacker Packer;
	Packer.Reset();
	PackProof(&Packer, aDevice, aSignature);
	ASSERT_EQ(Packer.Size(), 1 + ID_SIZE + SIGNATURE_SIZE);
	EXPECT_EQ(Packer.Data()[0], 1);
	EXPECT_EQ(memcmp(Packer.Data() + 1, aDevice, ID_SIZE), 0);
	EXPECT_EQ(memcmp(Packer.Data() + 1 + ID_SIZE, aSignature, SIGNATURE_SIZE), 0);
}

TEST(KogLoginWire, ReadsTheResultsOfTheWorkedExample)
{
	// 01 01 "Tee" and 01 00 00, after the message id
	const unsigned char aYes[] = {0x01, 0x01, 'T', 'e', 'e', 0x00};
	const unsigned char aNo[] = {0x01, 0x00, 0x00};
	CPacker Packer;
	SResult Result;
	Packer.Reset();
	Packer.AddRaw(aYes, sizeof(aYes));
	ASSERT_TRUE(ReadResult(Packer, &Result));
	EXPECT_TRUE(Result.m_LoggedIn);
	EXPECT_STREQ(Result.m_aAccountName, "Tee");

	Packer.Reset();
	Packer.AddRaw(aNo, sizeof(aNo));
	ASSERT_TRUE(ReadResult(Packer, &Result));
	EXPECT_FALSE(Result.m_LoggedIn);
	EXPECT_STREQ(Result.m_aAccountName, "");
}

TEST(KogLoginWire, ARefusalNeverCarriesAName)
{
	CPacker Packer;
	Packer.Reset();
	Packer.AddInt(1);
	Packer.AddInt(0);
	Packer.AddString("Somebody", 0);
	SResult Result;
	ASSERT_TRUE(ReadResult(Packer, &Result));
	EXPECT_FALSE(Result.m_LoggedIn);
	EXPECT_STREQ(Result.m_aAccountName, "");
}

TEST(KogLoginWire, AResultThatIsNotOneIsNotUnderstood)
{
	CPacker Packer;
	SResult Result;
	Packer.Reset();
	Packer.AddInt(0);
	Packer.AddInt(1);
	Packer.AddString("Tee", 0);
	EXPECT_FALSE(ReadResult(Packer, &Result)) << "revision 0";
	Packer.Reset();
	Packer.AddInt(1);
	Packer.AddInt(2);
	Packer.AddString("Tee", 0);
	EXPECT_FALSE(ReadResult(Packer, &Result)) << "status 2";
	Packer.Reset();
	Packer.AddInt(1);
	Packer.AddInt(-1);
	Packer.AddString("Tee", 0);
	EXPECT_FALSE(ReadResult(Packer, &Result)) << "status -1";
	Packer.Reset();
	Packer.AddInt(1);
	Packer.AddInt(1);
	EXPECT_FALSE(ReadResult(Packer, &Result)) << "no name";
	Packer.Reset();
	EXPECT_FALSE(ReadResult(Packer, &Result)) << "nothing";
}

TEST(KogLoginWire, ALongNameIsCutAndControlCharactersAreRemoved)
{
	CPacker Packer;
	Packer.Reset();
	Packer.AddInt(1);
	Packer.AddInt(1);
	Packer.AddString(std::string(300, 'n').c_str(), 0);
	SResult Result;
	ASSERT_TRUE(ReadResult(Packer, &Result));
	EXPECT_EQ(strlen(Result.m_aAccountName), (size_t)MAX_ACCOUNT_NAME - 1);

	Packer.Reset();
	Packer.AddInt(1);
	Packer.AddInt(1);
	Packer.AddString("A\x01\x1b[31mB", 0);
	ASSERT_TRUE(ReadResult(Packer, &Result));
	for(const char *p = Result.m_aAccountName; *p; p++)
		EXPECT_GE((unsigned char)*p, 32) << "a control character is left";
}
