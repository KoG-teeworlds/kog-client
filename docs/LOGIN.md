# Log in with KoG

The client logs in to your KoG account through your browser and then holds a device key pair. Nothing that could be
replayed ever travels over the game's network connection. This page describes what is built and what is not.

## Switches

| Setting | Default | Effect |
| --- | --- | --- |
| `cl_kog_login` | on in canary and dev builds, off in stable | Off: no browser flow and no automatic login. The `/login <code>` chat command keeps working |
| `cl_kog_auto_login` | off | Prove this device to KoG servers when joining (see "Join proof"). Off: the client never answers a login challenge, and does not offer the feature in the KoG hello |
| `cl_kog_account_url` | `https://kog.tw` | Base URL of the account server, for staging. https only |
| `cl_kog_account_extra_origins` | empty | Origins besides the account URL's own and `*.kog.tw` that its login description may name, `https://host[:port]` separated by spaces. See "Which hosts the login talks to" |
| `cl_kog_device_name` | empty | Name of this device on your account, at most 40 characters. Empty means `KoG Client (macOS)`, `(Windows)` or `(Linux)`: the host name is never sent unless you type it |
| `cl_kog_account_name` | empty | The account name shown in the settings. Not a secret |

## Flow

```
Client                           Browser                     kog.tw
  | GET /.well-known/openid-configuration ------------------->|
  |<----------------------------- authorize / token / userinfo |
  | listen on 127.0.0.1:<random port> (or [::1])               |
  | open authorize URL (PKCE S256, state, device_name) ------->|
  |                                |-- user logs in, agrees -->|
  |<-- GET /callback?code&state ---|<-- redirect --------------|
  | answer "You can close this tab", close the listener        |
  | POST token (code + code_verifier) ------------------------>|
  |<--------------------------------------------- access token |
  | GET userinfo (account name, for the settings) ------------>|
  | make Ed25519 key pair; private key into the key file       |
  | POST /api/client/device {device_name, public_key} -------->|
  |<--------------------------------------- 201 {id, name}    |
  | discard the access token                                   |
```

Log out on this device deletes the key file at once. The account server only accepts the removal of the device with a
token, so the client runs the browser step again and sends `DELETE /api/client/device/{id}`. If that is cancelled or fails,
the key is gone anyway and the device stays listed on kog.tw until you remove it there.

## Where the key is kept

One file, `kog_device.key`, in the save folder of this client. There is no use of the macOS Keychain, the Windows Credential
Manager or libsecret: the default is a plain file that only you can read, and the few people who want more can lock it with a
passphrase. The file holds only the device id and the 32 byte private key, nothing else.

| | Default | With a passphrase |
| --- | --- | --- |
| File | `v1`, the device id and the key as text | `KOGK` header, then the key encrypted |
| Protects against | Other users of the computer (mode 0600, your profile) | The same, and a stolen copy of the file |
| Does not protect against | Malware that runs as you | The same, once you unlocked the key in this session (it can read the client's memory) |
| Needed at start | Nothing | The passphrase, typed in the settings |

The passphrase is off by default and is not a setting: it is typed only in the settings box (Settings, DDNet, KoG account:
Lock the key, Unlock, Change passphrase, Remove passphrase), in a hidden field. It is never in a config file, a console command
or its history, or a log; `kog_key_passphrase` without an argument only says whether the key is locked. 8 to 128 characters.

A locked file is Argon2id (64 MiB, 3 passes, one lane, 16 byte random salt) into a 32 byte key for XChaCha20-Poly1305 with a
random 24 byte nonce, all from the Monocypher in the tree. The header (version, cost, salt, nonce) is authenticated. A file may
ask for at most 256 MiB and 8 passes, so a changed file cannot stall the start. Unlocking takes about a quarter of a second.

* Locked and not unlocked: you are not logged in, the settings say so, and no proof is answered. The client does not crash and
  does not make a new key.
* Wrong passphrase, or a file that was changed (the two cannot be told apart): the same message, and the file is not touched.
  A locked or unreadable file is never replaced by a login or by setting a passphrase. To start over, log out on this device
  (this deletes the file) and remove the device on kog.tw, because the id inside the locked key cannot be read.
* There is no way to recover a lost passphrase. Log out, remove the device on kog.tw and log in again.

Keys that an earlier canary or dev build put into the macOS Keychain or the Windows Credential Manager are moved into the file
at the first start (the old item is removed once the file reads back), and not again. This read-only step is meant to be deleted one
release after the first one that has it; after that, such a device has to be registered again and the old one removed on kog.tw.

## Which hosts the login talks to

The discovery document is only a hint where the account server lives, so its three endpoints (authorize, token, user
info) are checked before anything is sent to them. An endpoint must be https and have

1. the same origin (scheme, host and port) as `cl_kog_account_url`, or
2. a host that ends in `.kog.tw`, on https and the default port (the production discovery of `kog.tw` may name
   `auth.kog.tw`), or
3. an origin listed in `cl_kog_account_extra_origins`, for a staging setup whose identity provider runs on another host.

Everything else (another domain, `kog.tw.evil.example`, `kog.tw@evil.example`, another port, plain http) makes the login
fail with "unusable login description" before the browser is opened. The device calls go to `cl_kog_account_url` only.

The token, user info, device and removal requests do not follow redirects (a 307 or 308 would send the code and the
verifier again to a place the user never chose); a redirect there is a failure. After they finish, the client overwrites the
request body, the response and the header copies it holds. The browser build (emscripten) cannot switch redirects off
because the browser's fetch follows them; the origin check above is what protects it.

The authorization code may be up to 4096 characters (the callback request up to 8 KB): the league/oauth2-server library
hands out an encrypted token, which with a hex encoding easily passes 1 KB. A longer code is refused, not cut.

## Join proof

When you join a KoG server, the server can ask the client to prove your account with the device key. Nothing typed, nothing
reusable crosses the game connection. The messages are in `docs/protocol/login-proof.md` of the server repository:

```
server -> client   kog-login-challenge   nonce (32 bytes), session id (16 bytes), time to live
client -> server   kog-login-proof       device id (16 bytes), signature (64 bytes)
server -> client   kog-login-result      logged in or not, and the account name
```

`KogLogin::SignChallenge` signs, with the device key:

```
"KoG-login-v1" 0x00 || u8 len(addr) || addr || nonce[32] || device_id[16] || session_id[16]
```

The parts are length prefixed or fixed width, so two different tuples never give the same bytes (the first draft
concatenated them and `1.2.3.4:830` plus a nonce starting with `3` collided with `1.2.3.4:8303`). `addr` is the address
this client connected to, `ip:port` from its own socket and never from the server's message (`KogLogin::CanonicalAddress`:
IPv4 as `a.b.c.d:port`, an IPv4-mapped IPv6 address as IPv4, other IPv6 compressed and in brackets). A challenge cannot be
reused on another server, with another nonce, by another device or in another session: the server's backend rebuilds the
same bytes from its own record of that server's address and checks them.

The client answers a challenge only if all of these are true (`MayAnswerChallenge`):

- the server agreed to `client_login` in the KoG hello ([KOG-HELLO.md](KOG-HELLO.md)), which the client only offers while
  `cl_kog_login` and `cl_kog_auto_login` are on and this device has a key;
- the server's address is listed for the `kog` community in the server list (a server cannot claim that for itself);
- the key can be read: a key that is missing, locked or damaged answers nothing, and the settings box shows the state it
  has always shown;
- you did not already use `/login` in this join, and the client has not answered once already in this join.

Vanilla and other servers never get an answer. The challenge itself is checked before anything is signed: nonce and
session id of the right size, a time to live between 1 second and 5 minutes. It is answered at once, once per join.

The result is only read after the client sent a proof. "Logged in" is shown as a line in the chat (`[KoG] Logged in as
<name>`) and in the KoG account box of the settings (`Logged in on this server as <name>`). A refusal is one plain
line: the server did not accept this device. It does not say why (unknown, removed, wrong key and an error all look the
same, on purpose), and you play as a guest; `/login <code>` still works. The name is the account name the backend found
for the device; the client never tells the server which account it wants.

A server that is not listed as a KoG server, or that did not agree to the feature, can send a challenge and gets
nothing: no signature, no message.

## Threat model

| Threat | What happens |
| --- | --- |
| Someone captures game traffic | Sees at most a signature that is valid for one server and one nonce. No password, key or token travels in chat |
| Another local process on the loopback port | The port is random, chosen per login, and open for at most 3 minutes, on loopback only (and exclusive on Windows). Up to 4 connections are served at once and each has 5 seconds to send its request line, so a browser preconnect or a connection that only sits there cannot use up the single chance. It accepts the first request that has the exact path `/callback` and the right `state`; every other request is answered with an error and ignored. Even a process that wins the race only learns an authorization code that is useless without the PKCE verifier, which never leaves the client's memory. A process that sends a forged callback cannot match the 192-bit random state |
| Another local process reads the key | The private key is the file `kog_device.key` in the client's own save folder (the KoG Client folder, not the one of DDNet), on every system. It is written as a new `kog_device.key.tmp` (`O_EXCL`, `O_NOFOLLOW` where there is such a flag), synced and renamed over the old file, so a link planted at either name is never followed. On macOS and Linux its mode is 0600. On Windows no list is set: the file takes over that of the folder in your user profile, which normally allows you, the system and administrators only. **A process that runs as you can read the default file; this is the same protection as an SSH key without a passphrase.** With a passphrase it only gets the encrypted file, until you unlock the key in the session, and then the key is in the client's memory It is never in the config, logs, screenshots or crash reports. Memory buffers are wiped after use |
| Someone gets a copy of the key file (backup, shared disk, lost laptop) | With a passphrase (see below) the copy is useless without it, and every guess costs 64 MiB and a fraction of a second. Without one the thief can sign as this device until you remove it on kog.tw |
| The access token leaks | It is used for the user info, the device registration and the removal, held only in memory and wiped, together with the code, the verifier and the copies in the request. It is never written to disk. Requests are not logged (`HTTPLOG::NONE`), and the browser link is logged by host only, never with its query |
| A malicious server asks for a proof | The client answers only servers listed for the `kog` community that agreed to `client_login`, once per join, and only with a signature bound to that server's address. A proof for one server is worth nothing at another |
| A stolen device or key file | Remove the device on kog.tw. The server then refuses its signature. The key never held an OAuth token: the access token of the login is dropped after the device is registered and is never written down, so a stolen key cannot open the account, only prove this one device |
| Fake account server | TLS is verified; the discovery document is only accepted from the configured https base URL and its endpoints must be https and on an allowed origin (see above). There is no certificate pinning |
| Removal answered 404 | Only 200 and 204 mean removed. A 404 means the browser was signed in to a different account than the one that owns the device; the client says so and points to kog.tw instead of claiming success |

## Privacy

Nothing leaves the machine except requests to the account server (`kog.tw` by default, `cl_kog_account_url`): the discovery
document, the token exchange, user info, device registration and removal. The device name (a neutral "KoG Client (system)" by default, editable)
and the public key are sent to register the device. The default name has no host name in it. The login page itself is opened in your browser.

## Tests

`src/test/kog_login_wire_test.cpp`: the three message names, the challenge of the worked example of the server page, every malformed challenge (short parts, a time that is too short or too long), the proof bytes, the result (a refusal never carries a name, a result that is not one, a long name, control characters). `src/test/kog_login_test.cpp` has the spelling of the address.

`src/test/kog_login_test.cpp`: the RFC 7636 PKCE vector, the RFC 8032 Ed25519 vectors, parsing of the callback (wrong path,
wrong state, duplicate parameters, other methods), the listener (first matching request only, idle connections, refusal and timeout, close, a code of the maximum length), the allowed endpoint origins, the challenge bytes and signature (layout, the old ambiguity, a vector made outside this code base, binding to server, nonce, device and session), the neutral device name, the wiping of request bodies, a 307 that the token POST must not follow (against a loopback server and real libcurl), and the decision not to answer non-KoG servers or servers that did not agree to `client_login`.
`src/test/kog_keyfile_test.cpp`: the plain and the locked file round trip, mode 0600, a link at the temporary name, wrong and too long passphrases, a changed or truncated file (every byte region), limits on the cost in the header, that a locked or damaged file is never replaced, and the migration (moves once, leaves an existing file alone, refuses garbage).
The HTTP steps of the flow, the macOS and Windows migration source, the file access list on Windows, the settings box and the exchange with a running KoG server (challenge, proof, result on a real connection) need a running account server, a desktop session or a server and have no automated test.
