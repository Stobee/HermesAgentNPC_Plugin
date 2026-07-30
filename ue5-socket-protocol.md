# UE5 ↔ Hermes Socket Protocol (v2)

This document fully specifies the protocol between an Unreal Engine 5 client
(the NPC) and the Hermes agent server. It is **self-contained**: you can
implement either side from this document alone, without reading the other's code.

- **Port:** `8770` (TCP)
- **Transport:** **TLS 1.2+ over TCP**, carrying length-prefixed JSON frames
- **Text encoding:** UTF-8 everywhere
- **Protocol version:** 2

> **The server address is client configuration, not part of this protocol.**
> The UE5 client reads it from project settings; see the plugin README.
>
> The endpoint may be a **hostname or a literal IP**. The client resolves names
> through the platform resolver and prefers IPv4 among the returned addresses,
> so a server may be published under a DNS name. The configured name — not the
> resolved address — is what TLS SNI and certificate verification use (§0).

Every client-side limit this document cites is a **configurable default**, not a
protocol constant. §9 lists them with their setting names so a server
implementer can see the exact knobs and their shipped values.

## Scope: one connection, one NPC

**This protocol addresses exactly one conversational NPC.** No frame carries an
NPC identifier, and none will in v2 — the connection *is* the addressing.

That means, concretely:

- `chat` is the player talking **to that NPC**. `chat_response` is that NPC
  answering. There is no "which NPC said this" field because there is only one.
- `action_request` commands **that NPC**. The server never chooses a target; it
  says `move_to` and the client's single registered NPC moves.
- One `player_id` ↔ one `chat_id` ↔ one NPC ↔ one connection. The server keeps
  one conversation per identity, not one per character.

The UE5 side enforces this: the project designates a single Hermes NPC actor,
and registering another one **replaces** it rather than adding a second target
(see the plugin README). A server may therefore assume its actions always reach
the same character for the life of a session.

> **Multiple conversational NPCs are out of scope by design, not by oversight.**
> The plugin's remit is one agent-driven character. A project that wants a town
> full of them needs one connection per NPC — which this protocol supports only
> in the sense that each connection would need its own identity and its own
> client instance. Do not work around it by inventing an `npc_id` field: the
> client ignores unknown fields (§2), so the action would silently go to the
> wrong character instead of failing loudly.

## Non-negotiable properties

Most of this document is detail you can look up. These four are the ones a
server gets wrong quietly, so they are stated once, up front:

1. **The server issues the identity.** Clients never invent a `player_id`, and
   `identified` always carries both `player_id` and `session_token` — including
   on reconnect (§3, §4.2). A server that omits them leaves the connection
   unauthenticated, and the client closes it rather than proceeding.
2. **`action_result` means accepted, not finished.** Completion arrives later as
   `action_event` (§4.5, §4.10). A client that reports completion at accept time
   is lying to the agent about where the NPC is.
3. **One turn at a time.** Chat turns are serialized and their frames are never
   interleaved (§3.6).
4. **There is no compatibility mode and no version negotiation.** `identify`
   carries `protocol_version: 2`; anything else is refused (§5). A path that
   works without authentication defeats the point of having credentials.

---

## 0. TLS requirements

- The server **must** present a certificate. TLS 1.2 is the minimum version.
- For LAN deployments a self-signed certificate is expected. In that case the
  server operator **must** publish the SPKI SHA-256 pin so clients can pin it:

  ```bash
  openssl x509 -in server.crt -pubkey -noout \
    | openssl pkey -pubin -outform der \
    | openssl dgst -sha256 -binary \
    | openssl enc -base64
  ```

  On Windows, run that under Git Bash, or use this PowerShell equivalent.
  **Do not translate it into a PowerShell pipeline** — PowerShell pipes carry
  text, not bytes, and will corrupt the DER on its way to the hash. Go through
  files instead:

  ```powershell
  openssl x509 -in server.crt -pubkey -noout -out pub.pem
  openssl pkey -pubin -in pub.pem -outform der -out pub.der
  $sha = [Security.Cryptography.SHA256]::Create()
  [Convert]::ToBase64String($sha.ComputeHash([IO.File]::ReadAllBytes("pub.der")))
  ```

- Pins are on the **public key (SPKI)**, not on the certificate. Renewing the
  certificate with the same key pair keeps existing pins valid; pinning the
  certificate fingerprint would force a client redeploy on every renewal.
- Clients verify the server in one of three ways: pinned SPKI, a configured
  private CA, or the system root store. Whichever applies, **verification
  failure closes the connection.**
- Plaintext TCP is **development only** and must not be used in deployed
  configurations. **Clients do not fall back to plaintext when TLS fails** —
  a failed handshake is retried as TLS, with backoff, indefinitely.
- Terminating TLS at a reverse proxy (nginx, Caddy, IIS/ARR) and speaking
  plaintext on loopback to the agent process is a supported deployment. The
  certificate and pin then belong to the proxy.
- **The server's operating system is not part of this protocol.** The contract is
  length-prefixed frames over TCP; Linux, Windows, or anything else that can hold
  a socket open is equally valid. Nothing below assumes a platform.

---

## 1. Framing

TCP is a byte stream with no message boundaries, so every logical message is
sent as a **length-prefixed frame**:

```
+---------------------------+-------------------------------+
| 4-byte length (uint32 BE) |   JSON body (UTF-8, N bytes)  |
+---------------------------+-------------------------------+
```

- The 4-byte prefix is the **byte length of the JSON body**, unsigned 32-bit,
  **big-endian (network byte order)**.
- The body is a single UTF-8 JSON object.
- Length counts the JSON bytes only — it does **not** include the 4 prefix bytes.
- **Max body size: 1,048,576 bytes (1 MiB).** Frames larger than this are
  rejected and the connection is closed by the server. Keep messages small.

### Reading a frame (pseudocode)

```
read exactly 4 bytes            -> len = uint32_be(those bytes)
if len == 0 or len > 1048576    -> protocol error, close
read exactly len bytes          -> body
obj = json_parse(utf8(body))
```

### Writing a frame (pseudocode)

```
body = utf8(json_serialize(obj))
send( uint32_be(length(body)) )   // 4 bytes
send( body )                       // length(body) bytes
```

> **UE5 note:** with `FSocket`, accumulate received bytes into a buffer and only
> parse once you have the full 4-byte header plus `len` body bytes. A single
> `Recv` may return a partial frame or several frames — always drive parsing off
> the length prefix, never off packet boundaries.

---

## 2. Message envelope

Every frame body is a JSON object with a **`type`** string field. Other fields
depend on `type`. Unknown fields must be ignored (forward compatibility).
Unknown `type` values are ignored by both sides (the server logs and replies
with an `error` frame; see §5).

Fields marked `?` are optional.

---

## 3. Handshake & lifecycle

### 3.1 First connection — the server issues the identity

The client has no stored credentials, so it omits them. This is the signal to
mint a new identity.

```
UE5 client                              Hermes server
    |  TLS connect :8770                     |
    |--------------------------------------->|
    |  identify {protocol_version:2,         |
    |            player_name?}               |
    |--------------------------------------->|
    |        identified {ok, player_id,      |
    |                    session_token,      |
    |                    chat_id}            |
    |<---------------------------------------|
```

The client persists `player_id` and `session_token` locally.

### 3.2 Reconnection — the client proves the identity

```
    |  identify {protocol_version:2,         |
    |            player_id, session_token,   |
    |            player_name?}               |
    |--------------------------------------->|
    |        identified {ok, chat_id}        |
    |<---------------------------------------|
```

### 3.3 Steady state

```
    |  chat {id, text}                       |
    |--------------------------------------->|
    |            chat_delta {id, seq, text}  |  (zero or more, batched)
    |<---------------------------------------|
    |          chat_response {id, text, ...} |  (terminal, authoritative)
    |<---------------------------------------|
    |                                        |
    |   (server may push action_request)     |
    |          action_request {id, cmd, ...} |
    |<---------------------------------------|
    |  action_result {id, ok, result?}       |
    |--------------------------------------->|
    |                                        |
    |  ping {id}  /  pong {id}   either way  |
    |<-------------------------------------->|
```

**Rules**

- The client **must** send `identify` first. Any `chat` sent before `identify`
  is rejected with an `error` frame.
- **The server issues the identity. Clients must not invent a `player_id`.**
  An `identify` carrying no credentials is a request to issue new ones.
- On reconnect the client sends **both** `player_id` and `session_token`. The
  server validates the pair. A mismatch is `not_authorized` and closes the
  connection. **Knowing a `player_id` alone must never grant access to that
  session** — this is the whole point of v2.
- If a client sends `player_id` without `session_token` (or vice versa), treat
  it as malformed and reply `not_authorized`. Clients are specified to send
  neither or both.
- `session_token` must be high-entropy and **must not be derivable from**
  `player_id`. Leaking one must not reveal the other.
- Tokens are not rotated on reconnect. Rotation shortens the exposure window of
  a leaked token but risks locking a player out of their own session on a
  storage failure; with TLS closing the transport leak, the remaining exposure
  is local extraction by the device owner, which rotation does not help.
- The server keeps `player_id → session_token` mapping durable across restarts.
  Losing it locks every player out.
- The client sends `protocol_version: 2`. If `identified` arrives **without** a
  `session_token`, the client logs an error and reconnects rather than
  proceeding — an unauthenticated session is not a degraded session, it is the
  wrong session.

### 3.4 Two connections, one identity — the newcomer wins

A player reinstalls, copies a save, or launches the game twice. The server will
see a second `identify` carrying credentials that are **already bound to a live
connection**. This is not an error and must not be refused:

**The newest valid connection takes the session. The older one is evicted.**

```
    |  (conn A is live, identified)          |
    |                                        |<--- conn B: identify {player_id, session_token}
    |  error {code:"session_taken_over"}     |
    |<---------------------------------------|
    |  <connection A closed by server>       |
    |                                        |---> identified {...}  (to B)
```

Requirements:

- The eviction notice is a normal `error` frame with code `session_taken_over`
  (§5), **sent to the old connection before closing it**. Closing the socket
  silently is not acceptable — the displaced client cannot distinguish that from
  a network fault, and will reconnect and steal the session straight back.
- The server sends the notice, then closes. It does not wait for a reply.
- The credentials themselves are **not** invalidated. B is a legitimate holder
  of the identity; A simply no longer owns the connection.
- **An evicted client must not auto-reconnect.** `session_taken_over` is the one
  disconnect reason that suspends the reconnect loop (§9) — the session is
  someone else's now, and retrying produces an eviction war in which two game
  instances kick each other forever. Reconnecting requires a deliberate act
  (a new `identify` triggered by the game, e.g. the player re-entering the
  conversation), not the backoff timer.
- Conversation state belongs to the **identity**, not the connection. B resumes
  the same `chat_id` and history that A had.

### 3.5 What survives a disconnect

**Nothing in flight is retried automatically — by either side.**

| At the moment the connection drops | Outcome |
|---|---|
| A `chat` was sent, no `chat_response` yet | The turn is **abandoned**. The client does not resend it; the server discards the reply rather than delivering it on the next connection. |
| An `action_request` was accepted, no `action_event` yet | Lost (§4.10). The server must treat the outcome as **unknown**. |
| Utterances queued **before** `identified` ever arrived | These were never sent. They **are** flushed once `identified` arrives, subject to `MaxPendingChats` (§9). |

The last row is the only thing that looks like a retry and is not: those frames
never reached the wire. Once a `chat` has been written to the socket it is never
written again, so a server that finishes generating a reply for a connection
that has since dropped should **drop the reply**, not queue it. Delivering a
stale answer to the next connection would attach it to whatever the player says
next, which is worse than losing it.

Durable conversation history is the server's business (it keys off `chat_id`),
and the agent will see the abandoned utterance in that history. What must not
happen is the **frame** being replayed.

### 3.6 One turn at a time

**A session processes exactly one chat turn at a time, in arrival order.**

The client does not wait for a reply before sending again — the dialogue UI has
no send-gating, so a player pressing enter three times produces `c-0001`,
`c-0002`, `c-0003` back to back. This is **normal traffic, not abuse.**

The server queues them and answers them one at a time:

```
--> chat {id:"c-0001"}      --> chat {id:"c-0002"}     (both already on the wire)

<-- chat_delta {id:"c-0001", ...}   x N
<-- chat_response {id:"c-0001"}     <- turn 1 closed
<-- chat_delta {id:"c-0002", ...}   x N
<-- chat_response {id:"c-0002"}     <- turn 2 closed
```

Requirements:

- **Never interleave frames from two turns.** Every `chat_delta` between turn
  N's start and its `chat_response` belongs to turn N. A delta for a different
  `id` must not appear until the current turn has been closed by its
  `chat_response`.
- **Every accepted `chat` gets exactly one terminal frame** — a `chat_response`,
  or an `error` carrying its `id` (§5). A turn that is silently dropped leaves
  the client waiting out `ChatResponseTimeoutSeconds`.
- **Bound the queue.** A player can outrun any inference speed. Cap the queued
  turns per session and answer the overflow immediately with
  `error { code: "rate_limited", id: "<the chat id>" }` rather than growing.
- Order is arrival order. The queue is FIFO; do not reorder by length or cost.

**Why serial and not concurrent.** The client renders one conversation view with
one pending state. Two turns streaming at once produce two texts with nowhere to
put the second, and the accumulated display would be an interleaving of both
answers. Serial execution is what makes a single accumulation buffer correct —
see §4.9.

Actions are **not** part of this ordering. `action_request` may be pushed at any
time, including in the middle of a turn (§4.5), and `action_event` may arrive
long after the turn that caused it (§4.10). Only chat turns are serialized.

---

## 4. Message types

### 4.1 `identify` — Client → Server

Identifies the player and (re)binds the connection.

New identity (no stored credentials):

```json
{ "type": "identify", "protocol_version": 2, "player_name": "Aria" }
```

Reconnect (credentials stored from a previous `identified`):

```json
{
  "type": "identify",
  "protocol_version": 2,
  "player_id": "550e8400-e29b-41d4-a716-446655440000",
  "session_token": "9f2c...",
  "player_name": "Aria"
}
```

| Field | Type | Req | Notes |
|-------|------|-----|-------|
| `protocol_version` | int | yes | Always `2`. Reject any other value with `error { code: "unsupported_version" }` and close (§5). |
| `player_id` | string | no | Omit to request new credentials. |
| `session_token` | string | no | **Required whenever `player_id` is present.** |
| `player_name` | string | no | Display name; used in chat info. |

### 4.2 `identified` — Server → Client

Acknowledges `identify`. On first issuance it carries the credentials the
client must persist.

Issuance:

```json
{
  "type": "identified",
  "ok": true,
  "player_id": "550e8400-e29b-41d4-a716-446655440000",
  "session_token": "9f2c...",
  "chat_id": "550e8400-e29b-41d4-a716-446655440000"
}
```

Reconnect acknowledgement — **same shape**, echoing the credentials that were
just validated:

```json
{
  "type": "identified",
  "ok": true,
  "player_id": "550e8400-e29b-41d4-a716-446655440000",
  "session_token": "9f2c...",
  "chat_id": "550e8400-e29b-41d4-a716-446655440000"
}
```

| Field | Type | Req | Notes |
|-------|------|-----|-------|
| `ok` | bool | yes | `true` on success. |
| `player_id` | string | **yes, always** | Server-generated on issuance, echoed on reconnect. |
| `session_token` | string | **yes, always** | Server-generated credential, echoed on reconnect. |
| `chat_id` | string | yes | Conversation id for this player. **Server-side bookkeeping — the client never sends it back.** Do not design a flow that expects it to return in a later frame; nothing in §4 carries it. It exists so your logs and storage can key the conversation, and so a server that migrates history has a handle to it. |

> **`identified` must always carry both credentials, including on reconnect.**
> This is deliberate and load-bearing: it makes "no `session_token` in
> `identified`" mean exactly one thing — **this server did not authenticate the
> session** — with no second reading the client has to guess between. If the
> field were optional on reconnect, a missing token would be ambiguous and the
> client would have to either trust it or drop a healthy session. Echoing values
> the client already holds costs nothing: it overwrites them with identical
> values, and the channel is encrypted.
>
> This also gives the server a clean path to **re-issue** credentials (e.g.
> after a key rotation or a storage migration): return different values and the
> client persists them.

### 4.3 `chat` — Client → Server

A player utterance to the NPC.

```json
{ "type": "chat", "id": "c-0001", "text": "저기 언덕 위로 이동해줘" }
```

| Field | Type | Req | Notes |
|-------|------|-----|-------|
| `id` | string | yes | Client-generated message id (for your own correlation/logging). |
| `text` | string | yes | The player's message. |

### 4.4 `chat_response` — Server → Client

The agent's reply, and the **terminal frame of a chat turn**. May arrive after
one or more `action_request`s if the agent decided to act mid-turn, and after
any number of `chat_delta` frames (§4.9).

`text` is the **authoritative full reply**, not a remainder — send the complete
text even when you already streamed all of it as deltas. The client replaces
its accumulated display with this value, which is what makes dropped or
duplicated deltas self-correcting.

```json
{
  "type": "chat_response",
  "id": "c-0001",
  "text": "알겠어요, 언덕 위로 이동할게요.",
  "actions": [
    { "command": "move_to", "ok": true }
  ]
}
```

| Field | Type | Notes |
|-------|------|-------|
| `id` | string? | Echoes the triggering `chat.id` when available. |
| `text` | string | NPC dialogue to display. Plain text. |
| `actions` | array? | Summary of actions executed this turn (informational). |

### 4.5 `action_request` — Server → Client

The agent wants the game to perform an action. The client **must** execute it
and reply with a matching `action_result` (same `id`).

```json
{
  "type": "action_request",
  "id": "act-7f3a91",
  "command": "move_to",
  "params": { "location": { "x": 1200.0, "y": 340.0, "z": 90.0 } }
}
```

| Field | Type | Notes |
|-------|------|-------|
| `id` | string | Request id. Echo it back verbatim in `action_result`. |
| `command` | string | One of the whitelisted commands (§6). |
| `params` | object | Command-specific parameters (§6). |

> The server waits up to **15 seconds** (configurable) for the matching
> `action_result`. If none arrives, the agent is told the action timed out.
> Reply promptly, even for failures.

**`action_result` acknowledges acceptance, not completion.** Some actions
finish instantly (`follow_player`, `inventory_manage`); others take arbitrarily
long (`move_to` across a large map, blocked by pathing). Waiting for physical
completion before replying would blow the 15 s budget on exactly the actions
that matter most, and no server-side timeout tuning can fix that — the server
cannot know your map size or movement speed.

So the split is:

| Frame | Meaning | Deadline |
|---|---|---|
| `action_result` (§4.6) | "I accepted this and started it" | **within 15 s, always** |
| `action_event` (§4.10) | "It finished / it failed" | whenever it actually happens |

Instant actions send `action_result` and nothing else. Long-running actions
send `action_result` immediately and an `action_event` later.

### 4.6 `action_result` — Client → Server

Acknowledges an `action_request` and reports the outcome **as far as it is
known at accept time**. See the note in §4.5 — this is not a completion signal
for long-running actions.

```json
{ "type": "action_result", "id": "act-7f3a91", "ok": true, "result": { "arrived": true } }
```

Failure example:

```json
{ "type": "action_result", "id": "act-7f3a91", "ok": false, "error": "path blocked" }
```

| Field | Type | Req | Notes |
|-------|------|-----|-------|
| `id` | string | yes | Must equal the `action_request.id`. |
| `ok` | bool | yes | `true` if executed successfully. |
| `result` | object | no | Structured outcome the LLM can reason about. |
| `error` | string | no | Human-readable failure reason when `ok=false`. |

### 4.7 `ping` / `pong` — keepalive

Either side may send `ping`; the peer replies `pong` with the same `id`.

```json
{ "type": "ping", "id": "k-0012" }
{ "type": "pong", "id": "k-0012" }
```

**Both sides should treat receive-silence as death.** TCP does not report a
connection that dies quietly — a NAT timeout, a Wi-Fi handover, a yanked cable.
Without an application-level check, the failure surfaces only at the next send,
which on the client side is the moment the player tries to talk to the NPC.

The UE5 client behaves as follows (defaults; all configurable):

| Rule | Setting | Default |
|------|---------|---------|
| Send `ping` after this much **send**-silence | `KeepAlivePingIntervalSeconds` | 20 s |
| Declare the connection dead after this much **receive**-silence, then reconnect | `PeerTimeoutSeconds` | 60 s |

**Any received frame counts as liveness, not just `pong`.** An active
conversation keeps the connection alive with no extra pings. Servers are
recommended to apply the same rule so half-open connections are reaped from
both ends; pick a server-side timeout comfortably above the client's ping
interval so a healthy client is never reaped.

### 4.8 `error` — Server → Client

Sent on protocol violations (e.g. `chat` before `identify`, unknown `type`,
malformed action). The connection may or may not be closed depending on `code`
(framing errors close the connection).

```json
{ "type": "error", "code": "not_identified", "message": "send identify first" }
```

| Field | Type | Notes |
|-------|------|-------|
| `code` | string | Machine-readable error code — the full list and each code's connection/reaction contract is §5. |
| `message` | string | Human-readable detail. For logs, not for NPC dialogue. |
| `id` | string? | The `chat.id` or `action_request.id` this error belongs to, when it belongs to one. Including it lets the client fail that turn now instead of on timeout (§5). |

### 4.9 `chat_delta` — Server → Client

An incremental piece of an in-progress reply. Optional to send, but strongly
recommended: local inference of a few hundred tokens is several seconds of
silence otherwise, and the NPC appears frozen.

```json
{ "type": "chat_delta", "id": "c-0001", "seq": 0, "text": "알겠어요, " }
```

| Field | Type | Req | Notes |
|-------|------|-----|-------|
| `id` | string | yes | Echoes the triggering `chat.id`. |
| `seq` | int | no | Ordering hint. The UE5 client ignores it. |
| `text` | string | yes | Text fragment to append. |

**`chat_response.text` is authoritative; deltas are a display hint.** The
client accumulates deltas for immediate feedback and then *replaces* the
display with the final text. Consequences worth relying on:

- A client that ignores deltas entirely still works.
- Dropping or double-processing a delta still ends up correct.
- You do not need to guarantee delta ordering or delivery.

**A single accumulation buffer is sufficient, because turns are serial (§3.6).**
All deltas between a turn's start and its `chat_response` share one `id`. A
delta whose `id` differs from the turn in progress is a protocol violation — the
client discards its buffer and starts a new one rather than merging the two,
so an interleaving server produces truncated text, not a crash.

> **Servers must batch deltas.** One frame per token is tens to hundreds of
> frames per second, which pressures the client's per-tick frame budget
> (`MaxInboundFramesPerTick`, default 64) and inbound queue cap
> (`MaxInboundQueueSize`, default 1024) during **normal** operation. Flush
> every **~50 ms or every few tokens**. See §9.
>
> Clients will not raise those caps to accommodate unbatched streams. The caps
> exist to stop abusive peers; a well-behaved server should never approach
> them. A client that hits the inbound cap closes the connection.

Deltas double as **progress signals**. The client fails a `chat` that has seen
neither a delta nor a final response within its chat timeout
(`ChatResponseTimeoutSeconds`, default 60 s).
A long generation that keeps streaming is therefore never timed out, while a
generation that stalls is caught — which is only possible because deltas exist.

### 4.10 `action_event` — Client → Server

Reports that a previously accepted action has **finished**. Sent only for
actions that do not complete at accept time; see the note in §4.5.

Completion:

```json
{
  "type": "action_event",
  "id": "act-7f3a91",
  "event": "completed",
  "result": { "arrived": true }
}
```

Failure after acceptance:

```json
{
  "type": "action_event",
  "id": "act-7f3a91",
  "event": "failed",
  "error": "path blocked"
}
```

| Field | Type | Req | Notes |
|-------|------|-----|-------|
| `id` | string | yes | The original `action_request.id`. |
| `event` | string | yes | `"completed"` or `"failed"`. |
| `result` | object | no | Structured outcome, same conventions as `action_result.result`. |
| `error` | string | no | Failure reason when `event="failed"`. |

**Rules**

- An `action_event` always follows an `action_result` with the same `id`.
  It never arrives first, and never without one.
- **At most one `action_event` per `id`.** Servers should ignore duplicates.
- **An `id` the server does not recognize is ignored, not an error.** This is
  normal after a server restart: the client kept running, finished a long
  `move_to`, and reported it to a process that no longer remembers requesting
  it. Log it and drop it — do not reply with `error`, and do not synthesize a
  tool observation for an action you cannot attribute.
- The `id` may arrive long after the turn that requested it — potentially after
  several `chat` exchanges. Servers must not assume it belongs to the current
  turn.
- If the connection drops before the event is sent, it is **lost**. Neither
  side retries. The server should treat an accepted action with no event as
  "outcome unknown" rather than assuming success.
- Clients that implement no long-running actions never send this frame, and a
  server that ignores it still works — the LLM simply learns less.

**Why this matters for the agent.** With this split the NPC can say
"알겠어요, 언덕 위로 갈게요" on `action_result` and then, seconds later,
"도착했어요" when `action_event` arrives. Without it the client must either
lie at accept time or blow the timeout.

> **Server implementers:** feed `action_event` back into the conversation as a
> tool observation, not as a user turn. It is the game telling the agent what
> happened, unprompted.

---

## 5. Error codes

Every `error` frame carries a `code` from this table. The list is closed: a
server must not invent codes, because the client branches on them. A condition
with no code here is reported as `internal_error` with detail in `message`.

| `code` | Meaning | Connection | Client reaction |
|--------|---------|------------|-----------------|
| `not_identified` | A `chat`/action was sent before `identify`. | kept | Re-sends `identify`. |
| `unknown_type` | Unrecognized `type` value. | kept | Logs only. |
| `bad_frame` | Malformed JSON or length over 1 MiB. | **closed** | Reconnects with backoff. |
| `unknown_command` | `action_result` / action referenced a non-whitelisted command. | kept | Logs only. |
| `not_authorized` | `(player_id, session_token)` did not validate, or one was sent without the other. **Required in v2.** | **closed** | Discards stored credentials, reconnects, and re-identifies **with none** — i.e. asks for a new identity. |
| `unsupported_version` | `identify.protocol_version` is not `2`. | **closed** | Logs loudly and stops reconnecting. A version mismatch does not heal by retrying. |
| `session_taken_over` | Another connection claimed this identity (§3.4). | **closed** | **Stops the reconnect loop.** Credentials stay stored. |
| `rate_limited` | The server's own per-session limit was exceeded. Distinct from the client-side action limiter (§6), which never produces an `error` frame. | kept | Logs; does not resend. |
| `server_busy` | The server is up but cannot take work (inference queue full, model loading). | kept | Logs; the pending turn fails. Retrying is the player's choice, not the client's. |
| `internal_error` | Server-side failure while handling an otherwise valid frame. | kept | Logs; the pending turn fails. |

**`kept` vs `closed` is a contract, not a hint.** The client keeps the socket
open on `kept` codes and expects the session to remain usable. If the server
closes anyway, the client sees an unexplained drop and reconnects — which is
survivable but hides the real reason from the log, so pick the right code.

**Which codes end a turn.** `rate_limited`, `server_busy`, and `internal_error`
can arrive while a `chat` is outstanding. When they carry the `id` of that chat,
the client fails that turn immediately instead of waiting out
`ChatResponseTimeoutSeconds` (§9):

```json
{ "type": "error", "code": "server_busy", "id": "c-0007", "message": "inference queue full" }
```

| Field | Type | Req | Notes |
|-------|------|-----|-------|
| `code` | string | yes | From the table above. |
| `message` | string | yes | Human-readable detail. Logged, never shown to the player as NPC dialogue. |
| `id` | string | no | The `chat.id` or `action_request.id` this error belongs to, when it belongs to one. |

---

## 6. Action command catalog (v2)

Only these commands are whitelisted. Anything else is rejected server-side and
never dispatched. New commands are added in later versions — treat an
unrecognized `command` in an `action_request` defensively (reply `ok=false`,
`error="unsupported command"`).

> **The client enforces hard bounds before executing anything.** Values outside
> these ranges are rejected with `ok=false` and a descriptive `error`, and the
> action never reaches game code. These are defaults; each is configurable per
> project, so do not treat the numbers as protocol constants — treat the
> *existence* of bounds as guaranteed.
>
> | Constraint | Setting | Default | `error` on violation |
> |---|---|---|---|
> | `move_to` coordinates finite and `abs(v) <=` the bound | `MaxWorldCoordinate` | 1e7 cm (100 km) | `coordinate out of range` |
> | `item_transfer.quantity` integer, `1 <= q <=` the bound | `MaxItemQuantity` | 999999 | `quantity out of range` |
> | `item_transfer.item_id` non-empty, length `<=` the bound | `MaxItemIdLength` | 64 | `invalid item_id` |
> | Action requests processed per second | `MaxActionsPerSecond` | 20 | `rate limited` |
> | Response deadline before the client self-reports failure | `ActionTimeoutSeconds` | 15 s | `timeout` |
>
> Excess requests are **rejected, not queued** — a burst above
> `MaxActionsPerSecond` gets an immediate `ok=false, error="rate limited"` per
> request rather than delayed execution, so the agent learns right away instead
> of waiting out a silent backlog.
>
> **Constrain generation rather than relying on these rejections.** Use a JSON
> schema or GBNF grammar on the LLM so `command` can only be one of the
> whitelisted values and numeric parameters carry `minimum`/`maximum`. That
> makes out-of-catalog commands and out-of-range values *impossible to
> generate* instead of merely rejected afterwards — the only effective
> mitigation when a player's utterance is trying to steer tool use.
>
> The client keeps its own bounds regardless. It does not assume the server is
> correct or honest.

### `move_to` — long-running

Move the NPC to a world location.
```json
{ "command": "move_to", "params": { "location": { "x": 0.0, "y": 0.0, "z": 0.0 } } }
```
- `location.x/y/z`: floats, UE world coordinates (cm).

**Two-phase response.** Pathing succeeds or fails immediately; arrival takes as
long as it takes.

```jsonc
// immediately — pathfinding accepted the destination
{ "type": "action_result", "id": "act-1", "ok": true,
  "result": { "started": true, "eta_seconds": 8.4 } }

// later — the NPC actually got there
{ "type": "action_event", "id": "act-1", "event": "completed",
  "result": { "arrived": true } }

// or later — it gave up
{ "type": "action_event", "id": "act-1", "event": "failed",
  "error": "path blocked" }
```

If pathing is rejected outright (unreachable destination, no navmesh), reply
`action_result` with `ok=false, error="path blocked"` and send **no** event.

- `eta_seconds`: float, optional. Best-effort estimate; omit if unknown.
- `started`: bool. Always `true` when `ok=true` — present so the agent can tell
  "accepted" apart from a payload that claims arrival.

> **Never put `arrived: true` in an `action_result`.** It claims completion at
> accept time, which tells the agent the NPC has arrived when it has not yet
> moved. Arrival is reported by `action_event`, and only when it actually
> happens.

### `follow_player` — instant

Start or stop following the player.
```json
{ "command": "follow_player", "params": { "enabled": true } }
```
- `enabled`: bool — `true` starts following, `false` stops.

Completes at accept time. `action_result` only; no `action_event`. Following is
an ongoing state, not a task with an end.

### `inventory_manage`
Query or organize the NPC inventory.
```json
{ "command": "inventory_manage", "params": { "operation": "list", "target": null } }
```
- `operation`: string — e.g. `"list"`, `"sort"`, `"drop"` (game-defined).
- `target`: optional item id or slot the operation applies to.

### `item_transfer` — instant

Give or receive an item between player and NPC.
```json
{ "command": "item_transfer", "params": { "direction": "give", "item_id": "health_potion", "quantity": 1 } }
```
- `direction`: `"give"` (NPC → player) or `"receive"` (player → NPC).
- `item_id`: string, non-empty, length-bounded (see table above).
- `quantity`: integer ≥ 1, upper-bounded (see table above).

Completes at accept time. `action_result` only.

### Completion class summary

| Command | Class | Frames sent |
|---|---|---|
| `move_to` | long-running | `action_result` then `action_event` |
| `follow_player` | instant | `action_result` |
| `inventory_manage` | instant | `action_result` |
| `item_transfer` | instant | `action_result` |

New commands must declare their class. When in doubt, make it long-running —
an instant action that occasionally blocks will silently blow the 15 s budget.

**Result payload conventions** (recommended, game-defined): return the
observable outcome so the NPC can talk about it.

| Command | `action_result.result` | `action_event.result` |
|---|---|---|
| `move_to` | `{ "started": true, "eta_seconds": 8.4 }` | `{ "arrived": true }` |
| `follow_player` | `{ "following": true }` | — |
| `inventory_manage` (`list`) | `{ "items": [ ... ] }` | — |
| `item_transfer` | `{ "transferred": 1 }` | — |

---

## 7. Minimal client checklist

1. Open a **TLS** connection to the configured endpoint. The address comes from
   client configuration, not from this document. Verify the server certificate
   (pinned SPKI, private CA, or system roots). **Never fall back to plaintext.**
2. Send `identify` with `protocol_version: 2`, including `player_id` and
   `session_token` if you have them stored, omitting **both** if you do not.
3. Wait for `identified`. Persist the `player_id` and `session_token` it
   returns. **If it has no `session_token`, the server did not authenticate this
   session** — log loudly and disconnect. Do not proceed on the assumption that
   the server "probably meant to".
4. Send `chat` frames for player utterances — you may send again without waiting
   for the previous reply; the server answers in order (§3.6). Render
   `chat_delta.text` incrementally, then replace with `chat_response.text` when
   it arrives. Reset the accumulation buffer when a delta's `id` changes.
5. Handle incoming `action_request`: validate parameters against your bounds,
   start the action, then reply `action_result` with the same `id` **within
   ~15 s**. Reply even on failure. For long-running actions this means
   acknowledging acceptance, **not** waiting for completion.
6. For long-running actions, send `action_event` when they actually finish or
   fail. Never send one without a preceding `action_result`, and never more
   than one per `id`.
7. Reply to `ping` with `pong`. Send your own `ping` when idle, and treat
   prolonged receive-silence as a dead connection.
8. On disconnect, reconnect and re-`identify` with the **stored credentials**
   to resume the conversation — **except** after `session_taken_over` or
   `unsupported_version`, which stop the reconnect loop (§5). Never resend a
   `chat` that was already on the wire, and expect no `action_event` for
   actions accepted before the drop (§3.5).
9. Always frame with the 4-byte big-endian length prefix; always parse off the
   length, never off packet boundaries.

## 7b. Minimal server checklist

0. Model one conversation per identity, addressed to a single NPC. There is no
   NPC id in any frame and you must not add one (see "Scope" above).
1. Terminate TLS (directly or behind a reverse proxy). Publish your SPKI pin if
   the certificate is self-signed.
2. Accept `identify`. If it carries no credentials, mint a `player_id` and a
   high-entropy `session_token` that is **not derivable** from the `player_id`,
   and store the mapping durably.
3. If it carries credentials, validate the pair. On mismatch — or if only one of
   the two is present — send `error { code: "not_authorized" }` and close.
3b. If the pair is valid but that identity already has a live connection, evict
   the old one: send it `error { code: "session_taken_over" }`, close it, then
   admit the newcomer (§3.4). Never refuse the newcomer.
4. Always answer with `identified` carrying **both** `player_id` and
   `session_token` (§4.2), plus `chat_id`.
5. Reject `chat` before `identify` with `not_identified`.
6. Stream replies as `chat_delta`, **batched to ~50 ms or a few tokens**, then
   close the turn with an authoritative `chat_response`.
6a. Process one turn at a time per session, in arrival order, and never
   interleave two turns' frames (§3.6). Queue what arrives mid-turn, bound that
   queue, and answer the overflow with `rate_limited` carrying the `chat.id`.
   Every accepted `chat` must end in a `chat_response` or an `error` with its `id`.
6b. Treat `action_result` as acceptance, not completion. Feed a later
   `action_event` (§4.10) back to the agent as a tool observation. An accepted
   action that never produces an event has an **unknown** outcome — do not
   assume success.
7. Constrain LLM tool output with a JSON schema or grammar (§6) so only
   whitelisted commands and in-range parameters can be produced.
8. Rate-limit per authenticated session and bound your inference queue depth.
   The client's own limits protect the client, not you. Report refusals as
   `rate_limited` / `server_busy` with the outstanding `chat.id` so the client
   can fail that turn immediately (§5).
9. Answer `ping` with `pong`, and reap connections that go silent. Do not rely
   on the client noticing first (§9).
10. When a connection drops mid-turn, **discard** the in-flight reply instead of
   delivering it to the next connection (§3.5). Keep it in the conversation
   history if you like; do not put it back on the wire.

---

## 8. Quick manual test

A Python reference client lives at `scripts/ue5_test_client.py` on the server.
Run it on the same LAN to exercise the exact framing (connect → identify → chat
→ print response; auto-replies to `action_request`). Note: `nc`/`telnet` cannot
produce the binary length prefix, so use the Python client for manual checks.

> **Write the reference client against this document.** It needs to wrap the
> socket in TLS, send `protocol_version: 2`, persist and resend
> `player_id` / `session_token`, and accumulate `chat_delta` frames. One that
> skips the credential round-trip is refused at the `identified` step, which
> makes it useless as a test tool.

### Stub servers worth having

The UE5 client's integration checklist exercises failure paths that a healthy
server never produces. Small stubs make them reproducible:

| Stub behaviour | What it verifies on the client |
|---|---|
| Accepts TLS, never sends `identified` | Pending-utterance cap (`MaxPendingChats`, default 32); no unbounded growth |
| Sends `identified` without `session_token` | Loud failure, no silent downgrade to an unauthenticated session |
| Sends a few `chat_delta` then stops | Chat timeout fires; UI does not hang on "생각 중..." |
| Streams `chat_delta` for >60 s then completes | Long generations are **not** timed out |
| Floods frames as fast as possible | Inbound cap closes the connection, then reconnect |
| Floods `action_request` | Rate limiter replies `rate limited`, framerate holds |
| Accepts TCP but speaks plaintext | Client refuses to downgrade; retries as TLS |
| Presents a certificate with a different key | Pin mismatch rejected |
| Sends `session_taken_over`, then closes | Reconnect loop **stops**; no eviction war |
| Sends `server_busy` carrying the outstanding `chat.id` | Turn fails immediately, not at the 60 s timeout |
| Answers three rapid-fire `chat` frames in order, one turn at a time | Serial turns render correctly; no interleaved text (§3.6) |
| Interleaves `chat_delta` from two different `id`s | Client discards the buffer on the id change instead of merging |
| Drops the connection mid-turn, then answers the old `chat` on the next one | Client must not attribute a stale reply to the new utterance (§3.5) |

---

## 9. Client-enforced limits (reference)

Everything here is **client configuration**, not protocol. It is documented so a
server implementer can see what a well-behaved server must stay under, and what
happens when it does not. All values are per-project settings on the UE5 client
(`Project Settings > Plugins > Hermes Agent NPC`, persisted to
`Config/DefaultGame.ini`); the defaults below are what ships.

**Treat the existence of each limit as guaranteed and the number as advisory.**
A server that assumes the defaults will break on a project that tuned them.

### Transport and queues

| Setting | Default | Behaviour on breach |
|---|---|---|
| `MaxInboundQueueSize` | 1024 frames | Connection is **closed**, then reconnected. |
| `MaxInboundFramesPerTick` | 64 frames | Excess is deferred to the next tick, not dropped. |
| `MaxOutboundQueueSize` | 256 frames | New outbound frames are **dropped**. |
| `MaxPendingChats` | 32 utterances | Oldest pending utterance is dropped (FIFO). |

`MaxPendingChats` bounds utterances the player produced **before `identified`
arrived**. A server that accepts the connection and then never completes the
handshake cannot make the client grow without bound — it just loses the oldest
messages.

Frame body size is capped at **1 MiB** by the framing layer itself (§1) and is
not configurable — it is part of the protocol.

### Liveness

| Setting | Default | Meaning |
|---|---|---|
| `ActionTimeoutSeconds` | 15 s | Client's own deadline for producing `action_result` (§4.5). |
| `KeepAlivePingIntervalSeconds` | 20 s | Send-silence before the client pings (§4.7). |
| `PeerTimeoutSeconds` | 60 s | Receive-silence before the connection is declared dead (§4.7). |
| `ChatResponseTimeoutSeconds` | 60 s | Silence after `chat` before the turn is failed; any `chat_delta` resets it (§4.9). |

> **Status:** `ActionTimeoutSeconds`, `KeepAlivePingIntervalSeconds`, and
> `PeerTimeoutSeconds` are enforced today. The client answers a server `ping`
> with `pong`, **initiates** its own `ping` after that much send-silence, and
> reaps a connection that has been receive-silent past `PeerTimeoutSeconds`.
> `ChatResponseTimeoutSeconds` is not consumed yet — the client does not time out
> a `chat` turn; that lands with the remaining v2 protocol work. A server built
> to §4.7 and §4.9 is correct either way.
>
> Note that the client sets no socket-level `SO_KEEPALIVE` on the plaintext
> path — UE 5.8 exposes no API for it — so liveness there rests entirely on the
> application-level `ping` above. That detects a half-open connection within
> `PeerTimeoutSeconds`, but **the server should still run its own liveness
> check** rather than assume the client will always drop first.

### Reconnect backoff

| Setting | Default | Notes |
|---|---|---|
| `InitialReconnectDelay` | 0.5 s | Doubles after each failed attempt. |
| `MaxReconnectDelay` | 30 s | Ceiling. Retries continue **indefinitely**. |

The client never gives up and never downgrades the transport to get a
connection. A server that is down simply sees a reconnect every 30 s.

**Two codes suspend the loop** rather than feeding it (§5):
`session_taken_over` (the session is another connection's now) and
`unsupported_version` (retrying cannot fix a version mismatch). Both are
terminal until the game deliberately re-identifies. Every other disconnect —
including a silent socket death — goes back through the backoff.

### Action parameter bounds

See the table in §6 — `MaxWorldCoordinate`, `MaxItemQuantity`,
`MaxItemIdLength`, `MaxActionsPerSecond`.

### TLS settings

| Setting | Default | Notes |
|---|---|---|
| `bUseTLS` | `true` | Plaintext is development only (§0). |
| `TlsServerName` | empty | SNI / certificate hostname; falls back to the configured host. |
| `TlsPinnedPublicKeyHashes` | empty | base64 SPKI SHA-256 pins; non-empty enables pinning (§0). |
| `TlsPrivateCaPath` | empty | PEM path relative to the project directory. |
| `TlsHandshakeTimeoutSeconds` | 10 s | Handshake deadline. |

> **Status:** these keys exist on the client and are carried into the transport
> layer, but the TLS transport that consumes them is not implemented yet. Until
> it is, the client speaks plaintext TCP regardless of `bUseTLS`. §0 is the
> contract the server should be built against; do not read this table as a
> statement that the client currently verifies your certificate.
