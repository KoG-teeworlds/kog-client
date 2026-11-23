#ifndef ENGINE_SHARED_KOG_PROTOCOL_H
#define ENGINE_SHARED_KOG_PROTOCOL_H

// Names only; the messages are registered together with their handlers
#define KOG_MSG_CLIENT_HELLO "kog-client-hello@kog.tw"
#define KOG_MSG_SERVER_HELLO "kog-server-hello@kog.tw"

enum EKogCapability : unsigned
{
	KOG_CAP_NONE = 0,
	KOG_CAP_AUTH_BIND = 1u << 0,
	KOG_CAP_GUEST_ID = 1u << 1,
	KOG_CAP_TELEMETRY = 1u << 2,
	KOG_CAP_ATTESTATION = 1u << 3,
};

#endif
