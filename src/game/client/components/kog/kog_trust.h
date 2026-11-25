#ifndef GAME_CLIENT_COMPONENTS_KOG_KOG_TRUST_H
#define GAME_CLIENT_COMPONENTS_KOG_KOG_TRUST_H

#include <engine/http.h>
#include <engine/shared/kog_login_wire.h>
#include <engine/shared/kog_trust.h>

#include <game/client/component.h>

#include <memory>

// Does this server prove that KoG enrolled it? Keeps the signed trust list (revocations) and checks the certificate
// chain of a login challenge against the keys pinned for this channel. See docs/SERVER-CERTIFICATES.md.
class CKogTrust : public CComponent
{
public:
	enum class EVerdict
	{
		NO_CERT, // the server presented nothing
		OK,
		INVALID, // chain, signature or expiry wrong, or no usable trust list: never answered
		REVOKED,
		NOT_CHECKED, // the check is off, or nothing is pinned for this channel
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;

	// The challenge of the server, already read. pAddress is the address this client dialled.
	EVerdict Check(const char *pAddress, const KogLoginWire::SChallenge &Challenge);
	EVerdict LastVerdict() const { return m_LastVerdict; }
	const char *LastSubject() const { return m_aLastSubject; }
	// SHA-256 of the game server certificate that the last Check of this connection accepted. False: none was accepted.
	bool LeafHash(uint8_t (&aHash)[32]) const;

	// A certificate failed: the list may have changed, look again (at most once in five minutes)
	void RefreshSoon();

private:
	KogTrust::SAnchors m_Anchors;
	const char *m_pEnvironment = "";
	KogTrust::SBundle m_Bundle;
	bool m_HasBundle = false;
	int64_t m_Fetched = 0; // unix seconds when the held list was fetched
	int64_t m_LastTry = 0;
	EVerdict m_LastVerdict = EVerdict::NOT_CHECKED;
	char m_aLastSubject[KogTrust::MAX_ID_SIZE + 1] = "";
	bool m_HasLeafHash = false;
	uint8_t m_aLeafHash[32] = {};

	std::shared_ptr<IHttpRequest> m_pRequest;
	int m_Source = 0;

	bool Enabled() const;
	void Load();
	void Save(const char *pFile, size_t Size);
	void Start(int Source);
	void Finish();
	bool SourceUrl(int Source, char *pBuf, size_t Size) const;
	void FormatAgent(char *pBuf, size_t Size) const;
};

#endif
