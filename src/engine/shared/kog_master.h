#ifndef ENGINE_SHARED_KOG_MASTER_H
#define ENGINE_SHARED_KOG_MASTER_H

#include <engine/shared/kog_update.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The signed server list of the KoG master, format in docs/MASTER.md
namespace KogMaster
{
constexpr size_t MAX_DOCUMENT_SIZE = 1024 * 1024;
constexpr size_t MAX_SERVERS = 2000;
constexpr size_t MAX_MASTERS = 8;
constexpr size_t MAX_ADDRESSES = 4;
constexpr size_t MAX_URL_LENGTH = 200;
constexpr size_t MAX_TEXT_LENGTH = 64;
constexpr int64_t CLOCK_SKEW_SECONDS = 2ll * 24 * 3600;
constexpr int64_t MAX_DELEGATION_SECONDS = 45ll * 24 * 3600;
// A list that is expired by more than this is not used even when the master cannot be reached
constexpr int64_t MAX_STALE_SECONDS = 7ll * 24 * 3600;

struct SServer
{
	std::string m_Id;
	std::vector<std::string> m_vAddresses;
	std::string m_Name;
	std::string m_Map;
	std::string m_Version;
	int m_Players = 0;
	int m_MaxPlayers = 0;
};

struct SList
{
	std::string m_Env;
	int64_t m_Seq = 0;
	int64_t m_Issued = 0;
	int64_t m_NotAfter = 0;
	int64_t m_DelegationSeq = 0;
	// Further signed lists, in the order to try them, and the unsigned DDNet lists of the fallback
	std::vector<std::string> m_vMasters;
	std::vector<std::string> m_vDdnetFallbacks;
	std::vector<SServer> m_vServers;
};

enum class EResult
{
	OK,
	NO_KEYS,
	MALFORMED,
	TOO_BIG,
	BAD_ROOT,
	BAD_DELEGATION_SIGNATURE,
	BAD_DELEGATION,
	WRONG_CHANNEL,
	DELEGATION_EXPIRED,
	DELEGATION_ROLLBACK,
	BAD_LIST_SIGNATURE,
	BAD_LIST,
	LIST_EXPIRED,
	LIST_ROLLBACK,
};

const char *ResultName(EResult Result);

enum class EOutcome
{
	ACCEPT,
	// A copy older than the one already seen (a CDN serves different copies inside its TTL): ignored, no error, no next source
	KEEP_CURRENT,
	REJECT,
};
EOutcome Classify(EResult Result);

struct SVerifyInput
{
	const char *m_pChannel = "";
	const KogUpdate::SPublicKey *m_pRoots = nullptr;
	int m_NumRoots = 0;
	int64_t m_Now = 0;
	int64_t m_LastDelegationSeq = 0;
	int64_t m_LastListSeq = 0;
	// A cached list is checked again on load: it may be expired by up to MAX_STALE_SECONDS
	bool m_AllowStale = false;
	// Hosts the master URLs and the list may name besides https://*.kog.tw (only a DEV build sets this)
	const char *m_pDevBaseUrl = "";
};

EResult Verify(const char *pBody, size_t Length, const SVerifyInput &Input, SList *pOut);

// https on port 443 under kog.tw, or the same origin as m_pDevBaseUrl
bool UrlAllowed(const char *pUrl, const char *pDevBaseUrl);
bool AddressAllowed(const char *pAddress);
// The third-party lists the fallback may name: exactly the URLs of serverbrowser_http.cpp
bool DdnetFallbackAllowed(const char *pUrl);

struct SState
{
	int64_t m_DelegationSeq = 0;
	int64_t m_ListSeq = 0;
};

// "kog-master-state-v1 <channel> <delegation seq> <list seq>"
SState ParseState(const char *pText, const char *pChannel);
std::string SerializeState(const char *pChannel, const SState &State);
}

#endif
