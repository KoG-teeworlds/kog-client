#ifndef ENGINE_SHARED_KOG_PROTOCOL_H
#define ENGINE_SHARED_KOG_PROTOCOL_H

// The messages kog-client-hello and kog-server-hello are registered in protocol_ex_msgs.h, see docs/KOG-HELLO.md

// What this client can do together with a KoG server: the only list. Wire id (written once, never reused or
// renumbered), name for the console. A feature gets a line here when it is built.
#define KOG_CAPABILITIES(X) \
	X(CLIENT_LOGIN, 1, "client_login") \
	X(NOTICES, 2, "notices")

enum class EKogCap
{
#define X(Name, Id, Label) Name,
	KOG_CAPABILITIES(X)
#undef X
		COUNT
};

#endif
