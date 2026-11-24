#ifndef ENGINE_SHARED_KOG_HELLO_H
#define ENGINE_SHARED_KOG_HELLO_H

#include <engine/shared/kog_protocol.h>

#include <cstdint>

class CAbstractPacker;
class CUnpacker;

// The client side of the capability handshake with a KoG server, see docs/KOG-HELLO.md.
// The sets are 32 bit masks, bit n is the capability at place n of KOG_CAPABILITIES.
namespace KogHello {

enum
{
	REVISION = 1,
	// a server hello with more ids than this is not understood
	MAX_LISTED = 64,
	MAX_ID = 0xFFFF,
};

static_assert((int)EKogCap::COUNT <= 32, "a set of capabilities is a 32 bit mask");

inline uint32_t Bit(EKogCap Cap) { return 1u << (int)Cap; }
inline bool Has(uint32_t Mask, EKogCap Cap) { return (Mask & Bit(Cap)) != 0; }

int WireId(EKogCap Cap);
const char *Label(EKogCap Cap);

struct SHelloDecision
{
	bool m_SwitchOn; // kog_hello
	bool m_ServerIsKog; // the address is listed for the KoG community in the server list
	bool m_MainConnection; // not the dummy
	bool m_Sixup; // a 0.7 connection
	uint32_t m_Offer; // what the client would announce
};
// Whether the client says hello to the server it is connecting to. Vanilla and other servers, 0.7 connections, the
// dummy and an empty offer: never.
bool MaySendHello(const SHelloDecision &Decision);

// revision, number of ids, the ids, build, channel. Only the capabilities in the offer are listed.
void PackClientHello(CAbstractPacker *pPacker, uint32_t Offer, const char *pBuild, const char *pChannel);

struct SServerHello
{
	int m_Revision = 0;
	uint32_t m_Mask = 0; // what the server lists and this build knows
	int m_NumUnknown = 0;
};
// False: not understood, the client keeps nothing from it
bool UnpackServerHello(CUnpacker *pUnpacker, SServerHello *pHello);

// What may be used: listed by the server AND offered by this client
inline uint32_t Agreed(uint32_t Offer, uint32_t ServerListed) { return Offer & ServerListed; }

// "client_login,..." or "none"
void FormatMask(uint32_t Mask, char *pBuf, int BufSize);

}

#endif
