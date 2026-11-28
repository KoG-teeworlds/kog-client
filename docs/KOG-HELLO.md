# KoG hello: which KoG features this client uses with a server

A KoG server can offer features that other servers do not have (today: proving your KoG account with the device key
when you join). Client and server first tell each other what they support and then use **only what both support**.
Everything else stays off, without an error and without a message to you. Servers that are not KoG servers are never
asked.

## What the client sends, and to whom

One message, `kog-client-hello@kog.tw`, right after the normal DDNet version report when you connect:

| Field | Meaning |
| --- | --- |
| revision | version of this layout, now `1`; a newer revision only adds fields at the end |
| ids | the numbers of the KoG features this client supports right now (at most 64) |
| build | the client version, for example `0.1.0` or `0.2.0-canary.57` |
| channel | `stable`, `canary` or `dev` |

It contains no account name, no device id, no key, no address of yours and no random per-install value. It is not
encrypted (the game connection is not), so treat the build and the channel as public.

**The client sends it only if all of these are true:**

1. `kog_hello` is on.
2. The server's address is listed for the `kog` community in the server list. A server cannot claim that for itself.
   Typing an address of a KoG server that the list does not show means no hello.
3. It is the main connection (not the dummy) and not a 0.7 connection.
4. The client has at least one feature to offer.

If one is false, nothing is sent, and the connection is exactly the one a stock DDNet client would make. A vanilla
server never sees a byte of it.

## Features

The numbers are written once and never reused. They are the same on every client and server.

| Id | Name | Offered when | What it enables |
| --- | --- | --- | --- |
| 1 | `client_login` | `cl_kog_login` and `cl_kog_auto_login` are on and this device has a key | the client may prove your account at join (see [LOGIN.md](LOGIN.md)) |

The list is generated from one table in `src/engine/shared/kog_protocol.h` (`KOG_CAPABILITIES`); adding a feature is
one line there. A feature is offered only if its own settings are on.

## What the client does with the answer

The server answers once with `kog-server-hello@kog.tw`: its revision and the ids it agrees to. The client keeps the ids
that it also offered, for this connection only. Ids it does not know are ignored, a malformed answer counts as no
answer, only the first answer after a hello is read, and an answer without a hello is ignored. No answer means no
feature, and the game goes on as normal.

Features ask the component and never look at a version number: `GameClient()->m_KogHello.Has(EKogCap::CLIENT_LOGIN)`.
The join proof answers a challenge only if `client_login` was agreed in this connection (in addition to the checks in
[LOGIN.md](LOGIN.md)); without it the challenge, the proof and the result messages are never read or sent.

## Settings

| Setting | Default | Effect |
| --- | --- | --- |
| `kog_hello` | off in stable, on in canary and dev builds | Off: no hello is ever sent, the client behaves as a stock client towards every server |

Console: `kog_caps` prints what this client offers, whether a hello was sent and answered on this connection, and what
was agreed. `kog_version` prints the version.

## Tests

`src/test/kog_hello_test.cpp`: the table (ids unique, `client_login` is 1), the two message UUIDs, the bytes of a
hello, only the offered ids are listed, long texts, every malformed answer, unknown ids and a newer revision, that a
server cannot enable what the client did not offer, and the decision to send (vanilla server, switch off, dummy, 0.7,
empty offer: nothing). The test of the join proof decision also checks that it needs the agreed feature. Not tested
automatically: the exchange with a running KoG server.

## See also

What the client can and cannot prove about itself, and the planned `client_attest` proof: [CLIENT-ATTESTATION.md](https://github.com/KoG-teeworlds/kog-2.0-server/blob/main/docs/CLIENT-ATTESTATION.md) in `kog-2.0-server` (design, not built).
