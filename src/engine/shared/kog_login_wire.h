#ifndef ENGINE_SHARED_KOG_LOGIN_WIRE_H
#define ENGINE_SHARED_KOG_LOGIN_WIRE_H

#include <cstdint>

class CAbstractPacker;
class CUnpacker;

// The three messages of the login proof between this client and a KoG server: kog-login-challenge (server to client),
// kog-login-proof (client to server) and kog-login-result (server to client). Never sent unless the server agreed to
// client_login in the KoG hello. Layouts: docs/LOGIN.md, and docs/protocol/login-proof.md of kog-2.0-server.
namespace KogLoginWire {

enum
{
	REVISION = 1,
	NONCE_SIZE = 32,
	ID_SIZE = 16,
	SIGNATURE_SIZE = 64,
	MAX_ACCOUNT_NAME = 64,
	// a server that asks for more or less than this is not asking for a login
	MIN_TTL_MS = 1000,
	MAX_TTL_MS = 5 * 60 * 1000,
};

struct SChallenge
{
	int m_Revision = 0;
	uint8_t m_aNonce[NONCE_SIZE] = {};
	uint8_t m_aSessionId[ID_SIZE] = {};
	int m_TtlMs = 0;
};

// Reads the fields after the message id. False: not understood, nothing is signed.
bool UnpackChallenge(CUnpacker *pUnpacker, SChallenge *pChallenge);

// revision, device id (16 bytes), signature (64 bytes)
void PackProof(CAbstractPacker *pPacker, const uint8_t (&aDeviceId)[ID_SIZE], const uint8_t (&aSignature)[SIGNATURE_SIZE]);

struct SResult
{
	int m_Revision = 0;
	bool m_LoggedIn = false;
	char m_aAccountName[MAX_ACCOUNT_NAME] = "";
};

// False: not understood. A refusal never carries a name.
bool UnpackResult(CUnpacker *pUnpacker, SResult *pResult);

}

#endif
