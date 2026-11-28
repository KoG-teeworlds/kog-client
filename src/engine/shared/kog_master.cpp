#include "kog_master.h"

#include <base/str.h>

#include <engine/external/monocypher/monocypher-ed25519.h>
#include <engine/shared/json.h>
#include <engine/shared/kog_login.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace KogMaster
{
static const char DELEGATION_PREFIX[] = "kog-master-delegation-v1\n";
static const char LIST_PREFIX[] = "kog-master-list-v1\n";

const char *ResultName(EResult Result)
{
	switch(Result)
	{
	case EResult::OK: return "ok";
	case EResult::NO_KEYS: return "no keys";
	case EResult::MALFORMED: return "malformed";
	case EResult::TOO_BIG: return "too big";
	case EResult::BAD_ROOT: return "unknown root key";
	case EResult::BAD_DELEGATION_SIGNATURE: return "bad delegation signature";
	case EResult::BAD_DELEGATION: return "bad delegation";
	case EResult::WRONG_CHANNEL: return "wrong channel";
	case EResult::DELEGATION_EXPIRED: return "delegation expired";
	case EResult::DELEGATION_ROLLBACK: return "delegation rollback";
	case EResult::BAD_LIST_SIGNATURE: return "bad list signature";
	case EResult::BAD_LIST: return "bad list";
	case EResult::LIST_EXPIRED: return "list expired";
	case EResult::LIST_ROLLBACK: return "list rollback";
	}
	return "unknown";
}

EOutcome Classify(EResult Result)
{
	if(Result == EResult::OK)
		return EOutcome::ACCEPT;
	if(Result == EResult::LIST_ROLLBACK || Result == EResult::DELEGATION_ROLLBACK)
		return EOutcome::KEEP_CURRENT;
	return EOutcome::REJECT;
}

bool UrlAllowed(const char *pUrl, const char *pDevBaseUrl)
{
	const size_t UrlLength = str_length(pUrl);
	if(UrlLength == 0 || UrlLength > MAX_URL_LENGTH)
		return false;
	// The base is only used for its own origin: without a dev base it is a kog.tw address, so only https://*.kog.tw:443 passes
	const char *pBase = pDevBaseUrl[0] != '\0' ? pDevBaseUrl : "https://client.kog.tw";
	return KogLogin::EndpointAllowed(pBase, pUrl, "");
}

bool DdnetFallbackAllowed(const char *pUrl)
{
	static const char *const ALLOWED[] = {
		"https://master1.ddnet.org/ddnet/15/servers.json",
		"https://master2.ddnet.org/ddnet/15/servers.json",
		"https://master3.ddnet.org/ddnet/15/servers.json",
		"https://master4.ddnet.org/ddnet/15/servers.json",
	};
	for(const char *pAllowed : ALLOWED)
	{
		if(str_comp(pUrl, pAllowed) == 0)
			return true;
	}
	return false;
}

bool AddressAllowed(const char *pAddress)
{
	const char *pRest = str_startswith(pAddress, "tw-0.6+udp://");
	if(!pRest)
		pRest = str_startswith(pAddress, "tw-0.7+udp://");
	if(!pRest || str_length(pRest) > 80)
		return false;
	for(const char *p = pRest; *p; p++)
	{
		if(!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F') || *p == '.' || *p == ':' || *p == '[' || *p == ']'))
			return false;
	}
	return true;
}

static bool GetInt(const json_value *pObject, const char *pName, int64_t *pOut)
{
	const json_value *pValue = json_object_get(pObject, pName);
	if(pValue->type != json_integer)
		return false;
	*pOut = pValue->u.integer;
	return true;
}

static bool GetString(const json_value *pObject, const char *pName, std::string *pOut, size_t MaxLength)
{
	const json_value *pValue = json_object_get(pObject, pName);
	if(pValue->type != json_string || pValue->u.string.length == 0 || pValue->u.string.length > MaxLength)
		return false;
	*pOut = pValue->u.string.ptr;
	return true;
}

static bool GetPayload(const json_value *pObject, const char *pName, const char *pSigName, std::string *pPayload, unsigned char (&aSignature)[64])
{
	const json_value *pPart = json_object_get(pObject, pName);
	if(pPart->type != json_object)
		return false;
	std::string Signature;
	if(!GetString(pPart, "payload", pPayload, MAX_DOCUMENT_SIZE) || !GetString(pPart, pSigName, &Signature, 128) || Signature.size() != 128)
		return false;
	return str_hex_decode(aSignature, sizeof(aSignature), Signature.c_str()) == 0;
}

static bool Verified(const std::string &Payload, const char *pPrefix, const unsigned char (&aSignature)[64], const unsigned char *pKey)
{
	std::string Message = pPrefix;
	Message += Payload;
	return crypto_ed25519_check(aSignature, pKey, reinterpret_cast<const uint8_t *>(Message.data()), Message.size()) == 0;
}

static bool ParseServers(const json_value *pServers, std::vector<SServer> *pOut)
{
	if(pServers->type != json_array || pServers->u.array.length > MAX_SERVERS)
		return false;
	for(unsigned i = 0; i < pServers->u.array.length; i++)
	{
		const json_value *pEntry = pServers->u.array.values[i];
		if(pEntry->type != json_object)
			return false;
		SServer Server;
		std::string Community;
		int64_t Players, Max;
		if(!GetString(pEntry, "id", &Server.m_Id, MAX_TEXT_LENGTH) || !GetString(pEntry, "name", &Server.m_Name, MAX_TEXT_LENGTH) ||
			!GetString(pEntry, "map", &Server.m_Map, MAX_TEXT_LENGTH) || !GetString(pEntry, "version", &Server.m_Version, MAX_TEXT_LENGTH) ||
			!GetString(pEntry, "community", &Community, MAX_TEXT_LENGTH) || !GetInt(pEntry, "players", &Players) || !GetInt(pEntry, "max", &Max))
			return false;
		if(Community != "kog" || Players < 0 || Max < 0 || Max > 1024 || Players > Max)
			return false;
		if(!KogUpdate::IsPlainText(Server.m_Name.c_str(), MAX_TEXT_LENGTH) || !KogUpdate::IsPlainText(Server.m_Map.c_str(), MAX_TEXT_LENGTH) || !KogUpdate::IsPlainText(Server.m_Version.c_str(), MAX_TEXT_LENGTH))
			return false;
		const json_value *pAddresses = json_object_get(pEntry, "addresses");
		if(pAddresses->type != json_array || pAddresses->u.array.length == 0 || pAddresses->u.array.length > MAX_ADDRESSES)
			return false;
		for(unsigned a = 0; a < pAddresses->u.array.length; a++)
		{
			const json_value *pAddress = pAddresses->u.array.values[a];
			if(pAddress->type != json_string || !AddressAllowed(pAddress->u.string.ptr))
				return false;
			Server.m_vAddresses.emplace_back(pAddress->u.string.ptr);
		}
		Server.m_Players = (int)Players;
		Server.m_MaxPlayers = (int)Max;
		pOut->push_back(std::move(Server));
	}
	return true;
}

EResult Verify(const char *pBody, size_t Length, const SVerifyInput &Input, SList *pOut)
{
	*pOut = SList();
	if(Input.m_NumRoots <= 0)
		return EResult::NO_KEYS;
	if(Length == 0 || Length > MAX_DOCUMENT_SIZE)
		return EResult::TOO_BIG;
	json_value *pDocument = JsonParse(pBody, Length);
	if(pDocument == nullptr)
		return EResult::MALFORMED;
	EResult Result = EResult::MALFORMED;
	json_value *pDelegation = nullptr;
	json_value *pList = nullptr;
	do
	{
		int64_t Version = 0;
		std::string DelegationPayload, ListPayload, RootId;
		unsigned char aDelegationSig[64], aListSig[64];
		if(pDocument->type != json_object || !GetInt(pDocument, "v", &Version) || Version != 1 ||
			!GetPayload(pDocument, "delegation", "sig", &DelegationPayload, aDelegationSig) ||
			!GetPayload(pDocument, "list", "sig", &ListPayload, aListSig) ||
			!GetString(json_object_get(pDocument, "delegation"), "root", &RootId, 8) || RootId.size() != 8)
			break;
		unsigned char aRootId[4];
		if(str_hex_decode(aRootId, sizeof(aRootId), RootId.c_str()) != 0)
			break;

		const KogUpdate::SPublicKey *pRoot = nullptr;
		for(int i = 0; i < Input.m_NumRoots; i++)
		{
			if(memcmp(Input.m_pRoots[i].m_aKey, aRootId, sizeof(aRootId)) == 0)
				pRoot = &Input.m_pRoots[i];
		}
		if(pRoot == nullptr)
		{
			Result = EResult::BAD_ROOT;
			break;
		}
		if(!Verified(DelegationPayload, DELEGATION_PREFIX, aDelegationSig, pRoot->m_aKey))
		{
			Result = EResult::BAD_DELEGATION_SIGNATURE;
			break;
		}

		pDelegation = JsonParse(DelegationPayload.c_str(), DelegationPayload.size());
		std::string Env, Kid, Pub;
		int64_t DVersion = 0, NotBefore = 0, NotAfter = 0, DSeq = 0;
		if(pDelegation == nullptr || pDelegation->type != json_object || !GetInt(pDelegation, "v", &DVersion) || DVersion != 1 ||
			!GetString(pDelegation, "env", &Env, 16) || !GetString(pDelegation, "kid", &Kid, 8) || !GetString(pDelegation, "pub", &Pub, 64) ||
			!GetInt(pDelegation, "not_before", &NotBefore) || !GetInt(pDelegation, "not_after", &NotAfter) || !GetInt(pDelegation, "seq", &DSeq))
		{
			Result = EResult::BAD_DELEGATION;
			break;
		}
		unsigned char aDelegatedKey[32];
		if(Pub.size() != 64 || str_hex_decode(aDelegatedKey, sizeof(aDelegatedKey), Pub.c_str()) != 0 || Kid.size() != 8 || str_comp(Kid.c_str(), Pub.substr(0, 8).c_str()) != 0 ||
			NotAfter <= NotBefore || NotAfter - NotBefore > MAX_DELEGATION_SECONDS || DSeq < 1)
		{
			Result = EResult::BAD_DELEGATION;
			break;
		}
		if(Env != Input.m_pChannel)
		{
			Result = EResult::WRONG_CHANNEL;
			break;
		}
		if(Input.m_Now < NotBefore - CLOCK_SKEW_SECONDS || Input.m_Now > NotAfter + CLOCK_SKEW_SECONDS + (Input.m_AllowStale ? MAX_STALE_SECONDS : 0))
		{
			Result = EResult::DELEGATION_EXPIRED;
			break;
		}
		if(DSeq < Input.m_LastDelegationSeq)
		{
			Result = EResult::DELEGATION_ROLLBACK;
			break;
		}
		if(!Verified(ListPayload, LIST_PREFIX, aListSig, aDelegatedKey))
		{
			Result = EResult::BAD_LIST_SIGNATURE;
			break;
		}

		pList = JsonParse(ListPayload.c_str(), ListPayload.size());
		std::string LEnv, LKid;
		int64_t LVersion = 0;
		if(pList == nullptr || pList->type != json_object || !GetInt(pList, "v", &LVersion) || LVersion != 1 || !GetString(pList, "env", &LEnv, 16) ||
			!GetString(pList, "kid", &LKid, 8) || !GetInt(pList, "seq", &pOut->m_Seq) || !GetInt(pList, "issued", &pOut->m_Issued) || !GetInt(pList, "not_after", &pOut->m_NotAfter) ||
			LEnv != Input.m_pChannel || LKid != Kid || pOut->m_Seq < 1)
		{
			Result = EResult::BAD_LIST;
			break;
		}
		const json_value *pMasters = json_object_get(pList, "masters");
		if(pMasters->type != json_array || pMasters->u.array.length > MAX_MASTERS)
		{
			Result = EResult::BAD_LIST;
			break;
		}
		bool MastersOk = true;
		for(unsigned i = 0; i < pMasters->u.array.length && MastersOk; i++)
		{
			const json_value *pMaster = pMasters->u.array.values[i];
			std::string Type, Url;
			if(pMaster->type != json_object || !GetString(pMaster, "t", &Type, 16) || !GetString(pMaster, "u", &Url, MAX_URL_LENGTH))
			{
				MastersOk = false;
				break;
			}
			// An entry of an unknown type or of a host that is not allowed is dropped, not an error: the list stays usable
			if(Type == "kog" && UrlAllowed(Url.c_str(), Input.m_pDevBaseUrl))
				pOut->m_vMasters.push_back(Url);
			else if(Type == "ddnet-json" && DdnetFallbackAllowed(Url.c_str()))
				pOut->m_vDdnetFallbacks.push_back(Url);
		}
		if(!MastersOk || !ParseServers(json_object_get(pList, "servers"), &pOut->m_vServers))
		{
			Result = EResult::BAD_LIST;
			break;
		}
		if(Input.m_Now > pOut->m_NotAfter + CLOCK_SKEW_SECONDS + (Input.m_AllowStale ? MAX_STALE_SECONDS : 0))
		{
			Result = EResult::LIST_EXPIRED;
			break;
		}
		if(pOut->m_Seq < Input.m_LastListSeq)
		{
			Result = EResult::LIST_ROLLBACK;
			break;
		}
		pOut->m_Env = LEnv;
		pOut->m_DelegationSeq = DSeq;
		Result = EResult::OK;
	} while(false);
	json_value_free(pList);
	json_value_free(pDelegation);
	json_value_free(pDocument);
	if(Result != EResult::OK)
		*pOut = SList();
	return Result;
}

SState ParseState(const char *pText, const char *pChannel)
{
	SState State;
	char aPrefix[64];
	str_format(aPrefix, sizeof(aPrefix), "kog-master-state-v1 %s ", pChannel);
	if(pText == nullptr || !str_startswith(pText, aPrefix))
		return State;
	long long Delegation = 0, ListSeq = 0;
	if(sscanf(pText + str_length(aPrefix), "%lld %lld", &Delegation, &ListSeq) == 2 && Delegation >= 0 && ListSeq >= 0)
	{
		State.m_DelegationSeq = Delegation;
		State.m_ListSeq = ListSeq;
	}
	return State;
}

std::string SerializeState(const char *pChannel, const SState &State)
{
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "kog-master-state-v1 %s %lld %lld\n", pChannel, (long long)State.m_DelegationSeq, (long long)State.m_ListSeq);
	return aBuf;
}
}
