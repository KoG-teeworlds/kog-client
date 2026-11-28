#ifndef ENGINE_CLIENT_KOG_MASTER_LIST_H
#define ENGINE_CLIENT_KOG_MASTER_LIST_H

#include <engine/shared/kog_master.h>

#include <memory>
#include <string>
#include <vector>

class IHttp;
class IHttpRequest;
class IStorage;

// Fetches and verifies the signed list of the KoG master. Any failure leaves the list as it was and is only logged.
class CKogMasterList
{
public:
	void Init(IStorage *pStorage, IHttp *pHttp);
	// Starts a fetch when the feature is on and none is running or finished less than a short while ago
	void Refresh();
	void Update();
	void Shutdown();

	bool Enabled() const;
	bool Fetching() const { return m_pRequest != nullptr; }
	// True once after a new list was accepted
	bool TakeChanged();
	const std::vector<KogMaster::SServer> &Servers() const { return m_List.m_vServers; }
	bool Stale() const { return m_Stale; }
	const std::vector<std::string> &Urls() const { return m_vUrls; }

private:
	void BuildUrls();
	void StartAttempt();
	bool Accept(const char *pBody, size_t Length, bool FromCache);
	void LoadCache();
	void Save(const char *pBody, size_t Length);
	int RootKeys(std::vector<KogUpdate::SPublicKey> *pOut) const;

	IStorage *m_pStorage = nullptr;
	IHttp *m_pHttp = nullptr;
	std::shared_ptr<IHttpRequest> m_pRequest;
	std::vector<std::string> m_vUrls;
	std::vector<std::string> m_vLearnedMasters;
	// Unsigned DDNet lists the signed list names; not used until the DDNet list is switchable
	std::vector<std::string> m_vLearnedFallbacks;
	size_t m_Attempt = 0;
	int64_t m_LastStart = 0;
	KogMaster::SList m_List;
	KogMaster::SState m_State;
	bool m_Changed = false;
	bool m_Stale = false;
};

#endif
