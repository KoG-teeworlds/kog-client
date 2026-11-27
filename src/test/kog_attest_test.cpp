#include "kog_attest_vector.h"

#include <base/hash.h>
#include <base/str.h>

#include <engine/shared/json.h>
#include <engine/shared/kog_attest.h>
#include <engine/shared/kog_hello.h>
#include <engine/shared/kog_login.h>
#include <engine/shared/packer.h>
#include <engine/shared/uuid_manager.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using namespace KogAttest;

namespace {

std::vector<uint8_t> Hex(const std::string &Text)
{
	std::vector<uint8_t> Out;
	for(size_t i = 0; i + 1 < Text.size(); i += 2)
		Out.push_back((uint8_t)std::stoi(Text.substr(i, 2), nullptr, 16));
	return Out;
}

std::string ToHex(const uint8_t *pData, size_t Size)
{
	static const char *s_pDigits = "0123456789abcdef";
	std::string Out;
	for(size_t i = 0; i < Size; i++)
	{
		Out += s_pDigits[pData[i] >> 4];
		Out += s_pDigits[pData[i] & 15];
	}
	return Out;
}

std::string Uuid(const char *pName)
{
	char aBuf[UUID_MAXSTRSIZE];
	FormatUuid(CalculateUuid(pName), aBuf, sizeof(aBuf));
	return aBuf;
}

std::string Str(const json_value *pObject, const char *pName)
{
	const json_value *pValue = json_object_get(pObject, pName);
	EXPECT_TRUE(pValue != nullptr && pValue->type == json_string) << pName;
	return pValue != nullptr && pValue->type == json_string ? pValue->u.string.ptr : "";
}

// The vector of the protocol repository, parsed once
class CVector
{
public:
	json_value *m_pRoot;
	CVector() { m_pRoot = JsonParse(KOG_ATTEST_VECTOR, str_length(KOG_ATTEST_VECTOR)); }
	~CVector()
	{
		if(m_pRoot != nullptr)
			json_value_free(m_pRoot);
	}
	const json_value *Array(const char *pName) const { return json_object_get(m_pRoot, pName); }
	std::string Text(const char *pName) const { return Str(m_pRoot, pName); }
};

template<size_t N>
void Copy(uint8_t (&aDst)[N], const std::string &Hexed)
{
	const std::vector<uint8_t> Bytes = Hex(Hexed);
	ASSERT_EQ(Bytes.size(), N);
	memcpy(aDst, Bytes.data(), N);
}

struct SCase
{
	std::string m_Address;
	SChallenge m_Challenge;
	SInputs m_Inputs;
	uint8_t m_aKeyId[KEY_ID_SIZE];
	std::vector<uint8_t> m_Hello;
};

void ReadCase(const json_value *pCase, SCase *pOut)
{
	const json_value *pFields = json_object_get(pCase, "fields");
	pOut->m_Address = Str(pFields, "addr");
	pOut->m_Inputs.m_pAddress = pOut->m_Address.c_str();
	Copy(pOut->m_Challenge.m_aNonce, Str(pFields, "nonce"));
	Copy(pOut->m_Challenge.m_aSessionId, Str(pFields, "session_id"));
	Copy(pOut->m_Inputs.m_aCertHash, Str(pFields, "cert_hash"));
	Copy(pOut->m_aKeyId, Str(pFields, "key_id"));
	Copy(pOut->m_Inputs.m_aMeasurement, Str(pFields, "measurement"));
	pOut->m_Hello = Hex(Str(pFields, "hello"));
	HelloHash(pOut->m_Hello.data(), pOut->m_Hello.size(), pOut->m_Inputs.m_aHelloHash);
}

// A test key, made at run time: it is the seed that is made up, never a key of anybody
void TestSeed(uint8_t (&aSeed)[SEED_SIZE], const char *pName)
{
	const SHA256_DIGEST Digest = sha256(pName, str_length(pName));
	memcpy(aSeed, Digest.data, SEED_SIZE);
}

void PublicKeyOf(const uint8_t (&aSeed)[SEED_SIZE], uint8_t (&aPublic)[PUBLIC_KEY_SIZE])
{
	KogLogin::PublicKeyFromSeed(aSeed, aPublic);
}

} // namespace

TEST(KogAttest, MessagesAndCapabilityMatchTheVector)
{
	CVector Vector;
	ASSERT_TRUE(Vector.m_pRoot != nullptr);
	const json_value *pMessages = json_object_get(Vector.m_pRoot, "messages");
	EXPECT_EQ(Uuid("kog-attest-challenge@kog.tw"), Str(pMessages, "kog-attest-challenge@kog.tw"));
	EXPECT_EQ(Uuid("kog-attest-response@kog.tw"), Str(pMessages, "kog-attest-response@kog.tw"));
	const json_value *pCapability = json_object_get(Vector.m_pRoot, "capability");
	EXPECT_EQ(json_object_get(pCapability, "id")->u.integer, 4);
	EXPECT_EQ(KogHello::WireId(EKogCap::CLIENT_ATTEST), 4);
	EXPECT_EQ(Str(pCapability, "name"), KogHello::Label(EKogCap::CLIENT_ATTEST));
	EXPECT_EQ(Vector.Text("domain"), ToHex(reinterpret_cast<const uint8_t *>(SIGN_DOMAIN), str_length(SIGN_DOMAIN) + 1));
}

TEST(KogAttest, KeyIdIsTheFirstSixteenBytesOfTheHashOfTheKey)
{
	CVector Vector;
	uint8_t aPublic[PUBLIC_KEY_SIZE];
	uint8_t aKeyId[KEY_ID_SIZE];
	Copy(aPublic, Vector.Text("public_key"));
	KeyId(aPublic, aKeyId);
	EXPECT_EQ(ToHex(aKeyId, sizeof(aKeyId)), Vector.Text("key_id"));
}

TEST(KogAttest, SignedBytesAndSignaturesOfEveryCaseOfTheVector)
{
	CVector Vector;
	uint8_t aPublic[PUBLIC_KEY_SIZE];
	Copy(aPublic, Vector.Text("public_key"));
	const json_value *pCases = Vector.Array("signed_cases");
	ASSERT_TRUE(pCases != nullptr && pCases->type == json_array);
	ASSERT_GE(pCases->u.array.length, 5u);
	for(unsigned i = 0; i < pCases->u.array.length; i++)
	{
		const json_value *pCase = pCases->u.array.values[i];
		SCase Case;
		ReadCase(pCase, &Case);
		SCOPED_TRACE(Str(pCase, "name"));

		EXPECT_EQ(ToHex(Case.m_Inputs.m_aHelloHash, HASH_SIZE), Str(pCase, "client_hello_hash"));
		uint8_t aBytes[MAX_SIGNED_SIZE];
		const size_t Length = BuildSignedBytes(aBytes, sizeof(aBytes), Case.m_Inputs.m_pAddress, Case.m_Challenge.m_aNonce, Case.m_Challenge.m_aSessionId, Case.m_Inputs.m_aCertHash, Case.m_Inputs.m_aHelloHash, Case.m_aKeyId, Case.m_Inputs.m_aMeasurement);
		ASSERT_NE(Length, 0u);
		EXPECT_EQ(ToHex(aBytes, Length), Str(pCase, "signed_bytes"));
		const SHA256_DIGEST Digest = sha256(aBytes, Length);
		EXPECT_EQ(ToHex(Digest.data, SHA256_DIGEST_LENGTH), Str(pCase, "sha256"));

		uint8_t aSignature[SIGNATURE_SIZE];
		Copy(aSignature, Str(pCase, "signature"));
		EXPECT_TRUE(Verify(aPublic, Case.m_Inputs, Case.m_Challenge, Case.m_aKeyId, aSignature));
	}
}

TEST(KogAttest, ChangingAnyFieldBreaksTheSignatureOfTheVector)
{
	CVector Vector;
	uint8_t aPublic[PUBLIC_KEY_SIZE];
	Copy(aPublic, Vector.Text("public_key"));
	const json_value *pCase = Vector.Array("signed_cases")->u.array.values[0];
	uint8_t aSignature[SIGNATURE_SIZE];
	Copy(aSignature, Str(pCase, "signature"));
	SCase Base;
	ReadCase(pCase, &Base);
	ASSERT_TRUE(Verify(aPublic, Base.m_Inputs, Base.m_Challenge, Base.m_aKeyId, aSignature));

	{
		SCase Case;
		ReadCase(pCase, &Case);
		Case.m_Address = "203.0.113.7:8304"; // another server
		Case.m_Inputs.m_pAddress = Case.m_Address.c_str();
		EXPECT_FALSE(Verify(aPublic, Case.m_Inputs, Case.m_Challenge, Case.m_aKeyId, aSignature));
	}
	{
		SCase Case;
		ReadCase(pCase, &Case);
		Case.m_Challenge.m_aNonce[0] ^= 1; // another challenge
		EXPECT_FALSE(Verify(aPublic, Case.m_Inputs, Case.m_Challenge, Case.m_aKeyId, aSignature));
	}
	{
		SCase Case;
		ReadCase(pCase, &Case);
		Case.m_Challenge.m_aSessionId[0] ^= 1; // another session
		EXPECT_FALSE(Verify(aPublic, Case.m_Inputs, Case.m_Challenge, Case.m_aKeyId, aSignature));
	}
	{
		SCase Case;
		ReadCase(pCase, &Case);
		Case.m_Inputs.m_aCertHash[0] ^= 1; // another server certificate
		EXPECT_FALSE(Verify(aPublic, Case.m_Inputs, Case.m_Challenge, Case.m_aKeyId, aSignature));
	}
	{
		SCase Case;
		ReadCase(pCase, &Case);
		Case.m_Inputs.m_aHelloHash[0] ^= 1; // another declared build
		EXPECT_FALSE(Verify(aPublic, Case.m_Inputs, Case.m_Challenge, Case.m_aKeyId, aSignature));
	}
	{
		SCase Case;
		ReadCase(pCase, &Case);
		Case.m_aKeyId[0] ^= 1;
		EXPECT_FALSE(Verify(aPublic, Case.m_Inputs, Case.m_Challenge, Case.m_aKeyId, aSignature));
	}
	{
		SCase Case;
		ReadCase(pCase, &Case);
		Case.m_Inputs.m_aMeasurement[0] ^= 1;
		EXPECT_FALSE(Verify(aPublic, Case.m_Inputs, Case.m_Challenge, Case.m_aKeyId, aSignature));
	}
}

TEST(KogAttest, ASignatureMadeAtRunTimeVerifiesAndIsBound)
{
	CVector Vector;
	uint8_t aSeed[SEED_SIZE], aPublic[PUBLIC_KEY_SIZE];
	TestSeed(aSeed, "kog attest test key one");
	PublicKeyOf(aSeed, aPublic);
	uint8_t aOtherSeed[SEED_SIZE], aOtherPublic[PUBLIC_KEY_SIZE];
	TestSeed(aOtherSeed, "kog attest test key two");
	PublicKeyOf(aOtherSeed, aOtherPublic);

	SCase Case;
	ReadCase(Vector.Array("signed_cases")->u.array.values[0], &Case);
	uint8_t aKeyId[KEY_ID_SIZE], aSignature[SIGNATURE_SIZE], aSignature2[SIGNATURE_SIZE];
	ASSERT_TRUE(Sign(aSeed, Case.m_Inputs, Case.m_Challenge, aKeyId, aSignature));
	uint8_t aExpectedKeyId[KEY_ID_SIZE];
	KeyId(aPublic, aExpectedKeyId);
	EXPECT_EQ(memcmp(aKeyId, aExpectedKeyId, KEY_ID_SIZE), 0);
	EXPECT_TRUE(Verify(aPublic, Case.m_Inputs, Case.m_Challenge, aKeyId, aSignature));
	EXPECT_FALSE(Verify(aOtherPublic, Case.m_Inputs, Case.m_Challenge, aKeyId, aSignature));
	// Ed25519 is deterministic
	ASSERT_TRUE(Sign(aSeed, Case.m_Inputs, Case.m_Challenge, aKeyId, aSignature2));
	EXPECT_EQ(memcmp(aSignature, aSignature2, SIGNATURE_SIZE), 0);

	// the same bytes as the vector: what the key signs is exactly the signed bytes of the vector
	uint8_t aBytes[MAX_SIGNED_SIZE];
	const size_t Length = BuildSignedBytes(aBytes, sizeof(aBytes), Case.m_Inputs.m_pAddress, Case.m_Challenge.m_aNonce, Case.m_Challenge.m_aSessionId, Case.m_Inputs.m_aCertHash, Case.m_Inputs.m_aHelloHash, aKeyId, Case.m_Inputs.m_aMeasurement);
	ASSERT_NE(Length, 0u);
	EXPECT_EQ(aBytes[0], 'K');
}

TEST(KogAttest, TheAddressIsLengthPrefixedAndLimited)
{
	CVector Vector;
	SCase Case;
	ReadCase(Vector.Array("signed_cases")->u.array.values[0], &Case);
	uint8_t aBytes[MAX_SIGNED_SIZE];
	EXPECT_EQ(BuildSignedBytes(aBytes, sizeof(aBytes), "", Case.m_Challenge.m_aNonce, Case.m_Challenge.m_aSessionId, Case.m_Inputs.m_aCertHash, Case.m_Inputs.m_aHelloHash, Case.m_aKeyId, Case.m_Inputs.m_aMeasurement), 0u);
	const std::string Long(256, 'a');
	EXPECT_EQ(BuildSignedBytes(aBytes, sizeof(aBytes), Long.c_str(), Case.m_Challenge.m_aNonce, Case.m_Challenge.m_aSessionId, Case.m_Inputs.m_aCertHash, Case.m_Inputs.m_aHelloHash, Case.m_aKeyId, Case.m_Inputs.m_aMeasurement), 0u);
	const std::string Max(255, 'a');
	EXPECT_EQ(BuildSignedBytes(aBytes, sizeof(aBytes), Max.c_str(), Case.m_Challenge.m_aNonce, Case.m_Challenge.m_aSessionId, Case.m_Inputs.m_aCertHash, Case.m_Inputs.m_aHelloHash, Case.m_aKeyId, Case.m_Inputs.m_aMeasurement), (size_t)MAX_SIGNED_SIZE);
	// too small an output
	EXPECT_EQ(BuildSignedBytes(aBytes, 100, "203.0.113.7:8303", Case.m_Challenge.m_aNonce, Case.m_Challenge.m_aSessionId, Case.m_Inputs.m_aCertHash, Case.m_Inputs.m_aHelloHash, Case.m_aKeyId, Case.m_Inputs.m_aMeasurement), 0u);
}

TEST(KogAttest, TheHelloThatTheClientSendsHasTheHashOfTheVector)
{
	// "01 01 01 1.3.0 stable": revision 1, one id (client_login), build, channel
	CVector Vector;
	CPacker Packer;
	Packer.Reset();
	KogHello::PackClientHello(&Packer, 1u << (int)EKogCap::CLIENT_LOGIN, "1.3.0", "stable");
	uint8_t aHash[HASH_SIZE];
	HelloHash(Packer.Data(), Packer.Size(), aHash);
	EXPECT_EQ(ToHex(aHash, sizeof(aHash)), Str(Vector.Array("signed_cases")->u.array.values[0], "client_hello_hash"));
}

TEST(KogAttest, ReadsTheChallengesOfTheVector)
{
	CVector Vector;
	const json_value *pCases = Vector.Array("wire_cases");
	ASSERT_TRUE(pCases != nullptr);
	int Challenges = 0;
	for(unsigned i = 0; i < pCases->u.array.length; i++)
	{
		const json_value *pCase = pCases->u.array.values[i];
		const std::string Name = Str(pCase, "name");
		if(Name.rfind("challenge", 0) != 0)
			continue;
		SCOPED_TRACE(Name);
		Challenges++;
		// the message id and the uuid come first: 1 + 16 bytes
		const std::vector<uint8_t> Bytes = Hex(Str(pCase, "bytes"));
		ASSERT_GT(Bytes.size(), 17u);
		CUnpacker Unpacker;
		Unpacker.Reset(Bytes.data() + 17, (int)Bytes.size() - 17);
		SChallenge Challenge;
		ASSERT_TRUE(UnpackChallenge(&Unpacker, &Challenge));
		const json_value *pRead = json_object_get(pCase, "read");
		EXPECT_EQ(Challenge.m_Revision, json_object_get(pRead, "revision")->u.integer);
		EXPECT_EQ(Challenge.m_TtlMs, json_object_get(pRead, "ttl")->u.integer);
		const json_value *pRanges = json_object_get(pRead, "ranges");
		ASSERT_EQ((unsigned)Challenge.m_NumRanges, pRanges->u.array.length);
		for(unsigned r = 0; r < pRanges->u.array.length; r++)
		{
			const json_value *pRange = pRanges->u.array.values[r];
			EXPECT_EQ(Challenge.m_aRanges[r].m_Module, pRange->u.array.values[0]->u.integer);
			EXPECT_EQ(Challenge.m_aRanges[r].m_Offset, pRange->u.array.values[1]->u.integer);
			EXPECT_EQ(Challenge.m_aRanges[r].m_Length, pRange->u.array.values[2]->u.integer);
		}
		const json_value *pCertHash = json_object_get(pRead, "server_cert_hash");
		if(pCertHash->type == json_string)
		{
			EXPECT_TRUE(Challenge.m_HasCertHash);
			EXPECT_EQ(ToHex(Challenge.m_aCertHash, HASH_SIZE), pCertHash->u.string.ptr);
		}
		else
			EXPECT_FALSE(Challenge.m_HasCertHash);
		EXPECT_EQ(ToHex(Challenge.m_aNonce, NONCE_SIZE), "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
		EXPECT_EQ(ToHex(Challenge.m_aSessionId, SESSION_ID_SIZE), "101112131415161718191a1b1c1d1e1f");
	}
	EXPECT_EQ(Challenges, 2);
}

TEST(KogAttest, PacksTheResponsesOfTheVector)
{
	CVector Vector;
	const json_value *pCases = Vector.Array("wire_cases");
	int Responses = 0;
	for(unsigned i = 0; i < pCases->u.array.length; i++)
	{
		const json_value *pCase = pCases->u.array.values[i];
		const std::string Name = Str(pCase, "name");
		if(Name.rfind("response", 0) != 0)
			continue;
		const json_value *pRead = json_object_get(pCase, "read");
		const int Status = json_object_get(pRead, "status")->u.integer;
		// only a response with a key and status ok is ever sent by this client
		if(Status != 0)
			continue;
		SCOPED_TRACE(Name);
		Responses++;
		const std::vector<uint8_t> Bytes = Hex(Str(pCase, "bytes"));
		ASSERT_EQ(Bytes.size(), 17u + 1 + KEY_ID_SIZE + 1 + MEASUREMENT_SIZE + SIGNATURE_SIZE);
		uint8_t aKeyId[KEY_ID_SIZE], aMeasurement[MEASUREMENT_SIZE], aSignature[SIGNATURE_SIZE];
		memcpy(aKeyId, Bytes.data() + 18, KEY_ID_SIZE);
		Copy(aMeasurement, Str(pRead, "measurement"));
		memcpy(aSignature, Bytes.data() + 18 + KEY_ID_SIZE + 1 + MEASUREMENT_SIZE, SIGNATURE_SIZE);
		CPacker Packer;
		Packer.Reset();
		PackResponse(&Packer, aKeyId, EStatus::OK, aMeasurement, aSignature);
		ASSERT_EQ((size_t)Packer.Size(), Bytes.size() - 17);
		EXPECT_EQ(memcmp(Packer.Data(), Bytes.data() + 17, Bytes.size() - 17), 0);
	}
	EXPECT_EQ(Responses, 2);
}

namespace {

void PackChallenge(CPacker &Packer, int Revision, int TtlMs, int NumRanges, bool WithCertHash)
{
	Packer.Reset();
	Packer.AddInt(Revision);
	const std::string Nonce(NONCE_SIZE, 'n'), Session(SESSION_ID_SIZE, 's');
	Packer.AddRaw(Nonce.data(), NONCE_SIZE);
	Packer.AddRaw(Session.data(), SESSION_ID_SIZE);
	Packer.AddInt(TtlMs);
	Packer.AddInt(NumRanges);
	for(int i = 0; i < NumRanges; i++)
	{
		Packer.AddInt(0);
		Packer.AddInt(4096);
		Packer.AddInt(64);
	}
	if(WithCertHash)
	{
		const std::string Cert(HASH_SIZE, 'c');
		Packer.AddRaw(Cert.data(), HASH_SIZE);
	}
}

bool Read(const CPacker &Packer, SChallenge *pChallenge)
{
	CUnpacker Unpacker;
	Unpacker.Reset(Packer.Data(), Packer.Size());
	return UnpackChallenge(&Unpacker, pChallenge);
}

} // namespace

TEST(KogAttest, MalformedChallengesAreNotUnderstood)
{
	SChallenge Challenge;
	CPacker Packer;
	PackChallenge(Packer, 1, 30000, 0, false);
	EXPECT_TRUE(Read(Packer, &Challenge));
	PackChallenge(Packer, 0, 30000, 0, false);
	EXPECT_FALSE(Read(Packer, &Challenge));
	PackChallenge(Packer, 1, 999, 0, false);
	EXPECT_FALSE(Read(Packer, &Challenge));
	PackChallenge(Packer, 1, MAX_TTL_MS + 1, 0, false);
	EXPECT_FALSE(Read(Packer, &Challenge));
	PackChallenge(Packer, 1, 30000, MAX_RANGES + 1, false);
	EXPECT_FALSE(Read(Packer, &Challenge));
	PackChallenge(Packer, 1, 30000, -1, false);
	EXPECT_FALSE(Read(Packer, &Challenge));
	PackChallenge(Packer, 1, 30000, MAX_RANGES, false);
	EXPECT_TRUE(Read(Packer, &Challenge));
	EXPECT_EQ(Challenge.m_NumRanges, MAX_RANGES);
	// revision 2 without the certificate hash, or with it cut
	PackChallenge(Packer, 2, 30000, 0, false);
	EXPECT_FALSE(Read(Packer, &Challenge));
	PackChallenge(Packer, 2, 30000, 0, true);
	EXPECT_TRUE(Read(Packer, &Challenge));
	EXPECT_TRUE(Challenge.m_HasCertHash);
	// a newer revision only adds at the end
	PackChallenge(Packer, 3, 30000, 1, true);
	EXPECT_TRUE(Read(Packer, &Challenge));
	// nothing at all, and a nonce that is cut
	Packer.Reset();
	EXPECT_FALSE(Read(Packer, &Challenge));
	Packer.Reset();
	Packer.AddInt(1);
	Packer.AddRaw("short", 5);
	EXPECT_FALSE(Read(Packer, &Challenge));
	// a failed read leaves nothing behind
	EXPECT_EQ(Challenge.m_Revision, 0);
}

namespace {

SDecision Everything()
{
	SDecision Decision;
	Decision.m_SwitchOn = true;
	Decision.m_ProofOn = true;
	Decision.m_HasKey = true;
	Decision.m_ServerIsKog = true;
	Decision.m_CapabilityNegotiated = true;
	Decision.m_CertVerified = true;
	Decision.m_CertBad = false;
	Decision.m_CertRequired = false;
	return Decision;
}

SChallenge ChallengeWithNonce(uint8_t First)
{
	SChallenge Challenge;
	Challenge.m_Revision = 1;
	Challenge.m_aNonce[0] = First;
	Challenge.m_TtlMs = 30000;
	return Challenge;
}

SInputs InputsWithCert(uint8_t First)
{
	SInputs Inputs;
	Inputs.m_pAddress = "203.0.113.7:8303";
	Inputs.m_aCertHash[0] = First;
	return Inputs;
}

} // namespace

TEST(KogAttest, AnswersAnEnrolledPlayerOnAKoGServerThatAgreed)
{
	CJoin Join;
	EXPECT_EQ(Join.Decide(Everything(), ChallengeWithNonce(1), InputsWithCert(7), 0), EDecision::ANSWER);
}

TEST(KogAttest, ADecisionThatSaysNoSendsNothing)
{
	CJoin Join;
	const SChallenge Challenge = ChallengeWithNonce(1);
	const SInputs Inputs = InputsWithCert(7);

	// not logged in or not enrolled: no key, or the key is locked or damaged
	SDecision Decision = Everything();
	Decision.m_HasKey = false;
	EXPECT_EQ(Join.Decide(Decision, Challenge, Inputs, 0), EDecision::NO_KEY);

	// a vanilla or other server: not listed for KoG, or it did not agree to client_attest in the hello
	Decision = Everything();
	Decision.m_ServerIsKog = false;
	EXPECT_EQ(Join.Decide(Decision, Challenge, Inputs, 0), EDecision::NOT_KOG);
	Decision = Everything();
	Decision.m_CapabilityNegotiated = false;
	EXPECT_EQ(Join.Decide(Decision, Challenge, Inputs, 0), EDecision::NO_CAPABILITY);

	// the switches of the player
	Decision = Everything();
	Decision.m_SwitchOn = false;
	EXPECT_EQ(Join.Decide(Decision, Challenge, Inputs, 0), EDecision::SWITCH_OFF);
	Decision = Everything();
	Decision.m_ProofOn = false;
	EXPECT_EQ(Join.Decide(Decision, Challenge, Inputs, 0), EDecision::SWITCH_OFF);

	// a server certificate that is wrong, or missing while the player asks for one
	Decision = Everything();
	Decision.m_CertBad = true;
	EXPECT_EQ(Join.Decide(Decision, Challenge, Inputs, 0), EDecision::CERT_BAD);
	Decision = Everything();
	Decision.m_CertVerified = false;
	Decision.m_CertRequired = true;
	EXPECT_EQ(Join.Decide(Decision, Challenge, Inputs, 0), EDecision::CERT_REQUIRED);
	// without the requirement an unproven server is still answered, with zeros as its certificate hash
	Decision.m_CertRequired = false;
	EXPECT_EQ(Join.Decide(Decision, Challenge, InputsWithCert(0), 0), EDecision::ANSWER);
}

TEST(KogAttest, AChallengeThatNamesAnotherServerCertificateIsNotAnswered)
{
	CJoin Join;
	SChallenge Challenge = ChallengeWithNonce(1);
	Challenge.m_Revision = 2;
	Challenge.m_HasCertHash = true;
	Challenge.m_aCertHash[0] = 7;
	EXPECT_EQ(Join.Decide(Everything(), Challenge, InputsWithCert(7), 0), EDecision::ANSWER);
	// the wrong hash: a relayed challenge, or a server that shows another identity than the one that was checked
	Challenge.m_aCertHash[0] = 8;
	EXPECT_EQ(Join.Decide(Everything(), Challenge, InputsWithCert(7), 0), EDecision::CERT_MISMATCH);
	// a certificate named while the client verified none
	Challenge.m_aCertHash[0] = 7;
	EXPECT_EQ(Join.Decide(Everything(), Challenge, InputsWithCert(0), 0), EDecision::CERT_MISMATCH);
	// a server that names none while the client verified one
	Challenge.m_aCertHash[0] = 0;
	EXPECT_EQ(Join.Decide(Everything(), Challenge, InputsWithCert(7), 0), EDecision::CERT_MISMATCH);
	// a revision 1 challenge names nothing: the client signs the hash of the certificate it checked itself
	EXPECT_EQ(Join.Decide(Everything(), ChallengeWithNonce(1), InputsWithCert(7), 0), EDecision::ANSWER);
}

TEST(KogAttest, ANonceIsAnsweredOnce)
{
	CJoin Join;
	const SChallenge Challenge = ChallengeWithNonce(1);
	const SInputs Inputs = InputsWithCert(7);
	ASSERT_EQ(Join.Decide(Everything(), Challenge, Inputs, 0), EDecision::ANSWER);
	Join.Commit(Challenge, 0);
	// the same nonce again: replayed, also long after
	EXPECT_EQ(Join.Decide(Everything(), Challenge, Inputs, 0), EDecision::REPLAY);
	EXPECT_EQ(Join.Decide(Everything(), Challenge, Inputs, 10 * MIN_SPACING_MS), EDecision::REPLAY);
	// a new nonce is another challenge
	EXPECT_EQ(Join.Decide(Everything(), ChallengeWithNonce(2), Inputs, 10 * MIN_SPACING_MS), EDecision::ANSWER);
}

TEST(KogAttest, AtMostFourAnswersASpacingApartPerJoin)
{
	CJoin Join;
	const SInputs Inputs = InputsWithCert(7);
	int64_t Now = 1000;
	for(int i = 0; i < MAX_ANSWERS_PER_JOIN; i++)
	{
		const SChallenge Challenge = ChallengeWithNonce((uint8_t)(i + 1));
		ASSERT_EQ(Join.Decide(Everything(), Challenge, Inputs, Now), EDecision::ANSWER) << i;
		Join.Commit(Challenge, Now);
		// a challenge right after is too soon
		EXPECT_EQ(Join.Decide(Everything(), ChallengeWithNonce(100), Inputs, Now + MIN_SPACING_MS - 1), i + 1 < MAX_ANSWERS_PER_JOIN ? EDecision::TOO_SOON : EDecision::TOO_MANY);
		Now += MIN_SPACING_MS;
	}
	EXPECT_EQ(Join.Answers(), MAX_ANSWERS_PER_JOIN);
	EXPECT_EQ(Join.Decide(Everything(), ChallengeWithNonce(100), Inputs, Now + 10 * MIN_SPACING_MS), EDecision::TOO_MANY);
	// a new connection starts again
	Join.Reset();
	EXPECT_EQ(Join.Answers(), 0);
	EXPECT_EQ(Join.Decide(Everything(), ChallengeWithNonce(1), Inputs, 0), EDecision::ANSWER);
}

TEST(KogAttest, EveryDecisionHasALabel)
{
	for(EDecision Decision : {EDecision::ANSWER, EDecision::SWITCH_OFF, EDecision::NOT_KOG, EDecision::NO_CAPABILITY, EDecision::NO_KEY, EDecision::CERT_BAD, EDecision::CERT_REQUIRED, EDecision::CERT_MISMATCH, EDecision::REPLAY, EDecision::TOO_MANY, EDecision::TOO_SOON})
		EXPECT_NE(str_length(DecisionLabel(Decision)), 0);
}
