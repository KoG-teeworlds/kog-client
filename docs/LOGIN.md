# Log in with KoG

The client logs in to your KoG account through your browser and then holds a device key pair. Nothing that could be
replayed ever travels over the game's network connection. This page describes what is built, what is stubbed and why.

## Switches

| Setting | Default | Effect |
| --- | --- | --- |
| `cl_kog_login` | on in canary and dev builds, off in stable | Off: no browser flow and no automatic login. The `/login <code>` chat command keeps working |
| `cl_kog_auto_login` | off | Prove this device to KoG servers when joining. Has no effect until the join protocol exists (see below) |
| `cl_kog_account_url` | `https://kog.tw` | Base URL of the account server, for staging. https only |
| `cl_kog_device_name` | empty | Name of this device on your account, at most 40 characters. Empty means the host name |
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

## Join proof (module done, transport not)

`KogLogin::SignChallenge` signs `"KoG-login-v1" || server host:port as dialed || nonce || device id` (concatenated as
they are) with the device key, so a challenge cannot be reused on another server, with another nonce or by another device.
`MayAnswerChallenge` decides whether the client answers at all: only if the switch and `cl_kog_auto_login` are on, a key
exists, the server is a KoG server (its address is listed for the `kog` community in the server list, which the server
cannot claim for itself), the player did not already use `/login` in this join, and not twice per join. Vanilla and other
servers never get an answer.

The wire format is designed in a separate change. Until it lands the transport is a stub that sends nothing, which is why
`cl_kog_auto_login` is off by default.

## Threat model

| Threat | What happens |
| --- | --- |
| Someone captures game traffic | Sees at most a signature that is valid for one server and one nonce. No password, key or token travels in chat |
| Another local process on the loopback port | The port is random, chosen per login, and open for at most 3 minutes, on loopback only (and exclusive on Windows). It accepts the first request that has the exact path `/callback` and the right `state`; every other request is answered with an error and ignored. Even a process that wins the race only learns an authorization code that is useless without the PKCE verifier, which never leaves the client's memory. A process that sends a forged callback cannot match the 192-bit random state |
| Another local process reads the key | The private key is in the macOS Keychain or Windows Credential Manager. On other systems it is a file with mode 0600 in the save directory (libsecret is not used: it is a heavy dependency). A process running as the same user can still ask the keychain or read the file; this is the same protection as an SSH key. It is never in the config, logs, screenshots or crash reports. Memory buffers are wiped after use |
| The access token leaks | It is used once (user info, device registration), held only in memory and wiped. It is never written to disk. Requests are not logged (`HTTPLOG::NONE`) |
| A malicious server asks for a proof | The client answers only servers listed for the `kog` community, once per join, and only with a signature bound to that server's address |
| A stolen device | Remove the device on kog.tw. The server then refuses its signature |
| Fake account server | TLS is verified; the discovery document is only accepted from the configured https base URL and its endpoints must be https. There is no certificate pinning |

## Privacy

Nothing leaves the machine except requests to the account server (`kog.tw` by default, `cl_kog_account_url`): the discovery
document, the token exchange, user info, device registration and removal. The device name (host name by default, editable)
and the public key are sent to register the device. The login page itself is opened in your browser.

## Tests

`src/test/kog_login_test.cpp`: the RFC 7636 PKCE vector, the RFC 8032 Ed25519 vectors, parsing of the callback (wrong path,
wrong state, duplicate parameters, other methods), the listener (first matching request only, refusal and timeout, close),
the challenge signature (fixed vector, binding to server, nonce and device), and the decision not to answer non-KoG servers.
The HTTP steps, the keychain and the settings box need a running account server or a desktop session and have no automated test.
