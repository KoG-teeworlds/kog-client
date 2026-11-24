#include <engine/shared/kog_hello.h>
#include <engine/shared/packer.h>
#include <engine/shared/uuid_manager.h>

#include <gtest/gtest.h>

#include <cstring>
#include <initializer_list>
#include <set>
#include <string>

static bool Unpack(const CPacker &Packer, KogHello::SServerHello *pHello)
{
	CUnpacker Unpacker;
	Unpacker.Reset(Packer.Data(), Packer.Size());
	return KogHello::UnpackServerHello(&Unpacker, pHello);
}

static void Start(CPacker &Packer, int Revision, std::initializer_list<int> Ids)
{
	Packer.Reset();
	Packer.AddInt(Revision);
	Packer.AddInt((int)Ids.size());
	for(int Id : Ids)
		Packer.AddInt(Id);
}

static std::string Uuid(const char *pName)
{
	char aBuf[UUID_MAXSTRSIZE];
	FormatUuid(CalculateUuid(pName), aBuf, sizeof(aBuf));
	return aBuf;
}

static const uint32_t LOGIN = 1u << (int)EKogCap::CLIENT_LOGIN;

TEST(KogHello, TableIsConsistent)
{
	std::set<int> Ids;
	std::set<std::string> Labels;
	for(int Cap = 0; Cap < (int)EKogCap::COUNT; Cap++)
	{
		const int Id = KogHello::WireId((EKogCap)Cap);
		EXPECT_GE(Id, 1);
		EXPECT_LE(Id, (int)KogHello::MAX_ID);
		EXPECT_TRUE(Ids.insert(Id).second) << "wire id used twice: " << Id;
		EXPECT_TRUE(Labels.insert(KogHello::Label((EKogCap)Cap)).second);
	}
	// ids are part of the wire format and are never changed
	EXPECT_EQ(KogHello::WireId(EKogCap::CLIENT_LOGIN), 1);
	EXPECT_STREQ(KogHello::Label(EKogCap::CLIENT_LOGIN), "client_login");
}

TEST(KogHello, MessageNames)
{
	EXPECT_EQ(Uuid("kog-client-hello@kog.tw"), "e2b7e697-85eb-3c11-a45b-f678d64e416d");
	EXPECT_EQ(Uuid("kog-server-hello@kog.tw"), "94ac06ac-8245-3b7f-93dc-d7f81e27a1a1");
	// the formula is the one of the messages of DDNet
	EXPECT_EQ(Uuid("capabilities@ddnet.tw"), "f621a5a1-f585-3775-8e73-41beee79f2b2");
}

TEST(KogHello, ClientHelloHasTheDocumentedBytes)
{
	CPacker Packer;
	Packer.Reset();
	KogHello::PackClientHello(&Packer, LOGIN, "1.3.0", "stable");
	// revision 1, one id, client_login, "1.3.0", "stable" (docs/KOG-HELLO.md)
	static const unsigned char s_aExpected[] = {0x01, 0x01, 0x01, '1', '.', '3', '.', '0', 0, 's', 't', 'a', 'b', 'l', 'e', 0};
	ASSERT_EQ(Packer.Size(), (int)sizeof(s_aExpected));
	EXPECT_EQ(0, memcmp(Packer.Data(), s_aExpected, sizeof(s_aExpected)));

	Packer.Reset();
	KogHello::PackClientHello(&Packer, 0, "1.3.0", "stable");
	EXPECT_EQ(Packer.Data()[1], 0) << "only what is offered is listed";
}

TEST(KogHello, LongBuildAndChannelAreCut)
{
	CPacker Packer;
	Packer.Reset();
	KogHello::PackClientHello(&Packer, LOGIN, std::string(200, 'b').c_str(), std::string(200, 'c').c_str());
	EXPECT_LE(Packer.Size(), 3 + 32 + 16);
}

TEST(KogHello, ReadsTheServerHello)
{
	CPacker Packer;
	Start(Packer, 1, {1});
	KogHello::SServerHello Hello;
	ASSERT_TRUE(Unpack(Packer, &Hello));
	EXPECT_EQ(Hello.m_Revision, 1);
	EXPECT_EQ(Hello.m_Mask, LOGIN);
	EXPECT_EQ(Hello.m_NumUnknown, 0);

	Start(Packer, 1, {});
	ASSERT_TRUE(Unpack(Packer, &Hello));
	EXPECT_EQ(Hello.m_Mask, 0u);
}

TEST(KogHello, UnknownIdsAndNewerRevisionsAreIgnored)
{
	CPacker Packer;
	Start(Packer, 9, {4000, 1, 2});
	Packer.AddString("a field of a later revision", 0);
	KogHello::SServerHello Hello;
	ASSERT_TRUE(Unpack(Packer, &Hello));
	EXPECT_EQ(Hello.m_Mask, LOGIN);
	EXPECT_EQ(Hello.m_NumUnknown, 2);
}

TEST(KogHello, MalformedServerHelloIsNotUnderstood)
{
	KogHello::SServerHello Hello;
	CPacker Packer;

	Start(Packer, 0, {1});
	EXPECT_FALSE(Unpack(Packer, &Hello)) << "revision 0";
	Start(Packer, 1, {0});
	EXPECT_FALSE(Unpack(Packer, &Hello)) << "id 0";
	Start(Packer, 1, {KogHello::MAX_ID + 1});
	EXPECT_FALSE(Unpack(Packer, &Hello)) << "id too large";
	Start(Packer, 1, {-3});
	EXPECT_FALSE(Unpack(Packer, &Hello)) << "negative id";

	Packer.Reset();
	Packer.AddInt(1);
	Packer.AddInt(KogHello::MAX_LISTED + 1);
	for(int i = 0; i < KogHello::MAX_LISTED + 1; i++)
		Packer.AddInt(1000 + i);
	EXPECT_FALSE(Unpack(Packer, &Hello)) << "too many ids";

	Packer.Reset();
	Packer.AddInt(1);
	Packer.AddInt(-1);
	EXPECT_FALSE(Unpack(Packer, &Hello)) << "negative count";

	Packer.Reset();
	Packer.AddInt(1);
	Packer.AddInt(3);
	Packer.AddInt(1);
	EXPECT_FALSE(Unpack(Packer, &Hello)) << "fewer ids than announced";

	Packer.Reset();
	EXPECT_FALSE(Unpack(Packer, &Hello)) << "nothing";
	EXPECT_EQ(Hello.m_Mask, 0u);
}

TEST(KogHello, OnlyWhatBothHaveIsUsed)
{
	EXPECT_EQ(KogHello::Agreed(LOGIN, LOGIN), LOGIN);
	EXPECT_EQ(KogHello::Agreed(LOGIN, 0), 0u) << "the server did not list it";
	EXPECT_EQ(KogHello::Agreed(0, LOGIN), 0u) << "the client did not offer it: a server cannot switch it on";
	EXPECT_EQ(KogHello::Agreed(0, 0), 0u);
}

TEST(KogHello, HelloIsOnlySentToKogServers)
{
	KogHello::SHelloDecision Decision = {};
	Decision.m_SwitchOn = true;
	Decision.m_ServerIsKog = true;
	Decision.m_MainConnection = true;
	Decision.m_Offer = LOGIN;
	EXPECT_TRUE(KogHello::MaySendHello(Decision));

	KogHello::SHelloDecision Other = Decision;
	Other.m_ServerIsKog = false;
	EXPECT_FALSE(KogHello::MaySendHello(Other)) << "vanilla and other servers get nothing";
	Other = Decision;
	Other.m_SwitchOn = false;
	EXPECT_FALSE(KogHello::MaySendHello(Other)) << "kill switch";
	Other = Decision;
	Other.m_MainConnection = false;
	EXPECT_FALSE(KogHello::MaySendHello(Other)) << "dummy";
	Other = Decision;
	Other.m_Sixup = true;
	EXPECT_FALSE(KogHello::MaySendHello(Other)) << "0.7";
	Other = Decision;
	Other.m_Offer = 0;
	EXPECT_FALSE(KogHello::MaySendHello(Other)) << "nothing to offer";
}

TEST(KogHello, FormatsAMask)
{
	char aBuf[64];
	KogHello::FormatMask(0, aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "none");
	KogHello::FormatMask(LOGIN, aBuf, sizeof(aBuf));
	EXPECT_STREQ(aBuf, "client_login");
}
