#include <base/str.h>

#include <engine/external/monocypher/monocypher-ed25519.h>
#include <engine/shared/json.h>
#include <engine/shared/kog_master.h>

#include "kog_master_vector.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace KogMaster;

namespace
{
constexpr int64_t NOW = 1795501000;

struct SKeys
{
	unsigned char m_aRootSecretA[64], m_aRootPublicA[32];
	unsigned char m_aRootSecretB[64], m_aRootPublicB[32];
	unsigned char m_aDelegatedSecret[64], m_aDelegatedPublic[32];
	SKeys()
	{
		unsigned char aSeed[32];
		memset(aSeed, 1, sizeof(aSeed));
		crypto_ed25519_key_pair(m_aRootSecretA, m_aRootPublicA, aSeed);
		memset(aSeed, 2, sizeof(aSeed));
		crypto_ed25519_key_pair(m_aRootSecretB, m_aRootPublicB, aSeed);
		memset(aSeed, 3, sizeof(aSeed));
		crypto_ed25519_key_pair(m_aDelegatedSecret, m_aDelegatedPublic, aSeed);
	}
};

std::string Hex(const unsigned char *pData, size_t Size)
{
	std::string Out;
	for(size_t i = 0; i < Size; i++)
	{
		char aByte[3];
		str_format(aByte, sizeof(aByte), "%02x", pData[i]);
		Out += aByte;
	}
	return Out;
}

std::string JsonString(const std::string &Raw)
{
	std::string Out = "\"";
	for(char c : Raw)
	{
		if(c == '"' || c == '\\')
			Out += '\\';
		Out += c;
	}
	return Out + "\"";
}

std::string Sign(const unsigned char *pSecret, const char *pPrefix, const std::string &Payload)
{
	const std::string Message = std::string(pPrefix) + Payload;
	unsigned char aSignature[64];
	crypto_ed25519_sign(aSignature, pSecret, reinterpret_cast<const unsigned char *>(Message.data()), Message.size());
	return Hex(aSignature, sizeof(aSignature));
}

struct SParts
{
	std::string m_Env = "dev";
	std::string m_ListEnv = "dev";
	int64_t m_NotBefore = 1795000000;
	int64_t m_NotAfter = 1797592000;
	int64_t m_DelegationSeq = 1;
	int64_t m_Seq = 1795500000;
	int64_t m_ListNotAfter = 1795503600;
	std::string m_Masters = R"({"t":"kog","u":"https://master.client.dev.kog.tw/v1/dev/servers"},{"t":"kog","u":"https://client.kog.tw/list/dev/servers.json"},{"t":"ddnet-json","u":"https://master1.ddnet.org/ddnet/15/servers.json"})";
	std::string m_Servers = R"([{"id":"a","addresses":["tw-0.6+udp://203.0.113.10:8303"],"name":"KoG A","map":"Kobra","players":1,"max":64,"community":"kog","version":"0.6.5"}])";
	bool m_RootB = false;
	bool m_TamperList = false;
};

std::string Build(const SKeys &Keys, const SParts &P)
{
	const std::string Kid = Hex(Keys.m_aDelegatedPublic, 4);
	const std::string Pub = Hex(Keys.m_aDelegatedPublic, 32);
	const std::string Root = Hex(P.m_RootB ? Keys.m_aRootPublicB : Keys.m_aRootPublicA, 4);
	const char *aKid = Kid.c_str();
	const char *aPub = Pub.c_str();
	const char *aRoot = Root.c_str();
	char aBuf[1024];
	str_format(aBuf, sizeof(aBuf), R"({"v":1,"env":"%s","kid":"%s","pub":"%s","not_before":%lld,"not_after":%lld,"seq":%lld})", P.m_Env.c_str(), aKid, aPub, (long long)P.m_NotBefore, (long long)P.m_NotAfter, (long long)P.m_DelegationSeq);
	const std::string Delegation = aBuf;
	std::string List = std::string(R"({"v":1,"env":")") + P.m_ListEnv + R"(","seq":)" + std::to_string(P.m_Seq) + R"(,"issued":)" + std::to_string(P.m_Seq) + R"(,"not_after":)" + std::to_string(P.m_ListNotAfter) + R"(,"kid":")" + aKid + R"(","masters":[)" + P.m_Masters + R"(],"servers":)" + P.m_Servers + "}";
	const std::string ListSig = Sign(Keys.m_aDelegatedSecret, "kog-master-list-v1\n", List);
	if(P.m_TamperList)
		List.replace(List.find("KoG A"), 5, "KoG B");
	const std::string DelegationSig = Sign(P.m_RootB ? Keys.m_aRootSecretB : Keys.m_aRootSecretA, "kog-master-delegation-v1\n", Delegation);
	return std::string(R"({"v":1,"delegation":{"payload":)") + JsonString(Delegation) + R"(,"root":")" + aRoot + R"(","sig":")" + DelegationSig + R"("},"list":{"payload":)" + JsonString(List) + R"(,"sig":")" + ListSig + R"("}})";
}

EResult Check(const SKeys &Keys, const std::string &Body, const char *pChannel = "dev", int64_t Now = NOW, int64_t LastD = 0, int64_t LastL = 0, SList *pOut = nullptr)
{
	KogUpdate::SPublicKey aRoots[2];
	memcpy(aRoots[0].m_aKey, Keys.m_aRootPublicA, 32);
	memcpy(aRoots[1].m_aKey, Keys.m_aRootPublicB, 32);
	SVerifyInput Input;
	Input.m_pChannel = pChannel;
	Input.m_pRoots = aRoots;
	Input.m_NumRoots = 2;
	Input.m_Now = Now;
	Input.m_LastDelegationSeq = LastD;
	Input.m_LastListSeq = LastL;
	SList Out;
	return Verify(Body.data(), Body.size(), Input, pOut ? pOut : &Out);
}
}

TEST(KogMaster, BuiltDocumentVerifies)
{
	SKeys Keys;
	SList List;
	ASSERT_EQ(Check(Keys, Build(Keys, SParts()), "dev", NOW, 0, 0, &List), EResult::OK);
	EXPECT_EQ(List.m_vServers.size(), 1u);
	EXPECT_EQ(List.m_vServers[0].m_Name, "KoG A");
	EXPECT_EQ(List.m_vMasters.size(), 2u);
	EXPECT_EQ(List.m_vDdnetFallbacks.size(), 1u);
	EXPECT_EQ(List.m_Seq, 1795500000);
}

TEST(KogMaster, SecondRootKeyWorks)
{
	SKeys Keys;
	SParts P;
	P.m_RootB = true;
	EXPECT_EQ(Check(Keys, Build(Keys, P)), EResult::OK);
}

TEST(KogMaster, TamperedListIsRejected)
{
	SKeys Keys;
	SParts P;
	P.m_TamperList = true;
	EXPECT_EQ(Check(Keys, Build(Keys, P)), EResult::BAD_LIST_SIGNATURE);
}

TEST(KogMaster, UnknownRootIsRejected)
{
	SKeys Keys;
	std::string Body = Build(Keys, SParts());
	const size_t Pos = Body.find("\"root\":\"") + 8;
	Body[Pos] = Body[Pos] == '0' ? '1' : '0';
	EXPECT_EQ(Check(Keys, Body), EResult::BAD_ROOT);
}

TEST(KogMaster, NoKeysMeansOff)
{
	SKeys Keys;
	SVerifyInput Input;
	SList Out;
	const std::string Body = Build(Keys, SParts());
	EXPECT_EQ(Verify(Body.data(), Body.size(), Input, &Out), EResult::NO_KEYS);
}

TEST(KogMaster, WrongChannel)
{
	SKeys Keys;
	EXPECT_EQ(Check(Keys, Build(Keys, SParts()), "stable"), EResult::WRONG_CHANNEL);
	SParts P;
	P.m_ListEnv = "stable";
	EXPECT_EQ(Check(Keys, Build(Keys, P)), EResult::BAD_LIST);
}

TEST(KogMaster, Expiry)
{
	SKeys Keys;
	const std::string Body = Build(Keys, SParts());
	EXPECT_EQ(Check(Keys, Body, "dev", 1795503600 + CLOCK_SKEW_SECONDS), EResult::OK);
	EXPECT_EQ(Check(Keys, Body, "dev", 1795503600 + CLOCK_SKEW_SECONDS + 1), EResult::LIST_EXPIRED);
	EXPECT_EQ(Check(Keys, Body, "dev", 1797592000 + CLOCK_SKEW_SECONDS + 1), EResult::DELEGATION_EXPIRED);
	EXPECT_EQ(Check(Keys, Body, "dev", 1795000000 - CLOCK_SKEW_SECONDS - 1), EResult::DELEGATION_EXPIRED);
}

TEST(KogMaster, StaleListIsUsableFromTheCacheOnly)
{
	SKeys Keys;
	const std::string Body = Build(Keys, SParts());
	KogUpdate::SPublicKey Root;
	memcpy(Root.m_aKey, Keys.m_aRootPublicA, 32);
	SVerifyInput Input;
	Input.m_pChannel = "dev";
	Input.m_pRoots = &Root;
	Input.m_NumRoots = 1;
	Input.m_Now = 1795503600 + CLOCK_SKEW_SECONDS + 3 * 24 * 3600;
	SList Out;
	EXPECT_EQ(Verify(Body.data(), Body.size(), Input, &Out), EResult::LIST_EXPIRED);
	Input.m_AllowStale = true;
	EXPECT_EQ(Verify(Body.data(), Body.size(), Input, &Out), EResult::OK);
	Input.m_Now = 1795503600 + CLOCK_SKEW_SECONDS + MAX_STALE_SECONDS + 1;
	EXPECT_EQ(Verify(Body.data(), Body.size(), Input, &Out), EResult::LIST_EXPIRED);
}

TEST(KogMaster, Rollback)
{
	SKeys Keys;
	const std::string Body = Build(Keys, SParts());
	EXPECT_EQ(Check(Keys, Body, "dev", NOW, 1, 1795500000), EResult::OK);
	EXPECT_EQ(Check(Keys, Body, "dev", NOW, 1, 1795500001), EResult::LIST_ROLLBACK);
	EXPECT_EQ(Check(Keys, Body, "dev", NOW, 2, 0), EResult::DELEGATION_ROLLBACK);
}

TEST(KogMaster, StaleCopiesAreIgnoredNotRejected)
{
	// A cache of the CDN may serve seq 104 after 105 inside its TTL: the client keeps what it has and does not try the next source
	SKeys Keys;
	SParts Newer;
	Newer.m_Seq = 1795500105;
	SParts Older;
	Older.m_Seq = 1795500104;
	SList List;
	ASSERT_EQ(Check(Keys, Build(Keys, Newer), "dev", NOW, 1, 0, &List), EResult::OK);
	const int64_t Seen = List.m_Seq;
	const EResult Result = Check(Keys, Build(Keys, Older), "dev", NOW, 1, Seen);
	EXPECT_EQ(Result, EResult::LIST_ROLLBACK);
	EXPECT_EQ(Classify(Result), EOutcome::KEEP_CURRENT);
	EXPECT_EQ(Classify(Check(Keys, Build(Keys, SParts()), "dev", NOW, 2, 0)), EOutcome::KEEP_CURRENT);
	// everything else that is wrong is still a failure and moves on to the next source
	EXPECT_EQ(Classify(EResult::BAD_LIST_SIGNATURE), EOutcome::REJECT);
	EXPECT_EQ(Classify(EResult::LIST_EXPIRED), EOutcome::REJECT);
	EXPECT_EQ(Classify(EResult::WRONG_CHANNEL), EOutcome::REJECT);
	EXPECT_EQ(Classify(EResult::MALFORMED), EOutcome::REJECT);
	EXPECT_EQ(Classify(EResult::OK), EOutcome::ACCEPT);
	// a copy that is really old is refused by its expiry, however its seq compares
	EXPECT_EQ(Check(Keys, Build(Keys, Older), "dev", 1795503600 + CLOCK_SKEW_SECONDS + 1, 1, Seen), EResult::LIST_EXPIRED);
}

TEST(KogMaster, DelegationLimits)
{
	SKeys Keys;
	SParts P;
	P.m_NotAfter = P.m_NotBefore + MAX_DELEGATION_SECONDS + 1;
	EXPECT_EQ(Check(Keys, Build(Keys, P)), EResult::BAD_DELEGATION);
	P = SParts();
	P.m_NotAfter = P.m_NotBefore;
	EXPECT_EQ(Check(Keys, Build(Keys, P)), EResult::BAD_DELEGATION);
}

TEST(KogMaster, ForeignMastersAreDropped)
{
	SKeys Keys;
	SParts P;
	P.m_Masters = R"({"t":"kog","u":"https://evil.example/list"},{"t":"kog","u":"http://master.client.dev.kog.tw/x"},{"t":"kog","u":"https://kog.tw.evil.example/x"},{"t":"kog","u":"https://master.client.dev.kog.tw/v1/dev/servers"},)"
		R"({"t":"ddnet-json","u":"https://evil.example/ddnet/15/servers.json"},{"t":"ddnet-json","u":"https://master1.ddnet.org.evil.example/ddnet/15/servers.json"},{"t":"ddnet-json","u":"http://master1.ddnet.org/ddnet/15/servers.json"},{"t":"ddnet-json","u":"https://master2.ddnet.org/ddnet/15/servers.json"})";
	SList List;
	ASSERT_EQ(Check(Keys, Build(Keys, P), "dev", NOW, 0, 0, &List), EResult::OK);
	ASSERT_EQ(List.m_vMasters.size(), 1u);
	EXPECT_EQ(List.m_vMasters[0], "https://master.client.dev.kog.tw/v1/dev/servers");
	ASSERT_EQ(List.m_vDdnetFallbacks.size(), 1u);
	EXPECT_EQ(List.m_vDdnetFallbacks[0], "https://master2.ddnet.org/ddnet/15/servers.json");
	P.m_Masters = R"({"t":"ddnet-json","u":"https://master1.ddnet.org/other"},{"t":"kog","u":"https://master2.ddnet.org/ddnet/15/servers.json"},{"t":"other","u":"https://client.kog.tw/x"})";
	ASSERT_EQ(Check(Keys, Build(Keys, P), "dev", NOW, 0, 0, &List), EResult::OK);
	EXPECT_TRUE(List.m_vMasters.empty());
	EXPECT_TRUE(List.m_vDdnetFallbacks.empty());
	P.m_Masters = R"("https://client.kog.tw/x")";
	EXPECT_EQ(Check(Keys, Build(Keys, P)), EResult::BAD_LIST);
}

TEST(KogMaster, BadServersAreRejected)
{
	SKeys Keys;
	const char *apBad[] = {
		R"([{"id":"a","addresses":["tcp://1.2.3.4:1"],"name":"x","map":"m","players":1,"max":2,"community":"kog","version":"1"}])",
		R"([{"id":"a","addresses":["tw-0.6+udp://1.2.3.4:8303"],"name":"x","map":"m","players":3,"max":2,"community":"kog","version":"1"}])",
		R"([{"id":"a","addresses":["tw-0.6+udp://1.2.3.4:8303"],"name":"x","map":"m","players":1,"max":2,"community":"other","version":"1"}])",
		R"([{"id":"a","addresses":[],"name":"x","map":"m","players":1,"max":2,"community":"kog","version":"1"}])",
		R"([{"id":"a","addresses":["tw-0.6+udp://1.2.3.4:8303"],"name":"<b>","map":"m","players":1,"max":2,"community":"kog","version":"1"}])",
		R"({"a":1})",
	};
	for(const char *pBad : apBad)
	{
		SParts P;
		P.m_Servers = pBad;
		EXPECT_EQ(Check(Keys, Build(Keys, P)), EResult::BAD_LIST) << pBad;
	}
}

TEST(KogMaster, SizeAndGarbage)
{
	SKeys Keys;
	EXPECT_EQ(Check(Keys, ""), EResult::TOO_BIG);
	EXPECT_EQ(Check(Keys, std::string(MAX_DOCUMENT_SIZE + 1, 'a')), EResult::TOO_BIG);
	EXPECT_EQ(Check(Keys, "not json"), EResult::MALFORMED);
	EXPECT_EQ(Check(Keys, "{}"), EResult::MALFORMED);
	EXPECT_EQ(Check(Keys, R"({"v":2})"), EResult::MALFORMED);
}

TEST(KogMaster, Urls)
{
	EXPECT_TRUE(UrlAllowed("https://master.client.dev.kog.tw/v1/dev/servers", ""));
	EXPECT_TRUE(UrlAllowed("https://client.kog.tw/list/dev/servers.json", ""));
	EXPECT_FALSE(UrlAllowed("http://master.client.dev.kog.tw/x", ""));
	EXPECT_FALSE(UrlAllowed("https://master.client.dev.kog.tw:8443/x", ""));
	EXPECT_FALSE(UrlAllowed("http://127.0.0.1:8080/v1/dev/servers", ""));
	EXPECT_FALSE(UrlAllowed("https://kog.tw@evil.example/x", ""));
	EXPECT_TRUE(UrlAllowed("http://127.0.0.1:8080/v1/dev/servers", "http://127.0.0.1:8080/v1/dev/servers"));
	EXPECT_FALSE(UrlAllowed("http://127.0.0.1:9999/v1/dev/servers", "http://127.0.0.1:8080/v1/dev/servers"));
}

TEST(KogMaster, State)
{
	const SState State{3, 99};
	const std::string Text = SerializeState("dev", State);
	EXPECT_EQ(ParseState(Text.c_str(), "dev").m_ListSeq, 99);
	EXPECT_EQ(ParseState(Text.c_str(), "dev").m_DelegationSeq, 3);
	EXPECT_EQ(ParseState(Text.c_str(), "stable").m_ListSeq, 0);
	EXPECT_EQ(ParseState("garbage", "dev").m_ListSeq, 0);
	EXPECT_EQ(ParseState(nullptr, "dev").m_ListSeq, 0);
}

// The document made by the python generator, signed with the seeds 01.., 02.. and 03..
TEST(KogMaster, CrossLanguageVector)
{
	const std::string Text = KOG_MASTER_VECTOR;
	json_value *pVector = JsonParse(Text.c_str(), Text.size());
	ASSERT_NE(pVector, nullptr);
	const json_value &Document = (*pVector)["document"];
	// Re-serialise the document part by hand: the payloads are strings
	const std::string Body = std::string(R"({"v":1,"delegation":{"payload":)") + JsonString((const char *)Document["delegation"]["payload"]) + R"(,"root":")" + (const char *)Document["delegation"]["root"] + R"(","sig":")" + (const char *)Document["delegation"]["sig"] + R"("},"list":{"payload":)" + JsonString((const char *)Document["list"]["payload"]) + R"(,"sig":")" + (const char *)Document["list"]["sig"] + R"("}})";
	KogUpdate::SPublicKey aRoots[2];
	ASSERT_EQ(str_hex_decode(aRoots[0].m_aKey, 32, (const char *)(*pVector)["root_a_pub"]), 0);
	ASSERT_EQ(str_hex_decode(aRoots[1].m_aKey, 32, (const char *)(*pVector)["root_b_pub"]), 0);
	SVerifyInput Input;
	Input.m_pChannel = "dev";
	Input.m_pRoots = aRoots;
	Input.m_NumRoots = 2;
	Input.m_Now = (int64_t)(*pVector)["now"];
	SList List;
	EXPECT_EQ(Verify(Body.data(), Body.size(), Input, &List), EResult::OK);
	EXPECT_EQ(List.m_vServers.size(), 2u);
	json_value_free(pVector);
}
