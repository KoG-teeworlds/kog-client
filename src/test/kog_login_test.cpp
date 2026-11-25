#include <base/detect.h>
#include <base/net.h>
#include <base/secure.h>
#include <base/str.h>

#include <engine/external/monocypher/monocypher-ed25519.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/kog_login.h>

#include <gtest/gtest.h>

#if defined(CONF_FAMILY_UNIX)
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <memory>
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

namespace {

void Sequence(uint8_t *pOut, size_t Size, uint8_t Start)
{
	for(size_t i = 0; i < Size; i++)
		pOut[i] = (uint8_t)(Start + i);
}

const char *const DEVICE_UUID = "0b6f2d5e-6f1d-4a43-9c1a-2f0e5a1f7c11";

}

TEST(KogLogin, ChallengeMessageHasTheDesignedLayout)
{
	uint8_t aNonce[NONCE_SIZE], aDevice[UUID_SIZE], aSession[UUID_SIZE];
	Sequence(aNonce, sizeof(aNonce), 0);
	ASSERT_TRUE(ParseUuid(DEVICE_UUID, aDevice));
	Sequence(aSession, sizeof(aSession), 0x40);
	uint8_t aMessage[256];
	const size_t Length = BuildChallengeMessage(aMessage, sizeof(aMessage), "203.0.113.7:8303", aNonce, sizeof(aNonce), aDevice, aSession);
	ASSERT_EQ(Length, 13u + 1u + 16u + 32u + 16u + 16u);
	EXPECT_EQ(memcmp(aMessage, "KoG-login-v1\0", 13), 0);
	EXPECT_EQ(aMessage[13], 16);
	EXPECT_EQ(memcmp(aMessage + 14, "203.0.113.7:8303", 16), 0);
	EXPECT_EQ(memcmp(aMessage + 30, aNonce, 32), 0);
	EXPECT_EQ(memcmp(aMessage + 62, aDevice, 16), 0);
	EXPECT_EQ(memcmp(aMessage + 78, aSession, 16), 0);

	// the parts have to be what the design says
	EXPECT_EQ(BuildChallengeMessage(aMessage, sizeof(aMessage), "", aNonce, sizeof(aNonce), aDevice, aSession), 0u);
	EXPECT_EQ(BuildChallengeMessage(aMessage, sizeof(aMessage), "203.0.113.7:8303", aNonce, 16, aDevice, aSession), 0u);
	EXPECT_EQ(BuildChallengeMessage(aMessage, 50, "203.0.113.7:8303", aNonce, sizeof(aNonce), aDevice, aSession), 0u);
	const std::string TooLong(256, 'a');
	EXPECT_EQ(BuildChallengeMessage(aMessage, sizeof(aMessage), TooLong.c_str(), aNonce, sizeof(aNonce), aDevice, aSession), 0u);
}

TEST(KogLogin, ChallengeMessageHasNoAmbiguityBetweenAddressAndNonce)
{
	// The old format glued the parts together. This pair of tuples gave the same bytes there.
	uint8_t aDevice[UUID_SIZE], aSession[UUID_SIZE];
	ASSERT_TRUE(ParseUuid(DEVICE_UUID, aDevice));
	Sequence(aSession, sizeof(aSession), 0x40);
	uint8_t aNonce[NONCE_SIZE];
	for(uint8_t &Byte : aNonce)
		Byte = 'N';
	uint8_t aOtherNonce[NONCE_SIZE];
	aOtherNonce[0] = '3';
	memcpy(aOtherNonce + 1, aNonce, NONCE_SIZE - 1);

	uint8_t aOne[256], aTwo[256];
	const size_t One = BuildChallengeMessage(aOne, sizeof(aOne), "1.2.3.4:830", aOtherNonce, NONCE_SIZE, aDevice, aSession);
	const size_t Two = BuildChallengeMessage(aTwo, sizeof(aTwo), "1.2.3.4:8303", aNonce, NONCE_SIZE, aDevice, aSession);
	ASSERT_NE(One, 0u);
	ASSERT_NE(Two, 0u);
	EXPECT_FALSE(One == Two && memcmp(aOne, aTwo, One) == 0);
	EXPECT_NE(One, Two);
}

TEST(KogLogin, ChallengeSignatureIsBoundToServerNonceDeviceAndSession)
{
	uint8_t aSeed[SEED_SIZE], aPublic[PUBLIC_KEY_SIZE], aSignature[SIGNATURE_SIZE];
	Seed("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", aSeed);
	PublicKeyFromSeed(aSeed, aPublic);
	uint8_t aNonce[NONCE_SIZE], aSession[UUID_SIZE];
	Sequence(aNonce, sizeof(aNonce), 0);
	const std::vector<uint8_t> SessionBytes = Hex("018f3b0e7a1c7d2e8b4f9a0c1d2e3f40");
	std::copy(SessionBytes.begin(), SessionBytes.end(), aSession);

	// Vector made outside of this code base (Python cryptography, Ed25519) over the 94 bytes of the design
	ASSERT_TRUE(SignChallenge(aSeed, "203.0.113.7:8303", aNonce, sizeof(aNonce), DEVICE_UUID, aSession, aSignature));
	EXPECT_EQ(std::vector<uint8_t>(aSignature, aSignature + 64), Hex("88145915f051f48447e2a3182c3d90cffc95681e5b4a704ba3af7a28e49c82cdbb0d85af387c51d909abcc7e42efe0f206bfd0cd455e2bf354cb33958b1c5806"));
	EXPECT_TRUE(VerifyChallenge(aPublic, "203.0.113.7:8303", aNonce, sizeof(aNonce), DEVICE_UUID, aSession, aSignature));
	// a challenge cannot be reused on another server, nonce, device or session
	EXPECT_FALSE(VerifyChallenge(aPublic, "203.0.113.8:8303", aNonce, sizeof(aNonce), DEVICE_UUID, aSession, aSignature));
	EXPECT_FALSE(VerifyChallenge(aPublic, "203.0.113.7:8304", aNonce, sizeof(aNonce), DEVICE_UUID, aSession, aSignature));
	aNonce[0] ^= 1;
	EXPECT_FALSE(VerifyChallenge(aPublic, "203.0.113.7:8303", aNonce, sizeof(aNonce), DEVICE_UUID, aSession, aSignature));
	aNonce[0] ^= 1;
	EXPECT_FALSE(VerifyChallenge(aPublic, "203.0.113.7:8303", aNonce, sizeof(aNonce), "1b6f2d5e-6f1d-4a43-9c1a-2f0e5a1f7c11", aSession, aSignature));
	EXPECT_FALSE(VerifyChallenge(aPublic, "203.0.113.7:8303", aNonce, sizeof(aNonce), "other-device", aSession, aSignature));
	aSession[15] ^= 1;
	EXPECT_FALSE(VerifyChallenge(aPublic, "203.0.113.7:8303", aNonce, sizeof(aNonce), DEVICE_UUID, aSession, aSignature));
	aSession[15] ^= 1;
	EXPECT_FALSE(SignChallenge(aSeed, "", aNonce, sizeof(aNonce), DEVICE_UUID, aSession, aSignature));
	EXPECT_FALSE(SignChallenge(aSeed, "203.0.113.7:8303", aNonce, 16, DEVICE_UUID, aSession, aSignature));
}

TEST(KogLogin, UuidParsing)
{
	uint8_t aUuid[UUID_SIZE];
	ASSERT_TRUE(ParseUuid("0B6F2D5E-6F1D-4A43-9C1A-2F0E5A1F7C11", aUuid));
	EXPECT_EQ(std::vector<uint8_t>(aUuid, aUuid + 16), Hex("0b6f2d5e6f1d4a439c1a2f0e5a1f7c11"));
	EXPECT_FALSE(ParseUuid("", aUuid));
	EXPECT_FALSE(ParseUuid("0b6f2d5e6f1d4a439c1a2f0e5a1f7c11", aUuid));
	EXPECT_FALSE(ParseUuid("0b6f2d5e-6f1d-4a43-9c1a-2f0e5a1f7c1g", aUuid));
	EXPECT_FALSE(ParseUuid("0b6f2d5e-6f1d-4a43-9c1a-2f0e5a1f7c111", aUuid));
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
	Decision.m_CapabilityNegotiated = true;
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
	Off.m_CapabilityNegotiated = false;
	EXPECT_FALSE(MayAnswerChallenge(Off));
	Off = Decision;
	Off.m_AlreadyLoggedIn = true;
	EXPECT_FALSE(MayAnswerChallenge(Off));
	Off = Decision;
	Off.m_AlreadyAnsweredThisJoin = true;
	EXPECT_FALSE(MayAnswerChallenge(Off));
}

TEST(KogLogin, DeviceNameIsNeutralAndNotTheHostName)
{
	char aName[161];
	DefaultDeviceName(aName, sizeof(aName));
	EXPECT_TRUE(str_startswith(aName, "KoG Client")) << aName;
	EXPECT_LE(str_length(aName), (int)MAX_DEVICE_NAME);
	char aHost[256] = "";
#if defined(CONF_FAMILY_UNIX)
	if(gethostname(aHost, sizeof(aHost) - 1) == 0 && aHost[0] != '\0')
	{
		EXPECT_EQ(str_find_nocase(aName, aHost), nullptr) << aName;
	}
#endif
}

TEST(KogLogin, EndpointsMustShareTheOriginOrBeKogOrListed)
{
	const char *pBase = "https://kog.tw";
	// the same origin, with the default port written or not, any case, any path and query
	EXPECT_TRUE(EndpointAllowed(pBase, "https://kog.tw/oauth/token", ""));
	EXPECT_TRUE(EndpointAllowed(pBase, "https://KOG.tw:443/oauth/token?x=1", ""));
	// another host of kog.tw, only https on the default port
	EXPECT_TRUE(EndpointAllowed(pBase, "https://auth.kog.tw/oauth2/token", ""));
	EXPECT_TRUE(EndpointAllowed(pBase, "https://a.b.kog.tw/x", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://auth.kog.tw:8443/oauth2/token", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "http://auth.kog.tw/oauth2/token", ""));

	// everything else is refused
	EXPECT_FALSE(EndpointAllowed(pBase, "https://evil.example/oauth/token", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://kog.tw.evil.example/oauth/token", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://evilkog.tw/oauth/token", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://kog.tw@evil.example/oauth/token", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://evil.example\\@kog.tw/oauth/token", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://evil.example#@kog.tw/", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://kog.tw:8443/oauth/token", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "http://kog.tw/oauth/token", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://kog.tw:0/x", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://kog.tw:99999/x", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "https:///x", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://kog.tw /x", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "ftp://kog.tw/x", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "//evil.example/x", ""));
	EXPECT_FALSE(EndpointAllowed(pBase, "", ""));

	// the extra list: exact origins, https only, spaces or commas
	EXPECT_TRUE(EndpointAllowed(pBase, "https://zitadel.example/oauth/v2/token", "https://zitadel.example"));
	EXPECT_TRUE(EndpointAllowed(pBase, "https://zitadel.example/oauth/v2/token", "https://other.example, https://zitadel.example"));
	EXPECT_TRUE(EndpointAllowed(pBase, "https://zitadel.example:8443/token", "https://zitadel.example:8443"));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://zitadel.example:8443/token", "https://zitadel.example"));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://zitadel.example.evil.example/token", "https://zitadel.example"));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://zitadel.example/token", "http://zitadel.example"));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://zitadel.example/token", "zitadel.example"));
	EXPECT_FALSE(EndpointAllowed(pBase, "https://zitadel.example/token", "https://zitadel.example/path"));
	EXPECT_FALSE(EndpointAllowed(pBase, "http://zitadel.example/token", "https://zitadel.example"));

	// a staging base accepts itself but not production by accident of the suffix rule only
	EXPECT_TRUE(EndpointAllowed("https://staging.example", "https://staging.example/token", ""));
	EXPECT_FALSE(EndpointAllowed("https://staging.example", "https://evil.example/token", ""));
	// plain http is only for a base that is http itself (HttpAllowInsecure in a test setup) and only on the same origin
	EXPECT_TRUE(EndpointAllowed("http://127.0.0.1:8080", "http://127.0.0.1:8080/token", ""));
	EXPECT_FALSE(EndpointAllowed("http://127.0.0.1:8080", "http://127.0.0.1:8081/token", ""));
	EXPECT_TRUE(EndpointAllowed("https://[::1]:8443", "https://[::1]:8443/token", ""));
}

TEST(KogLogin, ALongAuthorizationCodeSurvivesTheListener)
{
	// league/oauth2-server hands out an encrypted token, hex or base64 of a JSON payload. Take the cap as the size.
	std::string Code(MAX_CODE_LENGTH - 1, 'a');
	for(size_t i = 0; i < Code.size(); i += 7)
		Code[i] = (char)('A' + i % 26);
	CLoopbackListener Listener;
	ASSERT_TRUE(Listener.Start("s", 10000));
	const std::string Response = Exchange(Listener, "GET /callback?code=" + Code + "&state=s HTTP/1.1\r\nHost: x\r\n\r\n");
	EXPECT_NE(Response.find("200 OK"), std::string::npos);
	EXPECT_EQ(Listener.State(), CLoopbackListener::EState::GOT_CODE);
	EXPECT_EQ(std::string(Listener.Code()), Code);
	EXPECT_GE((size_t)MAX_CODE_LENGTH, 4096u);
	EXPECT_GT((size_t)MAX_REQUEST_SIZE, (size_t)MAX_CODE_LENGTH);

	// a code longer than the cap is refused, not cut
	char aCode[MAX_CODE_LENGTH + 1], aError[16];
	const std::string TooLong = "GET /callback?code=" + std::string(MAX_CODE_LENGTH + 10, 'b') + "&state=s HTTP/1.1\r\n\r\n";
	EXPECT_EQ(ParseCallback(TooLong.data(), TooLong.size(), "s", aCode, sizeof(aCode), aError, sizeof(aError)), ECallback::MALFORMED);
}

TEST(KogLogin, IdleConnectionsDoNotUseUpTheSingleChance)
{
	CLoopbackListener Listener;
	ASSERT_TRUE(Listener.Start("s", 10000));
	NETADDR Addr;
	char aAddr[64];
	str_format(aAddr, sizeof(aAddr), "127.0.0.1:%d", Listener.Port());
	ASSERT_EQ(net_addr_from_str(&Addr, aAddr), 0);
	NETSOCKET aIdle[3];
	for(NETSOCKET &Idle : aIdle)
	{
		NETADDR Bind = {};
		Bind.type = NETTYPE_IPV4;
		Idle = net_tcp_create(Bind);
		ASSERT_NE(Idle, nullptr);
		ASSERT_EQ(net_tcp_connect(Idle, &Addr), 0);
	}
	for(int i = 0; i < 5; i++)
	{
		Listener.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	EXPECT_EQ(Listener.State(), CLoopbackListener::EState::LISTENING);
	// a wrong request while the idle ones still sit there
	EXPECT_NE(Exchange(Listener, "GET /other HTTP/1.1\r\n\r\n").find("404"), std::string::npos);
	EXPECT_EQ(Listener.State(), CLoopbackListener::EState::LISTENING);
	const std::string Response = Exchange(Listener, "GET /callback?code=c&state=s HTTP/1.1\r\n\r\n");
	EXPECT_NE(Response.find("200 OK"), std::string::npos);
	EXPECT_EQ(Listener.State(), CLoopbackListener::EState::GOT_CODE);
	EXPECT_STREQ(Listener.Code(), "c");
	for(NETSOCKET Idle : aIdle)
		net_tcp_close(Idle);
}

namespace {

class CTestRequest : public IHttpRequest
{
public:
	CTestRequest() :
		IHttpRequest("https://example.invalid/") {}
	void Header(const char *pNameColonValue) override { m_Headers.push_back(pNameColonValue); }
	std::vector<std::string> m_Headers;
	const unsigned char *Body() const { return m_pBody; }
	size_t BodyLength() const { return m_BodyLength; }
	bool Follows() const { return m_FollowRedirects; }
};

}

TEST(KogLogin, HttpRequestsFollowRedirectsUnlessToldNotTo)
{
	CTestRequest Request;
	EXPECT_TRUE(Request.Follows());
	Request.FollowRedirects(false);
	EXPECT_FALSE(Request.Follows());
}

TEST(KogLogin, HttpRequestBodyCanBeWiped)
{
	CTestRequest Request;
	Request.PostForm("code=secret-code&code_verifier=secret-verifier");
	ASSERT_EQ(Request.BodyLength(), 46u);
	EXPECT_EQ(memcmp(Request.Body(), "code=secret", 11), 0);
	Request.WipeSecrets();
	for(size_t i = 0; i < Request.BodyLength(); i++)
		EXPECT_EQ(Request.Body()[i], 0) << i;
}

namespace {

// Answers every request with a 307 to /next on the same server and counts what arrived
class CRedirectServer
{
public:
	CRedirectServer()
	{
		for(int Attempt = 0; Attempt < 20 && !m_Listen; Attempt++)
		{
			const int Port = 49152 + secure_rand_below(65535 - 49152);
			char aAddr[64];
			str_format(aAddr, sizeof(aAddr), "127.0.0.1:%d", Port);
			NETADDR Addr;
			if(net_addr_from_str(&Addr, aAddr) != 0)
				continue;
			NETSOCKET Socket = net_tcp_create(Addr);
			if(Socket && net_tcp_listen(Socket, 4) == 0)
			{
				m_Listen = Socket;
				m_Port = Port;
			}
			else if(Socket)
				net_tcp_close(Socket);
		}
		m_Thread = std::thread([this]() { Run(); });
	}
	~CRedirectServer()
	{
		m_Stop = true;
		m_Thread.join();
		if(m_Listen)
			net_tcp_close(m_Listen);
	}
	int Port() const { return m_Port; }
	std::atomic<int> m_Requests{0};
	std::atomic<bool> m_SawSecretAtNext{false};

private:
	NETSOCKET m_Listen = nullptr;
	int m_Port = 0;
	std::atomic<bool> m_Stop{false};
	std::thread m_Thread;

	void Run()
	{
		while(!m_Stop && m_Listen)
		{
			if(net_socket_read_wait(m_Listen, std::chrono::milliseconds(20)) <= 0)
				continue;
			NETSOCKET Conn = nullptr;
			NETADDR From;
			if(net_tcp_accept(m_Listen, &Conn, &From) < 0 || !Conn)
				continue;
			std::string Request;
			char aBuf[2048];
			while(Request.find("\r\n\r\n") == std::string::npos && net_socket_read_wait(Conn, std::chrono::milliseconds(500)) > 0)
			{
				const int Bytes = net_tcp_recv(Conn, aBuf, sizeof(aBuf));
				if(Bytes <= 0)
					break;
				Request.append(aBuf, Bytes);
			}
			m_Requests++;
			if(Request.find("GET /next") == 0 || Request.find("POST /next") == 0)
				m_SawSecretAtNext = true;
			char aReply[256];
			const int Length = str_format(aReply, sizeof(aReply), "HTTP/1.1 307 Temporary Redirect\r\nLocation: http://127.0.0.1:%d/next\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", m_Port);
			net_tcp_send(Conn, aReply, Length);
			net_tcp_close(Conn);
		}
	}
};

int RunPost(bool Follow, CRedirectServer &Server)
{
	std::unique_ptr<IEngineHttp> pHttp(CreateEngineHttp());
	EXPECT_TRUE(pHttp->Init(std::chrono::milliseconds(500)));
	char aUrl[64];
	str_format(aUrl, sizeof(aUrl), "http://127.0.0.1:%d/token", Server.Port());
	std::shared_ptr<IHttpRequest> pRequest = CreateHttpRequest(aUrl);
	pRequest->PostForm("code=secret&code_verifier=secret");
	pRequest->FailOnErrorStatus(false);
	pRequest->FollowRedirects(Follow);
	pRequest->Timeout(CTimeout{2000, 5000, 0, 0});
	pHttp->Run(pRequest);
	pRequest->Wait();
	const int Status = pRequest->State() == EHttpState::DONE ? pRequest->StatusCode() : -1;
	pHttp->Shutdown();
	return Status;
}

}

TEST(KogLogin, CurlDoesNotFollowARedirectOfASecretRequest)
{
	g_Config.m_HttpAllowInsecure = 1;
	{
		CRedirectServer Server;
		ASSERT_NE(Server.Port(), 0);
		EXPECT_EQ(RunPost(false, Server), 307);
		EXPECT_EQ(Server.m_Requests.load(), 1);
		EXPECT_FALSE(Server.m_SawSecretAtNext.load());
	}
	{
		// the default still follows, which is why the switch exists
		CRedirectServer Server;
		ASSERT_NE(Server.Port(), 0);
		RunPost(true, Server);
		EXPECT_GT(Server.m_Requests.load(), 1);
		EXPECT_TRUE(Server.m_SawSecretAtNext.load());
	}
	g_Config.m_HttpAllowInsecure = 0;
}
