#include <engine/shared/kog_hello.h>
#include <engine/shared/kog_notice_wire.h>
#include <engine/shared/packer.h>
#include <engine/shared/uuid_manager.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

using namespace KogNoticeWire;
using KogNotices::EKind;

// The golden vectors of test_vectors/kog-notice.json of the protocol repository (scripts/kog_notice_vector.py),
// the same as in the test of the server: the bytes of the message and what a reader must get from them.
namespace {

struct SVector
{
	const char *m_pName;
	const char *m_pHex;
	bool m_Ok;
	int m_Kind;
	const char *m_pKey;
	const char *m_pText;
	int m_Value;
	int m_Limit;
};

const SVector gs_aVectors[] = {
	{"achievement", "018ccfa9e614cb3b64842d1088e06a065c026163683a336632613a66696e69736865723a3200416368696576656d656e7420756e6c6f636b65643a2046696e69736865722c2073696c76657220282b31353020636f696e7329000000", true, 2, "ach:3f2a:finisher:2", "Achievement unlocked: Finisher, silver (+150 coins)", 0, 0},
	{"quest completed", "018ccfa9e614cb3b64842d1088e06a065c0371756573743a6e6f68616d6d657200517565737420636f6d706c657465643a204e6f2068616d6d6572000000", true, 3, "quest:nohammer", "Quest completed: No hammer", 0, 0},
	{"quest progress", "018ccfa9e614cb3b64842d1088e06a065c0471756573743a7465616d380051756573743a205465616d206f662038000308", true, 4, "quest:team8", "Quest: Team of 8", 3, 8},
	{"maintenance with a countdown", "018ccfa9e614cb3b64842d1088e06a065c056d61696e74656e616e63650053657276657220726573746172747320696e2035206d696e7574657300ac0400", true, 5, "maintenance", "Server restarts in 5 minutes", 300, 0},
	{"warning", "018ccfa9e614cb3b64842d1088e06a065c067761726e3a3100506c65617365207374617920696e207468652072616365000000", true, 6, "warn:1", "Please stay in the race", 0, 0},
	{"tournament", "018ccfa9e614cb3b64842d1088e06a065c07746f75726e616d656e743a726f756e643200526f756e6420322073746172747320696e203330207365636f6e6473001e00", true, 7, "tournament:round2", "Round 2 starts in 30 seconds", 30, 0},
	{"non ascii text", "018ccfa9e614cb3b64842d1088e06a065c01696e666f3a310045696e20476cc3bc636b3a20e38381e383bce383a020f09f8f81000000", true, 1, "info:1", "Ein Gl\303\274ck: \343\203\201\343\203\274\343\203\240 \360\237\217\201", 0, 0},
	{"empty key", "018ccfa9e614cb3b64842d1088e06a065c010041206c696e6520776974686f75742061206b6579000000", true, 1, "", "A line without a key", 0, 0},
	{"unknown kind is shown as info", "018ccfa9e614cb3b64842d1088e06a065ca301780066726f6d2061206e6577657220736572766572000000", true, 1, "x", "from a newer server", 0, 0},
	{"kind 0 is not a notice", "018ccfa9e614cb3b64842d1088e06a065c007800706c61696e000000", true, 1, "x", "plain", 0, 0},
	{"negative numbers are 0", "018ccfa9e614cb3b64842d1088e06a065c056d0074657874004440", true, 5, "m", "text", 0, 0},
	{"key and text are cut at their limits", "018ccfa9e614cb3b64842d1088e06a065c016b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b007474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474747474000000", true, 1, "kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk", "tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt", 0, 0},
	{"control characters become spaces", "018ccfa9e614cb3b64842d1088e06a065c0163006f6e650174776f1f7468726565000000", true, 1, "c", "one two three", 0, 0},
	{"fields appended by a newer revision are ignored", "018ccfa9e614cb3b64842d1088e06a065c016e006e65776572000000076c6174657200", true, 1, "n", "newer", 0, 0},
	{"missing fields are not understood", "018ccfa9e614cb3b64842d1088e06a065c016e007465787400", false, 0, "", "", 0, 0},
	{"no text is not understood", "018ccfa9e614cb3b64842d1088e06a065c016e00000000", false, 0, "", "", 0, 0},
};

constexpr size_t HEADER_SIZE = 1 + 16; // the integer 1 for an extended message, the uuid

std::vector<unsigned char> FromHex(const char *pHex)
{
	std::vector<unsigned char> Bytes;
	for(size_t i = 0; pHex[i] != '\0' && pHex[i + 1] != '\0'; i += 2)
	{
		const char aByte[3] = {pHex[i], pHex[i + 1], '\0'};
		Bytes.push_back((unsigned char)std::strtoul(aByte, nullptr, 16));
	}
	return Bytes;
}

std::string Uuid(const char *pName)
{
	char aBuf[UUID_MAXSTRSIZE];
	FormatUuid(CalculateUuid(pName), aBuf, sizeof(aBuf));
	return aBuf;
}

}

TEST(KogNoticeWire, CapabilityAndMessageName)
{
	EXPECT_EQ(KogHello::WireId(EKogCap::NOTICES), 2);
	EXPECT_STREQ(KogHello::Label(EKogCap::NOTICES), "notices");
	EXPECT_EQ(Uuid("kog-notice@kog.tw"), "8ccfa9e6-14cb-3b64-842d-1088e06a065c");
}

TEST(KogNoticeWire, GoldenVectors)
{
	for(const SVector &Vector : gs_aVectors)
	{
		SCOPED_TRACE(Vector.m_pName);
		const std::vector<unsigned char> Bytes = FromHex(Vector.m_pHex);
		ASSERT_GT(Bytes.size(), HEADER_SIZE);
		EXPECT_EQ(Bytes[0], 1);
		CUnpacker Unpacker;
		Unpacker.Reset(Bytes.data() + HEADER_SIZE, (int)(Bytes.size() - HEADER_SIZE));
		SNotice Notice;
		ASSERT_EQ(Unpack(&Unpacker, &Notice), Vector.m_Ok);
		if(!Vector.m_Ok)
			continue;
		EXPECT_EQ((int)Notice.m_Kind, Vector.m_Kind);
		EXPECT_STREQ(Notice.m_aKey, Vector.m_pKey);
		EXPECT_STREQ(Notice.m_aText, Vector.m_pText);
		EXPECT_EQ(Notice.m_Value, Vector.m_Value);
		EXPECT_EQ(Notice.m_Limit, Vector.m_Limit);
	}
}

TEST(KogNoticeWire, TheKindsAreTheOnesOfTheWire)
{
	EXPECT_EQ((int)EKind::INFO, 1);
	EXPECT_EQ((int)EKind::ACHIEVEMENT, 2);
	EXPECT_EQ((int)EKind::QUEST, 3);
	EXPECT_EQ((int)EKind::QUEST_PROGRESS, 4);
	EXPECT_EQ((int)EKind::MAINTENANCE, 5);
	EXPECT_EQ((int)EKind::WARNING, 6);
	EXPECT_EQ((int)EKind::TOURNAMENT, 7);
	EXPECT_EQ(KogNotices::KindFromWire(0), EKind::INFO);
	EXPECT_EQ(KogNotices::KindFromWire(8), EKind::INFO);
	EXPECT_EQ(KogNotices::KindFromWire(-3), EKind::INFO);
	EXPECT_EQ(KogNotices::KindFromWire(5), EKind::MAINTENANCE);
}
