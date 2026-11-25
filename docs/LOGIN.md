# Log in with KoG

The client logs in to your KoG account through your browser and then holds a device key pair. Nothing that could be
replayed ever travels over the game's network connection. This page describes what is built, what is stubbed and why.

## Switches

| Setting | Default | Effect |
| --- | --- | --- |
| `cl_kog_login` | on in canary and dev builds, off in stable | Off: no browser flow and no automatic login. The `/login <code>` chat command keeps working |
| `cl_kog_auto_login` | off | Prove this device to KoG servers when joining. Has no effect until the join protocol exists (see below) |
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
  | make Ed25519 key pair; private key into the keychain       |
  | POST /api/client/device {device_name, public_key} -------->|
  |<--------------------------------------- 201 {id, name}    |
  | discard the access token                                   |
```

Log out on this device wipes the private key at once. The account server only accepts the removal of the device with a
token, so the client runs the browser step again and sends `DELETE /api/client/device/{id}`. If that is cancelled or fails,
the key is gone anyway and the device stays listed on kog.tw until you remove it there.

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

## Join proof (module done, transport not)

`KogLogin::SignChallenge` signs, with the device key, the bytes of the design in `docs/KOG-CLIENT-LOGIN-PROOF.md` section 2.2
of the server repository:

```
"KoG-login-v1" 0x00 || u8 len(addr) || addr || nonce[32] || device_id[16] || session_id[16]
```

The parts are length prefixed or fixed width, so two different tuples never give the same bytes (the first draft
concatenated them and `1.2.3.4:830` plus a nonce starting with `3` collided with `1.2.3.4:8303`). A challenge cannot be
reused on another server, with another nonce, by another device or in another session.
`MayAnswerChallenge` decides whether the client answers at all: only if the switch and `cl_kog_auto_login` are on, a key
exists, the server is a KoG server (its address is listed for the `kog` community in the server list, which the server
cannot claim for itself), the player did not already use `/login` in this join, and not twice per join. Vanilla and other
servers never get an answer.

The client also needs the server to have agreed to the feature `client_login` in the KoG hello ([KOG-HELLO.md](KOG-HELLO.md)):
without it no challenge is answered, and the hello only lists `client_login` while `cl_kog_login`, `cl_kog_auto_login` and a device key are there.

The wire format of the challenge and the proof is designed in a separate change. Until it lands the transport is a stub that sends nothing, which is why
`cl_kog_auto_login` is off by default.

## Threat model

| Threat | What happens |
| --- | --- |
| Someone captures game traffic | Sees at most a signature that is valid for one server and one nonce. No password, key or token travels in chat |
| Another local process on the loopback port | The port is random, chosen per login, and open for at most 3 minutes, on loopback only (and exclusive on Windows). Up to 4 connections are served at once and each has 5 seconds to send its request line, so a browser preconnect or a connection that only sits there cannot use up the single chance. It accepts the first request that has the exact path `/callback` and the right `state`; every other request is answered with an error and ignored. Even a process that wins the race only learns an authorization code that is useless without the PKCE verifier, which never leaves the client's memory. A process that sends a forged callback cannot match the 192-bit random state |
| Another local process reads the key | The private key is in the macOS Keychain or Windows Credential Manager. On other systems it is a file with mode 0600 in the save directory, written as a new `kog_device.key.tmp` (`O_EXCL`, `O_NOFOLLOW`), synced and renamed over the old file, so a link planted at either name is never followed (libsecret is not used: it is a heavy dependency). The macOS item is `WhenUnlockedThisDeviceOnly`: it is not in backups and not synced. The Windows credential is `CRED_PERSIST_LOCAL_MACHINE`: it survives log-off and stays on this PC (`ENTERPRISE` would roam with a domain profile, `SESSION` would end the login at every log-off); it is readable by this Windows user only A process running as the same user can still ask the keychain or read the file; this is the same protection as an SSH key. It is never in the config, logs, screenshots or crash reports. Memory buffers are wiped after use |
| The access token leaks | It is used for the user info, the device registration and the removal, held only in memory and wiped, together with the code, the verifier and the copies in the request. It is never written to disk. Requests are not logged (`HTTPLOG::NONE`), and the browser link is logged by host only, never with its query |
| A malicious server asks for a proof | The client answers only servers listed for the `kog` community, once per join, and only with a signature bound to that server's address |
| A stolen device | Remove the device on kog.tw. The server then refuses its signature |
| Fake account server | TLS is verified; the discovery document is only accepted from the configured https base URL and its endpoints must be https and on an allowed origin (see above). There is no certificate pinning |
| Removal answered 404 | Only 200 and 204 mean removed. A 404 means the browser was signed in to a different account than the one that owns the device; the client says so and points to kog.tw instead of claiming success |

## Privacy

Nothing leaves the machine except requests to the account server (`kog.tw` by default, `cl_kog_account_url`): the discovery
document, the token exchange, user info, device registration and removal. The device name (a neutral "KoG Client (system)" by default, editable)
and the public key are sent to register the device. The default name has no host name in it. The login page itself is opened in your browser.

## Tests

`src/test/kog_login_test.cpp`: the RFC 7636 PKCE vector, the RFC 8032 Ed25519 vectors, parsing of the callback (wrong path,
wrong state, duplicate parameters, other methods), the listener (first matching request only, idle connections, refusal and timeout, close, a code of the maximum length), the allowed endpoint origins, the challenge bytes and signature (layout, the old ambiguity, a vector made outside this code base, binding to server, nonce, device and session), the neutral device name, the wiping of request bodies, a 307 that the token POST must not follow (against a loopback server and real libcurl), and the decision not to answer non-KoG servers or servers that did not agree to `client_login`.
The HTTP steps of the flow, the keychain, the key file and the settings box need a running account server or a desktop session and have no automated test.
