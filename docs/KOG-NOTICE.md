# Notices from a KoG server

A KoG server tells you things that are not chat: an achievement you unlocked, a quest you finished, that the server
restarts in five minutes. This client shows them as a toast at the top of the screen. The full design, the wire format
and the decisions are in `docs/KOG-NOTICE.md` of the server repository (kog-2.0-server) and the golden vectors in
`test_vectors/kog-notice.json` of the protocol repository.

## Two ways, one at a time

| Server | What arrives | What you see |
| --- | --- | --- |
| Agreed to `notices` in the KoG hello ([KOG-HELLO.md](KOG-HELLO.md)) | the message `kog-notice@kog.tw`, no chat line | a toast, and the same text in the chat log |
| Anything else (other servers, KoG servers without the capability, `kog_hello` off) | a chat line | for "Achievement unlocked: ..." and "Quest completed: ..." on a KoG server a toast; the line stays in the chat as sent |

A server never sends both, so nothing shows twice. A stock Teeworlds or DDNet client only ever gets the chat line.

## What the client does with a notice

- Reads the message only if the server agreed to `notices`; otherwise it is not read. A message that is not well formed
  (a field missing, an empty text) is dropped.
- The kind (info, achievement, quest completed, quest progress, maintenance, warning, tournament) chooses the headline and
  the colour. A kind this client does not know is shown as an info. The text is shown as it is, nothing in it is
  interpreted, nothing is clickable.
- A notice with the key of a toast that is still shown replaces it: a quest counter that moves, a countdown that changes.
- A toast lasts 5 seconds. A maintenance or tournament notice with seconds left stays until they are over (at most one
  hour) and counts down on the client. A quest progress notice with a target draws a bar.
- At most 4 new toasts in 10 seconds; the rest are only in the chat log. Every notice is written to the chat log.
