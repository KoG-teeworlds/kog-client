# KoG certificates of servers

A KoG server can prove to this client that KoG enrolled it. The client then knows the server is not just an entry in a
list. This page describes what the client checks and fetches. It says nothing about how a server gets its certificate.

## What is checked

A login challenge ([LOGIN.md](LOGIN.md), revision 2) carries a short chain of two certificates and a signature:

```
node certificate        issued by the KoG key (the "anchor"), names the issuer of the next one
server certificate      issued by the node certificate, holds the public key of the server
signature               by the server key over "KoG-server-v1" 0x00 || u8 len(addr) || addr || nonce[32] || session_id[16] || u32 BE ttl_ms
```

`addr` is the address this client dialled, from its own socket, spelled as in the login proof. The client accepts the
server only if all of these hold:

1. the node certificate verifies under one of the keys pinned in this build for the channel (`src/engine/client/kog_trust_pins.h`:
   stable uses the production key, canary and dev the staging key; two slots so a rotation overlaps);
2. the server certificate verifies under the key of the node certificate, and names it as issuer;
3. neither certificate is expired (one day of clock skew is allowed; certificates live 30 days);
4. the signature fits this address, this nonce, this session and this lifetime;
5. neither the server name nor the hash of its key is on the revocation list of the trust list (below), and that list
   is signed by a pinned key, is not expired, is not older than 24 hours when it was fetched, and did not go back in sequence.

If a certificate is present and any of this fails, the challenge is never answered, whatever the settings are. If the server
presents none, the client answers as before (it only needs the server to be listed for the KoG community and to have
agreed to the feature), unless `cl_kog_require_server_cert` is on. The certificate adds to the list check; it never replaces it.

## The trust list

One small file, signed, the same bytes from every host:

```json
{"v":1,"payload":"<base64 of the JSON below>","sig":{"<label>":"<base64 of 64 bytes>"}}
{"v":1,"environment":"production","seq":7,"issued_at":1800000000,"expires_at":1800172800,
 "anchors":{"v1":"<base64 of 32 bytes>"},"revoked":[{"subject_id":"...","pubkey_sha256":"<hex>","since":1799990000}]}
```

The signature is Ed25519 by a pinned key over `"kog-trust-bundle-v1" 0x00 || payload bytes`. Because it is signed, the host that
serves it is not trusted: any copy of the file is as good as any other, and a host that is wrong or old can only withhold
the file or hand out an older one that has not yet expired (the client refuses a lower `seq` and an expired file).
`anchors` is informational: **only keys compiled into the client are ever trusted**; a key that appears only in the file is
ignored. A change of the anchor is therefore a client release, like a change of the update keys (`docs/UPDATE_CHANNEL.md`).
`environment` must be the one of the channel, so a staging file is useless on a stable client.

Where the client looks, in this order, until one gives a verified file:

1. `cl_kog_trust_url`, if set (https and a `*.kog.tw` host, the rule of the login hosts; it never changes the pinned keys);
2. the master server of the environment: `https://master.client.kog.tw/list/trust.json` (stable), `master.client.canary.kog.tw`, `master.client.dev.kog.tw`;
3. the mirror: `https://client.kog.tw/list/<channel>/trust.json`.

Until the master server exists, step 2 does not answer and the mirror is used. The client does not care which host served
a file that verifies.

## When it fetches

Not on a timer. The client fetches only when a login is possible (`cl_kog_login` and `cl_kog_auto_login` on, `cl_kog_server_cert` on,
a key pinned for the channel) and then

- when it starts to connect to a server listed for KoG and the held list is older than 5 minutes;
- when a certificate fails or the held list is too old to check one (at most once in 5 minutes).

The list is kept in `kog_trust.json` in the save directory and read back with the same checks as a download. A bundle is at
most 64 KB; the request is a plain GET with `KoG-Client/<version> (<platform>; <arch>)` as user agent, no cookie, no install
id, no account, no device id. A missing or unusable list means no answer to the challenge (the `/login` code still works).
Revocation reaches a connecting client in about the edge cache time plus 5 minutes.

## Settings

| Setting | Default | Effect |
| --- | --- | --- |
| `cl_kog_server_cert` | 1 | 0: no check and no fetch; the list of servers alone decides, as before |
| `cl_kog_require_server_cert` | 1 in canary and dev, 0 in stable | 1: answer a challenge only from a server with a valid certificate |
| `cl_kog_trust_url` | empty | Where to look first, for staging |

Requiring a certificate is a safety setting, not a privacy one. It stays off in stable until all KoG servers present a
certificate, because turning it on earlier would end automatic login there; the plan is to switch the default to 1 in the
release after that. Without a key pinned for the channel (the placeholder state of the pins file) nothing is checked.

## What a valid certificate does not show

It shows that a key enrolled by KoG signed this challenge for the address this client dialled. It does not show that the
operator is trustworthy, and it does not encrypt the connection. A certificate does not name an address yet, so a server that
relays a challenge from a real server can still look valid for the length of that relay; the login proof itself is bound to the
dialled address and is refused for such a server.

## Attack table (client view)

| Attacker | Result |
| --- | --- |
| Serves a fake list from a host the client asks (master, mirror, network) | Not signed by a pinned key: ignored. Can only withhold the file |
| Replays an old list | Refused when `seq` is lower or the file is expired; an old unexpired list delays a revocation by at most its lifetime |
| Takes over the master or the mirror | Same as above: can withhold, cannot forge anchors or revocations |
| Spoofs DNS for the hosts | TLS fails, and a signed file is needed anyway |
| Presents a certificate of another environment or key | Not under a pinned key: the challenge is not answered |
| Server key stolen | Valid until revoked (list, about 5 minutes after a connect) or expired (30 days) |
| The signing key of KoG stolen | Can sign anything the client accepts. Fix: a new pinned key in a release |

## Tests

`src/test/kog_trust_test.cpp`: the node certificate that the platform's own code prints for the root `test-root-secret` (the same
bytes as its test), a server certificate and signature and a trust list made with the same keys, every refusal above, the wire
fields of revision 2, and the decision with `m_CertVerified`, `m_CertBad` and `m_CertRequired` in `kog_login_test.cpp`. Not
tested: the fetch against a real host, the settings box on screen, a real server.
