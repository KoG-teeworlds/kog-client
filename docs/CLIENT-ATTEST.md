# Client attestation: the install key answers a fresh challenge

A KoG server can ask this client to prove that it is the same install as the one that was registered on your account.
The answer is a signature with the device key from [LOGIN.md](LOGIN.md). It says "this is install K of an account", and
nothing else: not that the code is unmodified (nothing can prove that on a desktop, and a fork can do what this client does),
and nothing about the game. The design and its limits are in `docs/CLIENT-ATTESTATION.md` of kog-2.0-server (section 10);
this page is the client side.

## What is sent, and when

Two messages, never sent unless the server agreed to `client_attest` (id 4) in the KoG hello ([KOG-HELLO.md](KOG-HELLO.md)):

```
server -> client   kog-attest-challenge   revision, nonce (32 bytes), session id (16), time to live, up to 4 ranges,
                                          (revision 2) hash of the server certificate
client -> server   kog-attest-response    revision, key id (16 bytes), status, measurement (32 bytes), signature (64 bytes)
```

The key is the key of the player's account: a key exists only after the browser login registered it, and it is deleted when
you log out on this device. A player without an account, a client that is not enrolled and a locked key answer nothing. The
status is always `ok` when something is sent; there is no answer that says "no key" or "declined".

The client offers `client_attest` in the hello under the same conditions as `client_login`: `cl_kog_login` and
`cl_kog_auto_login` are on and this device has a key. There is no further setting: `cl_kog_auto_login` is the switch for
"prove this device to KoG servers", and the nonce proof is part of it.

## What is signed

```
"KoG-attest-v1" 0x00 || u8 len(addr) || addr || nonce[32] || session_id[16] || server_cert_hash[32]
  || SHA-256(client hello)[32] || key_id[16] || measurement[32]
```

| Part | Where it comes from in the client |
| --- | --- |
| `addr` | the address this client connected to (`KogLogin::CanonicalAddress`), never from a message of the server |
| `nonce`, `session_id` | the challenge |
| `server_cert_hash` | SHA-256 of the game server certificate that this client verified in the login challenge of this connection ([SERVER-CERTIFICATES.md](SERVER-CERTIFICATES.md)); 32 zero bytes when it verified none |
| client hello hash | SHA-256 of the fields of the `kog-client-hello` this connection sent (after the message id): revision, ids, build, channel |
| `key_id` | the first 16 bytes of SHA-256 of the public key |
| `measurement` | 32 zero bytes: this client makes no code measurement, and the ranges of a challenge are ignored |

Length prefixed or fixed width, with a domain at the front: two different tuples never give the same bytes, and the key never
signs anything else that this. The test vector is `test_vectors/kog-attest.json` of the protocol repository.

## When the client answers

All of these must be true (`KogAttest::CJoin::Decide`), otherwise nothing is sent and the game goes on:

- `cl_kog_login` and `cl_kog_auto_login` are on, the device key is there and can be read (not locked, not damaged);
- the server's address is listed for the `kog` community in the server list, and it agreed to `client_attest`;
- the server certificate is not wrong or revoked, and with `cl_kog_require_server_cert` it was proven;
- a revision 2 challenge names the certificate this client verified itself. A server that names another certificate, or one when
  the client verified none, is not answered. A revision 1 challenge names none: the client signs the hash of what it verified;
- the nonce was not answered before in this join, at most four answers per join, and not twice within a minute.

The count is made before the signature, so a failure is never tried again for the same challenge, and nothing runs on a timer.
Everything happens when a challenge arrives, and the work is one signature: no request, no wait.

## What the server does not tell the client

The answer to a response is not sent to the client. A key that was revoked, banned or removed gets no message that says why. What
the player can see, and what to do, is on the account page of the website: the list of keys and its revoke button.

## Tests

`src/test/kog_attest_test.cpp`: the golden vector of the protocol repository (message names, capability id, key id, the signed
bytes and signatures of every case, the hello hash, the layouts of the challenges and responses), each field changed in turn
breaks the signature, a signature made at run time with a test key, malformed challenges, and the decisions: no key, vanilla
and other servers, no agreed capability, switches off, wrong or revoked or missing server certificate, a replayed nonce, too many
or too quick challenges. Not tested automatically: the exchange with a running KoG server.
