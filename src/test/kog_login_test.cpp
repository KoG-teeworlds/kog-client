#include <base/net.h>
#include <base/str.h>

#include <engine/external/monocypher/monocypher-ed25519.h>
#include <engine/shared/kog_login.h>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace KogLogin;

namespace {

std::vector<uint8_t> Hex(const char *pHex)
{
	std::vector<uint8_t> Bytes;
	for(size_t i = 0; pHex[i] && pHex[i + 1]; i += 2)
		Bytes.push_back((uint8_t)std::stoi(std::string(pHex + i, 2), nullptr, 16));
	return Bytes;
}

void Seed(const char *pHex, uint8_t (&aSeed)[SEED_SIZE])
{
	const std::vector<uint8_t> Bytes = Hex(pHex);
	ASSERT_EQ(Bytes.size(), (size_t)SEED_SIZE);
	std::copy(Bytes.begin(), Bytes.end(), aSeed);
}

// Sends the raw request to the listener on loopback and returns what the listener answered
std::string Exchange(CLoopbackListener &Listener, const std::string &Request)
{
	NETADDR Addr;
	char aAddr[64];
	str_format(aAddr, sizeof(aAddr), "127.0.0.1:%d", Listener.Port());
	EXPECT_EQ(net_addr_from_str(&Addr, aAddr), 0);
	NETADDR Bind = {};
	Bind.type = NETTYPE_IPV4;
	NETSOCKET Client = net_tcp_create(Bind);
	EXPECT_NE(Client, nullptr);
	if(!Client || net_tcp_connect(Client, &Addr) != 0)
		return "";
	net_tcp_send(Client, Request.data(), (int)Request.size());
	for(int i = 0; i < 200 && Listener.State() == CLoopbackListener::EState::LISTENING; i++)
	{
		Listener.Poll();
		if(net_socket_read_wait(Client, std::chrono::milliseconds(5)) > 0)
			break;
	}
	std::string Response;
	char aBuf[1024];
	while(net_socket_read_wait(Client, std::chrono::milliseconds(200)) > 0)
	{
		const int Bytes = net_tcp_recv(Client, aBuf, sizeof(aBuf));
		if(Bytes <= 0)
			break;
		Response.append(aBuf, Bytes);
	}
	net_tcp_close(Client);
	return Response;
}

}

TEST(KogLogin, PkceRfc7636Vector)
{
	char aChallenge[CHALLENGE_LENGTH + 1];
	CodeChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", aChallenge);
	EXPECT_STREQ(aChallenge, "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

TEST(KogLogin, VerifierAndStateAreLongRandomUnreserved)
{
	char aVerifier[VERIFIER_LENGTH + 1], aOther[VERIFIER_LENGTH + 1];
	MakeVerifier(aVerifier);
	MakeVerifier(aOther);
	EXPECT_GE(str_length(aVerifier), 43);
	EXPECT_STRNE(aVerifier, aOther);
	for(const char *p = aVerifier; *p; p++)
		EXPECT_TRUE(isalnum((unsigned char)*p) || *p == '-' || *p == '_');
}

TEST(KogLogin, Base64Url)
{
	uint8_t aData[] = {0xfb, 0xff, 0xfe, 0x01};
	char aEncoded[16];
	Base64UrlEncode(aEncoded, sizeof(aEncoded), aData, sizeof(aData));
	EXPECT_STREQ(aEncoded, "-__-AQ");
	uint8_t aDecoded[8];
	EXPECT_EQ(Base64UrlDecode(aDecoded, sizeof(aDecoded), aEncoded), 4);
	EXPECT_EQ(memcmp(aDecoded, aData, 4), 0);
	EXPECT_EQ(Base64UrlDecode(aDecoded, sizeof(aDecoded), "AQ=="), -1);
	EXPECT_EQ(Base64UrlDecode(aDecoded, sizeof(aDecoded), "A"), -1);
	EXPECT_EQ(Base64UrlDecode(aDecoded, sizeof(aDecoded), "+/"), -1);
}

TEST(KogLogin, DeviceNameIsCutToFortyCharacters)
{
	char aName[161];
	CutDeviceName(aName, sizeof(aName), "  a-very-long-host-name-that-goes-on-and-on-and-on-forever");
	EXPECT_EQ(str_length(aName), 40);
	EXPECT_EQ(aName[0], 'a');
	CutDeviceName(aName, sizeof(aName), "äöüäöüäöüäöüäöüäöüäöüäöüäöüäöüäöüäöüäöüäöüäöüäöüäöüäöü");
	size_t Size = 0, Count = 0;
	str_utf8_stats(aName, sizeof(aName), 100, &Size, &Count);
	EXPECT_EQ(Count, 40u);
}

TEST(KogLogin, AuthorizeUrlCarriesTheContract)
{
	char aUrl[1024];
	ASSERT_TRUE(BuildAuthorizeUrl(aUrl, sizeof(aUrl), "https://kog.tw/oauth/authorize", "http://127.0.0.1:50000/callback", "STATE", "CHALLENGE", "my pc"));
	EXPECT_NE(strstr(aUrl, "https://kog.tw/oauth/authorize?response_type=code"), nullptr);
	EXPECT_NE(strstr(aUrl, "client_id=kog-client"), nullptr);
	EXPECT_NE(strstr(aUrl, "redirect_uri=http%3A%2F%2F127.0.0.1%3A50000%2Fcallback"), nullptr);
	EXPECT_NE(strstr(aUrl, "scope=openid%20profile%20kog%3Alogin"), nullptr);
	EXPECT_NE(strstr(aUrl, "code_challenge_method=S256"), nullptr);
	EXPECT_NE(strstr(aUrl, "device_name=my%20pc"), nullptr);
	ASSERT_TRUE(BuildAuthorizeUrl(aUrl, sizeof(aUrl), "https://x/a?tenant=1", "http://127.0.0.1:1/callback", "S", "C", "d"));
	EXPECT_NE(strstr(aUrl, "tenant=1&response_type=code"), nullptr);
}

TEST(KogLogin, CallbackParsing)
{
	char aCode[MAX_CODE_LENGTH + 1], aError[128];
	const auto Parse = [&](const char *pRequest) {
		return ParseCallback(pRequest, str_length(pRequest), "good-state", aCode, sizeof(aCode), aError, sizeof(aError));
	};
	EXPECT_EQ(Parse("GET /callback?code=abc%2Fdef&state=good-state HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"), ECallback::CODE);
	EXPECT_STREQ(aCode, "abc/def");
	EXPECT_EQ(Parse("GET /callback?state=good-state&code=xyz HTTP/1.1\r\n\r\n"), ECallback::CODE);
	EXPECT_EQ(Parse("GET /callback?code=abc&state=bad-state HTTP/1.1\r\n\r\n"), ECallback::BAD_STATE);
	EXPECT_STREQ(aCode, "");
	EXPECT_EQ(Parse("GET /callback?code=abc HTTP/1.1\r\n\r\n"), ECallback::BAD_STATE);
	EXPECT_EQ(Parse("GET /callback?code=abc&state=good-stat HTTP/1.1\r\n\r\n"), ECallback::BAD_STATE);
	EXPECT_EQ(Parse("GET /callback?code=abc&state=good-state2 HTTP/1.1\r\n\r\n"), ECallback::BAD_STATE);
	EXPECT_EQ(Parse("GET /callback2?code=abc&state=good-state HTTP/1.1\r\n\r\n"), ECallback::WRONG_PATH);
	EXPECT_EQ(Parse("GET /callback/?code=abc&state=good-state HTTP/1.1\r\n\r\n"), ECallback::WRONG_PATH);
	EXPECT_EQ(Parse("GET /?code=abc&state=good-state HTTP/1.1\r\n\r\n"), ECallback::WRONG_PATH);
	EXPECT_EQ(Parse("POST /callback?code=abc&state=good-state HTTP/1.1\r\n\r\n"), ECallback::WRONG_METHOD);
	EXPECT_EQ(Parse("GET /callback?code=abc&state=good-state&state=good-state HTTP/1.1\r\n\r\n"), ECallback::MALFORMED);
	EXPECT_EQ(Parse("GET /callback?code=abc&code=def&state=good-state HTTP/1.1\r\n\r\n"), ECallback::MALFORMED);
	EXPECT_EQ(Parse("GET /callback?code=%zz&state=good-state HTTP/1.1\r\n\r\n"), ECallback::MALFORMED);
	EXPECT_EQ(Parse("GET /callback?code=abc&state=good-state"), ECallback::MALFORMED);
	EXPECT_EQ(Parse("garbage\r\n"), ECallback::MALFORMED);
	EXPECT_EQ(Parse("GET /callback?state=good-state HTTP/1.1\r\n\r\n"), ECallback::MISSING_CODE);
	EXPECT_EQ(Parse("GET /callback?error=access_denied&state=good-state HTTP/1.1\r\n\r\n"), ECallback::SERVER_ERROR);
	EXPECT_STREQ(aError, "access_denied");
	// an error with the wrong state is not trusted
	EXPECT_EQ(Parse("GET /callback?error=access_denied&state=other HTTP/1.1\r\n\r\n"), ECallback::BAD_STATE);
}

TEST(KogLogin, ListenerAcceptsOnlyTheFirstMatchingRequest)
{
	CLoopbackListener Listener;
	ASSERT_TRUE(Listener.Start("the-state", 10000));
	EXPECT_EQ(Listener.State(), CLoopbackListener::EState::LISTENING);
	EXPECT_GE(Listener.Port(), 49152);
	char aRedirect[64];
	Listener.RedirectUri(aRedirect, sizeof(aRedirect));
	EXPECT_TRUE(str_startswith(aRedirect, "http://127.0.0.1:") || str_startswith(aRedirect, "http://[::1]:"));
	EXPECT_TRUE(str_endswith(aRedirect, "/callback"));

	// wrong path and wrong state are answered and refused, the listener keeps waiting
	EXPECT_NE(Exchange(Listener, "GET /other?code=x&state=the-state HTTP/1.1\r\n\r\n").find("404"), std::string::npos);
	EXPECT_EQ(Listener.State(), CLoopbackListener::EState::LISTENING);
	EXPECT_NE(Exchange(Listener, "GET /callback?code=x&state=nope HTTP/1.1\r\n\r\n").find("400"), std::string::npos);
	EXPECT_EQ(Listener.State(), CLoopbackListener::EState::LISTENING);

	const std::string Response = Exchange(Listener, "GET /callback?code=the-code&state=the-state HTTP/1.1\r\nHost: x\r\n\r\n");
	EXPECT_NE(Response.find("200 OK"), std::string::npos);
	EXPECT_NE(Response.find("You can close this tab"), std::string::npos);
	EXPECT_EQ(Listener.State(), CLoopbackListener::EState::GOT_CODE);
	EXPECT_STREQ(Listener.Code(), "the-code");

	// then it is closed: a second request cannot connect and the code stays the first one
	NETADDR Addr;
	char aAddr[64];
	str_format(aAddr, sizeof(aAddr), "127.0.0.1:%d", Listener.Port());
	ASSERT_EQ(net_addr_from_str(&Addr, aAddr), 0);
	NETADDR Bind = {};
	Bind.type = NETTYPE_IPV4;
	NETSOCKET Second = net_tcp_create(Bind);
	ASSERT_NE(Second, nullptr);
	EXPECT_NE(net_tcp_connect(Second, &Addr), 0);
	net_tcp_close(Second);
	EXPECT_EQ(Listener.Poll(), CLoopbackListener::EState::GOT_CODE);
	EXPECT_STREQ(Listener.Code(), "the-code");
}

TEST(KogLogin, ListenerTimesOutAndStops)
{
	CLoopbackListener Listener;
	ASSERT_TRUE(Listener.Start("s", 50));
	std::this_thread::sleep_for(std::chrono::milliseconds(120));
	EXPECT_EQ(Listener.Poll(), CLoopbackListener::EState::TIMED_OUT);
	EXPECT_EQ(Listener.Poll(), CLoopbackListener::EState::TIMED_OUT);

	CLoopbackListener Other;
	ASSERT_TRUE(Other.Start("s", 10000));
	Other.Stop();
	EXPECT_EQ(Other.State(), CLoopbackListener::EState::CLOSED);
	EXPECT_EQ(Other.Poll(), CLoopbackListener::EState::CLOSED);
}

TEST(KogLogin, ListenerReportsTheServersRefusal)
{
	CLoopbackListener Listener;
	ASSERT_TRUE(Listener.Start("s", 10000));
	Exchange(Listener, "GET /callback?error=access_denied&state=s HTTP/1.1\r\n\r\n");
	EXPECT_EQ(Listener.State(), CLoopbackListener::EState::SERVER_ERROR);
	EXPECT_STREQ(Listener.Error(), "access_denied");
	EXPECT_STREQ(Listener.Code(), "");
}

TEST(KogLogin, Ed25519Rfc8032Vectors)
{
	uint8_t aSeed[SEED_SIZE], aPublic[PUBLIC_KEY_SIZE];
	Seed("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", aSeed);
	PublicKeyFromSeed(aSeed, aPublic);
	EXPECT_EQ(std::vector<uint8_t>(aPublic, aPublic + 32), Hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));

	// test 2: one byte message 0x72
	Seed("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", aSeed);
	PublicKeyFromSeed(aSeed, aPublic);
	EXPECT_EQ(std::vector<uint8_t>(aPublic, aPublic + 32), Hex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"));
	uint8_t aSecret[64], aSeedCopy[32], aPub2[32], aSignature[64];
	memcpy(aSeedCopy, aSeed, sizeof(aSeedCopy));
	crypto_ed25519_key_pair(aSecret, aPub2, aSeedCopy);
	const uint8_t Message = 0x72;
	crypto_ed25519_sign(aSignature, aSecret, &Message, 1);
	EXPECT_EQ(std::vector<uint8_t>(aSignature, aSignature + 64), Hex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"));
	EXPECT_EQ(crypto_ed25519_check(aSignature, aPub2, &Message, 1), 0);
}

TEST(KogLogin, ChallengeSignatureIsBoundToServerNonceAndDevice)
{
	uint8_t aSeed[SEED_SIZE], aPublic[PUBLIC_KEY_SIZE], aSignature[SIGNATURE_SIZE];
	Seed("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", aSeed);
	PublicKeyFromSeed(aSeed, aPublic);
	uint8_t aNonce[16];
	for(int i = 0; i < 16; i++)
		aNonce[i] = i;
	const char *pDevice = "0b6f2d5e-6f1d-4a43-9c1a-2f0e5a1f7c11";

	uint8_t aMessage[256];
	const size_t Length = BuildChallengeMessage(aMessage, sizeof(aMessage), "203.0.113.7:8303", aNonce, sizeof(aNonce), pDevice);
	ASSERT_EQ(Length, 12u + 16u + 16u + 36u);
	EXPECT_EQ(memcmp(aMessage, "KoG-login-v1203.0.113.7:8303", 28), 0);

	ASSERT_TRUE(SignChallenge(aSeed, "203.0.113.7:8303", aNonce, sizeof(aNonce), pDevice, aSignature));
	EXPECT_EQ(std::vector<uint8_t>(aSignature, aSignature + 64), Hex("4efa77a278e1488393a568049dcafdca202cdc7395046647bb5fbdf07f1df204f43dff9b5b0269a3e2389fc06d646a0e0ec3fcc191caea752808b462d39bab0e"));
	EXPECT_TRUE(VerifyChallenge(aPublic, "203.0.113.7:8303", aNonce, sizeof(aNonce), pDevice, aSignature));
	// a challenge cannot be reused on another server, nonce or device
	EXPECT_FALSE(VerifyChallenge(aPublic, "203.0.113.8:8303", aNonce, sizeof(aNonce), pDevice, aSignature));
	EXPECT_FALSE(VerifyChallenge(aPublic, "203.0.113.7:8304", aNonce, sizeof(aNonce), pDevice, aSignature));
	aNonce[0] ^= 1;
	EXPECT_FALSE(VerifyChallenge(aPublic, "203.0.113.7:8303", aNonce, sizeof(aNonce), pDevice, aSignature));
	aNonce[0] ^= 1;
	EXPECT_FALSE(VerifyChallenge(aPublic, "203.0.113.7:8303", aNonce, sizeof(aNonce), "other-device", aSignature));
	EXPECT_FALSE(SignChallenge(aSeed, "", aNonce, sizeof(aNonce), pDevice, aSignature));
}

TEST(KogLogin, KeyBlobRoundTrip)
{
	SDeviceKey Key = {};
	GenerateSeed(Key.m_aSeed);
	str_copy(Key.m_aId, "test-device-id");
	char aBlob[256];
	ASSERT_TRUE(EncodeKey(Key, aBlob, sizeof(aBlob)));
	SDeviceKey Back = {};
	ASSERT_TRUE(DecodeKey(aBlob, &Back));
	EXPECT_STREQ(Back.m_aId, Key.m_aId);
	EXPECT_EQ(memcmp(Back.m_aSeed, Key.m_aSeed, SEED_SIZE), 0);
	EXPECT_FALSE(DecodeKey("v2\nid\nAAAA", &Back));
	EXPECT_FALSE(DecodeKey("v1\nid\nAAAA", &Back));
	EXPECT_FALSE(DecodeKey("v1\n\nAAAA", &Back));
}

TEST(KogLogin, NothingIsAnsweredToANonKogServer)
{
	SJoinDecision Decision = {};
	Decision.m_SwitchOn = true;
	Decision.m_AutoLoginOn = true;
	Decision.m_HasDeviceKey = true;
	Decision.m_ServerIsKog = true;
	EXPECT_TRUE(MayAnswerChallenge(Decision));

	SJoinDecision Vanilla = Decision;
	Vanilla.m_ServerIsKog = false;
	EXPECT_FALSE(MayAnswerChallenge(Vanilla));

	SJoinDecision Off = Decision;
	Off.m_SwitchOn = false;
	EXPECT_FALSE(MayAnswerChallenge(Off));
	Off = Decision;
	Off.m_AutoLoginOn = false;
	EXPECT_FALSE(MayAnswerChallenge(Off));
	Off = Decision;
	Off.m_HasDeviceKey = false;
	EXPECT_FALSE(MayAnswerChallenge(Off));
	Off = Decision;
	Off.m_AlreadyLoggedIn = true;
	EXPECT_FALSE(MayAnswerChallenge(Off));
	Off = Decision;
	Off.m_AlreadyAnsweredThisJoin = true;
	EXPECT_FALSE(MayAnswerChallenge(Off));
}
