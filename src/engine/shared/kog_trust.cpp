#include "kog_trust.h"

#include <base/hash.h>
#include <base/mem.h>
#include <base/str.h>

#include <engine/external/monocypher/monocypher-ed25519.h>
#include <engine/shared/json.h>

#include <cstring>

namespace KogTrust {

namespace {

bool ReadField(const uint8_t *pData, size_t Size, size_t *pOffset, size_t MaxLength, const uint8_t **ppField, size_t *pLength)
{
	if(Size - *pOffset < 4)
		return false;
	const uint8_t *p = pData + *pOffset;
	const uint32_t Length = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
	*pOffset += 4;
	if(Length > MaxLength || Size - *pOffset < Length)
		return false;
	*ppField = pData + *pOffset;
	*pLength = Length;
	*pOffset += Length;
	return true;
}

void PutField(std::vector<uint8_t> &Out, const void *pField, size_t Length)
{
	for(int i = 0; i < 4; i++)
		Out.push_back(uint8_t(Length >> (8 * i)));
	Out.insert(Out.end(), static_cast<const uint8_t *>(pField), static_cast<const uint8_t *>(pField) + Length);
}

bool PrintableId(const std::string &Id)
{
	if(Id.empty() || Id.size() > MAX_ID_SIZE)
		return false;
	for(char c : Id)
	{
		if(c < 0x20 || c > 0x7e)
			return false;
	}
	return true;
}

int HexValue(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

std::vector<uint8_t> SigningBytes(const SCert &Cert)
{
	std::vector<uint8_t> Out(CERT_DOMAIN, CERT_DOMAIN + str_length(CERT_DOMAIN));
	PutField(Out, Cert.m_Subject.data(), Cert.m_Subject.size());
	PutField(Out, Cert.m_aPubkey, KEY_SIZE);
	PutField(Out, Cert.m_Issuer.data(), Cert.m_Issuer.size());
	for(int i = 0; i < 8; i++)
		Out.push_back(uint8_t(Cert.m_NotAfter >> (8 * i)));
	return Out;
}

bool Check(const uint8_t (&aSignature)[SIGNATURE_SIZE], const uint8_t *pKey, const std::vector<uint8_t> &Message)
{
	return crypto_ed25519_check(aSignature, pKey, Message.data(), Message.size()) == 0;
}

bool NotExpired(uint64_t NotAfter, uint64_t Now)
{
	return Now <= NotAfter + CLOCK_SKEW_SECONDS;
}

} // namespace

bool ParseCert(const uint8_t *pData, size_t Size, SCert *pCert, size_t *pUsed)
{
	*pCert = SCert();
	size_t Offset = 0;
	const uint8_t *pSubject, *pKey, *pIssuer;
	size_t SubjectLength, KeyLength, IssuerLength;
	if(!ReadField(pData, Size, &Offset, MAX_ID_SIZE, &pSubject, &SubjectLength) ||
		!ReadField(pData, Size, &Offset, KEY_SIZE, &pKey, &KeyLength) || KeyLength != KEY_SIZE ||
		!ReadField(pData, Size, &Offset, MAX_ID_SIZE, &pIssuer, &IssuerLength) ||
		Size - Offset < 8 + SIGNATURE_SIZE)
		return false;
	pCert->m_Subject.assign(reinterpret_cast<const char *>(pSubject), SubjectLength);
	pCert->m_Issuer.assign(reinterpret_cast<const char *>(pIssuer), IssuerLength);
	if(!PrintableId(pCert->m_Subject) || !PrintableId(pCert->m_Issuer))
		return false;
	mem_copy(pCert->m_aPubkey, pKey, KEY_SIZE);
	for(int i = 0; i < 8; i++)
		pCert->m_NotAfter |= uint64_t(pData[Offset + i]) << (8 * i);
	Offset += 8;
	mem_copy(pCert->m_aSignature, pData + Offset, SIGNATURE_SIZE);
	Offset += SIGNATURE_SIZE;
	*pUsed = Offset;
	return true;
}

bool VerifyCert(const SCert &Cert, const uint8_t (&aIssuerKey)[KEY_SIZE], uint64_t Now)
{
	return NotExpired(Cert.m_NotAfter, Now) && Check(Cert.m_aSignature, aIssuerKey, SigningBytes(Cert));
}

SAnchors AnchorsFromHex(const char *const *ppHex, size_t Count)
{
	SAnchors Anchors;
	for(size_t i = 0; i < Count; i++)
	{
		const char *pHex = ppHex[i];
		if(pHex == nullptr || str_length(pHex) != KEY_SIZE * 2)
			continue;
		std::vector<uint8_t> Key(KEY_SIZE);
		bool Valid = true;
		for(int j = 0; j < KEY_SIZE && Valid; j++)
		{
			const int High = HexValue(pHex[2 * j]), Low = HexValue(pHex[2 * j + 1]);
			Valid = High >= 0 && Low >= 0;
			Key[j] = uint8_t(High * 16 + Low);
		}
		if(Valid)
			Anchors.m_Keys.push_back(Key);
	}
	return Anchors;
}

EChain VerifyChain(const SAnchors &Anchors, const uint8_t *pChain, size_t Size, uint64_t Now, SCert *pGameServer)
{
	if(Anchors.m_Keys.empty())
		return EChain::NO_ANCHOR;
	SCert Node, Game;
	size_t Used = 0, Used2 = 0;
	if(Size > 2 * MAX_CERT_SIZE || !ParseCert(pChain, Size, &Node, &Used) || !ParseCert(pChain + Used, Size - Used, &Game, &Used2) || Used + Used2 != Size)
		return EChain::MALFORMED;
	if(Node.m_Issuer != "ca")
		return EChain::NODE_NOT_BY_ANCHOR;
	bool ByAnchor = false;
	for(const std::vector<uint8_t> &Key : Anchors.m_Keys)
	{
		uint8_t aKey[KEY_SIZE];
		mem_copy(aKey, Key.data(), KEY_SIZE);
		if(Check(Node.m_aSignature, aKey, SigningBytes(Node)))
		{
			ByAnchor = true;
			break;
		}
	}
	if(!ByAnchor)
		return EChain::NODE_NOT_BY_ANCHOR;
	if(!NotExpired(Node.m_NotAfter, Now) || !NotExpired(Game.m_NotAfter, Now))
		return EChain::EXPIRED;
	if(Game.m_Issuer != Node.m_Subject || !VerifyCert(Game, Node.m_aPubkey, Now))
		return EChain::GAME_NOT_BY_NODE;
	*pGameServer = Game;
	return EChain::OK;
}

size_t BuildServerSigned(uint8_t *pOut, size_t OutSize, const char *pAddress, const uint8_t *pNonce, size_t NonceSize, const uint8_t (&aSessionId)[16], uint32_t TtlMs)
{
	const size_t AddressLength = str_length(pAddress);
	const size_t DomainLength = str_length(SERVER_DOMAIN) + 1;
	const size_t Total = DomainLength + 1 + AddressLength + NonceSize + 16 + 4;
	if(AddressLength == 0 || AddressLength > 255 || NonceSize != 32 || Total > OutSize)
		return 0;
	size_t Offset = 0;
	mem_copy(pOut, SERVER_DOMAIN, DomainLength); // includes the 0 byte
	Offset += DomainLength;
	pOut[Offset++] = uint8_t(AddressLength);
	mem_copy(pOut + Offset, pAddress, AddressLength);
	Offset += AddressLength;
	mem_copy(pOut + Offset, pNonce, NonceSize);
	Offset += NonceSize;
	mem_copy(pOut + Offset, aSessionId, 16);
	Offset += 16;
	for(int i = 3; i >= 0; i--)
		pOut[Offset++] = uint8_t(TtlMs >> (8 * i));
	return Offset;
}

bool VerifyServerSignature(const uint8_t (&aServerKey)[KEY_SIZE], const char *pAddress, const uint8_t *pNonce, size_t NonceSize, const uint8_t (&aSessionId)[16], uint32_t TtlMs, const uint8_t (&aSignature)[SIGNATURE_SIZE])
{
	uint8_t aMessage[512];
	const size_t Length = BuildServerSigned(aMessage, sizeof(aMessage), pAddress, pNonce, NonceSize, aSessionId, TtlMs);
	return Length != 0 && crypto_ed25519_check(aSignature, aServerKey, aMessage, Length) == 0;
}

int Base64Decode(void *pDst, size_t DstSize, const char *pSrc, size_t SrcLength)
{
	if(SrcLength % 4 != 0)
		return -1;
	uint8_t *pOut = static_cast<uint8_t *>(pDst);
	size_t Out = 0;
	for(size_t i = 0; i < SrcLength; i += 4)
	{
		uint32_t Value = 0;
		int Pad = 0;
		for(int j = 0; j < 4; j++)
		{
			const char c = pSrc[i + j];
			int V;
			if(c >= 'A' && c <= 'Z')
				V = c - 'A';
			else if(c >= 'a' && c <= 'z')
				V = c - 'a' + 26;
			else if(c >= '0' && c <= '9')
				V = c - '0' + 52;
			else if(c == '+')
				V = 62;
			else if(c == '/')
				V = 63;
			else if(c == '=' && i + 4 == SrcLength && j >= 2)
			{
				V = 0;
				Pad++;
			}
			else
				return -1;
			if(Pad > 0 && c != '=')
				return -1;
			Value = Value << 6 | uint32_t(V);
		}
		const int Bytes = 3 - Pad;
		if(Out + Bytes > DstSize)
			return -1;
		for(int j = 0; j < Bytes; j++)
			pOut[Out++] = uint8_t(Value >> (16 - 8 * j));
	}
	return (int)Out;
}

namespace {

const json_value *Get(const json_value *pObject, const char *pKey, json_type Type)
{
	if(pObject == nullptr || pObject->type != json_object)
		return nullptr;
	const json_value *pValue = json_object_get(pObject, pKey);
	return pValue != nullptr && pValue->type == Type ? pValue : nullptr;
}

} // namespace

EBundle ParseBundle(const SAnchors &Anchors, const char *pEnvironment, const char *pFile, size_t Size, uint64_t Now, int64_t HeldSeq, SBundle *pBundle)
{
	*pBundle = SBundle();
	if(Anchors.m_Keys.empty())
		return EBundle::NO_ANCHOR;
	if(Size == 0 || Size > MAX_BUNDLE_SIZE)
		return EBundle::MALFORMED;
	json_value *pEnvelope = JsonParse(pFile, Size);
	const json_value *pPayloadText = Get(pEnvelope, "payload", json_string);
	const json_value *pSigs = Get(pEnvelope, "sig", json_object);
	const json_value *pVersion = Get(pEnvelope, "v", json_integer);
	if(pPayloadText == nullptr || pSigs == nullptr || pVersion == nullptr || pVersion->u.integer != 1)
	{
		json_value_free(pEnvelope);
		return EBundle::MALFORMED;
	}
	std::vector<uint8_t> Payload(pPayloadText->u.string.length / 4 * 3 + 3);
	const int PayloadSize = Base64Decode(Payload.data(), Payload.size(), pPayloadText->u.string.ptr, pPayloadText->u.string.length);
	if(PayloadSize <= 0)
	{
		json_value_free(pEnvelope);
		return EBundle::MALFORMED;
	}
	Payload.resize(PayloadSize);

	std::vector<uint8_t> Message(BUNDLE_DOMAIN, BUNDLE_DOMAIN + str_length(BUNDLE_DOMAIN) + 1);
	Message.insert(Message.end(), Payload.begin(), Payload.end());
	bool Signed = false;
	for(unsigned i = 0; i < pSigs->u.object.length && !Signed; i++)
	{
		const json_value *pSig = pSigs->u.object.values[i].value;
		uint8_t aSignature[SIGNATURE_SIZE];
		if(pSig->type != json_string || Base64Decode(aSignature, sizeof(aSignature), pSig->u.string.ptr, pSig->u.string.length) != SIGNATURE_SIZE)
			continue;
		for(const std::vector<uint8_t> &Key : Anchors.m_Keys)
		{
			if(crypto_ed25519_check(aSignature, Key.data(), Message.data(), Message.size()) == 0)
			{
				Signed = true;
				break;
			}
		}
	}
	json_value_free(pEnvelope);
	if(!Signed)
		return EBundle::BAD_SIGNATURE;

	json_value *pRoot = JsonParse(reinterpret_cast<const char *>(Payload.data()), Payload.size());
	const json_value *pEnvironmentValue = Get(pRoot, "environment", json_string);
	const json_value *pSeq = Get(pRoot, "seq", json_integer);
	const json_value *pIssued = Get(pRoot, "issued_at", json_integer);
	const json_value *pExpires = Get(pRoot, "expires_at", json_integer);
	const json_value *pRevoked = Get(pRoot, "revoked", json_array);
	if(pEnvironmentValue == nullptr || pSeq == nullptr || pIssued == nullptr || pExpires == nullptr || pRevoked == nullptr || pRevoked->u.array.length > MAX_REVOKED)
	{
		json_value_free(pRoot);
		return EBundle::MALFORMED;
	}
	if(str_comp(pEnvironmentValue->u.string.ptr, pEnvironment) != 0)
	{
		json_value_free(pRoot);
		return EBundle::WRONG_ENVIRONMENT;
	}
	pBundle->m_Environment = pEnvironmentValue->u.string.ptr;
	pBundle->m_Seq = pSeq->u.integer;
	pBundle->m_IssuedAt = pIssued->u.integer;
	pBundle->m_ExpiresAt = pExpires->u.integer;
	for(unsigned i = 0; i < pRevoked->u.array.length; i++)
	{
		const json_value *pEntry = pRevoked->u.array.values[i];
		const json_value *pSubject = Get(pEntry, "subject_id", json_string);
		const json_value *pHash = Get(pEntry, "pubkey_sha256", json_string);
		if(pSubject == nullptr && pHash == nullptr)
			continue;
		SRevoked Revoked;
		if(pSubject != nullptr)
			Revoked.m_Subject = pSubject->u.string.ptr;
		if(pHash != nullptr)
			Revoked.m_PubkeySha256Hex = pHash->u.string.ptr;
		pBundle->m_Revoked.push_back(Revoked);
	}
	json_value_free(pRoot);
	if(pBundle->m_ExpiresAt < 0 || Now > uint64_t(pBundle->m_ExpiresAt))
		return EBundle::EXPIRED;
	if(pBundle->m_Seq < HeldSeq)
		return EBundle::NOT_NEWER;
	return EBundle::OK;
}

bool IsRevoked(const SBundle &Bundle, const SCert &GameServer)
{
	char aHash[SHA256_MAXSTRSIZE];
	sha256_str(sha256(GameServer.m_aPubkey, KEY_SIZE), aHash, sizeof(aHash));
	for(const SRevoked &Revoked : Bundle.m_Revoked)
	{
		if(!Revoked.m_Subject.empty() && Revoked.m_Subject == GameServer.m_Subject)
			return true;
		if(!Revoked.m_PubkeySha256Hex.empty() && str_comp_nocase(Revoked.m_PubkeySha256Hex.c_str(), aHash) == 0)
			return true;
	}
	return false;
}

bool BundleUsable(const SBundle &Bundle, int64_t Fetched, uint64_t Now)
{
	return Fetched > 0 && int64_t(Now) >= Fetched && int64_t(Now) - Fetched <= MAX_BUNDLE_AGE_SECONDS && Now <= uint64_t(Bundle.m_ExpiresAt);
}

} // namespace KogTrust
