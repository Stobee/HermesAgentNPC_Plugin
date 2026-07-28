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

## What changed from v1

If you implemented against v1, these are breaking changes:

| Area | v1 | v2 |
|------|----|----|
| Transport | plaintext TCP | **TLS 1.2+ required** (§0) |
| Identity | client invents `player_id` | **server issues** `player_id` + `session_token` (§3) |
| Reconnect auth | none — `player_id` alone was accepted | `(player_id, session_token)` pair is validated (§4.2) |
| `not_authorized` | optional allow-list | **required** (§5) |
| Streaming | none | `chat_delta` frames (§4.9) |
| Long actions | `action_result` claimed completion | `action_result` = accepted, `action_event` = finished (§4.10) |
| `move_to` result | `{ "arrived": true }` immediately — **a lie** | `{ "started": true }` then a later `{ "arrived": true }` event |
| Keepalive | server may `ping` | both sides `ping`; receive-silence means death (§4.7) |
| Version field | none | `protocol_version: 2` in `identify` (§4.1) |

The framing layer (§1) is unchanged. The action catalog (§6) keeps the same four
commands, but now documents the bounds the client enforces and each command's
completion class.

**There is no compatibility mode.** A v2 client closes the connection when
`identified` arrives without a `session_token`. A compatibility path is a path
that works without authentication, which defeats the purpose of v2.

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

- Pins are on the **public key (SPKI)**, not on the certificate. Renewing the
  certificate with the same key pair keeps existing pins valid; pinning the
  certificate fingerprint would force a client redeploy on every renewal.
- Clients verify the server in one of three ways: pinned SPKI, a configured
  private CA, or the system root store. Whichever applies, **verification
  failure closes the connection.**
- Plaintext TCP is **development only** and must not be used in deployed
  configurations. **Clients do not fall back to plaintext when TLS fails** —
  a failed handshake is retried as TLS, with backoff, indefinitely.
- Terminating TLS at a reverse proxy (nginx, Caddy) and speaking plaintext on
  loopback to the agent process is a supported deployment. The certificate and
  pin then belong to the proxy.

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
  proceeding. See "What changed from v1" above.

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
| `protocol_version` | int | yes | Always `2`. Reject other values with `unknown_type`-style `error`. |
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
| `chat_id` | string | yes | Conversation id for this player. |

> **`identified` must always carry both credentials, including on reconnect.**
> This is deliberate and load-bearing: it is what makes "no `session_token` in
> `identified`" an unambiguous v1-server signal. If the server omitted them on
> reconnect, a v2 client could not distinguish that from talking to a v1 server
> and would disconnect. Echoing values the client already holds costs nothing —
> the client overwrites them with identical values, and the channel is
> encrypted.
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

| Rule | Default |
|------|---------|
| Send `ping` after this much **send**-silence | 20 s |
| Declare the connection dead after this much **receive**-silence, then reconnect | 60 s |

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
| `code` | string | Machine-readable error code (§5). |
| `message` | string | Human-readable detail. |

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

> **Servers must batch deltas.** One frame per token is tens to hundreds of
> frames per second, which pressures the client's per-tick frame budget
> (default 64 frames/tick) and inbound queue cap (default 1024) during
> **normal** operation. Flush every **~50 ms or every few tokens**.
>
> Clients will not raise those caps to accommodate unbatched streams. The caps
> exist to stop abusive peers; a well-behaved server should never approach
> them. A client that hits the inbound cap closes the connection.

Deltas double as **progress signals**. The client fails a `chat` that has seen
neither a delta nor a final response within its chat timeout (default 60 s).
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

| `code` | Meaning | Connection |
|--------|---------|------------|
| `not_identified` | A `chat`/action was sent before `identify`. | kept |
| `unknown_type` | Unrecognized `type` value. | kept |
| `bad_frame` | Malformed JSON or length over 1 MiB. | **closed** |
| `unknown_command` | `action_result` / action referenced a non-whitelisted command. | kept |
| `not_authorized` | `(player_id, session_token)` did not validate, or one was sent without the other. **Required in v2.** | **closed** |

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
> | Constraint | Default | `error` on violation |
> |---|---|---|
> | `move_to` coordinates finite and `abs(v) <= MaxWorldCoordinate` | 1e7 cm | `coordinate out of range` |
> | `item_transfer.quantity` integer, `1 <= q <= MaxItemQuantity` | 999999 | `quantity out of range` |
> | `item_transfer.item_id` non-empty, length `<= MaxItemIdLength` | 64 | `invalid item_id` |
> | Action requests processed per second | 20 | `rate limited` |
> | Response deadline before the client self-reports failure | 15 s | `timeout` |
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
- `started`: bool. Always `true` when `ok=true` — present so the agent can
  distinguish "accepted" from a legacy `arrived` payload.

> `arrived: true` in `action_result` is a **v1 shape and is no longer valid**.
> It claimed completion at accept time, which told the agent the NPC had
> arrived when it had not yet moved.

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
   returns. If it has no `session_token`, you are talking to a v1 server —
   log loudly and disconnect.
4. Send `chat` frames for player utterances. Render `chat_delta.text`
   incrementally, then replace with `chat_response.text` when it arrives.
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
   to resume the conversation. Actions accepted before the drop will never
   produce their `action_event`.
9. Always frame with the 4-byte big-endian length prefix; always parse off the
   length, never off packet boundaries.

## 7b. Minimal server checklist

1. Terminate TLS (directly or behind a reverse proxy). Publish your SPKI pin if
   the certificate is self-signed.
2. Accept `identify`. If it carries no credentials, mint a `player_id` and a
   high-entropy `session_token` that is **not derivable** from the `player_id`,
   and store the mapping durably.
3. If it carries credentials, validate the pair. On mismatch — or if only one of
   the two is present — send `error { code: "not_authorized" }` and close.
4. Always answer with `identified` carrying **both** `player_id` and
   `session_token` (§4.2), plus `chat_id`.
5. Reject `chat` before `identify` with `not_identified`.
6. Stream replies as `chat_delta`, **batched to ~50 ms or a few tokens**, then
   close the turn with an authoritative `chat_response`.
6b. Treat `action_result` as acceptance, not completion. Feed a later
   `action_event` (§4.10) back to the agent as a tool observation. An accepted
   action that never produces an event has an **unknown** outcome — do not
   assume success.
7. Constrain LLM tool output with a JSON schema or grammar (§6) so only
   whitelisted commands and in-range parameters can be produced.
8. Rate-limit per authenticated session and bound your inference queue depth.
   The client's own limits protect the client, not you.
9. Answer `ping` with `pong`, and reap connections that go silent.

---

## 8. Quick manual test

A Python reference client lives at `scripts/ue5_test_client.py` on the server.
Run it on the same LAN to exercise the exact framing (connect → identify → chat
→ print response; auto-replies to `action_request`). Note: `nc`/`telnet` cannot
produce the binary length prefix, so use the Python client for manual checks.

> **The reference client must be updated for v2** before it is useful again:
> wrap the socket in TLS, send `protocol_version: 2`, persist and resend
> `player_id` / `session_token`, and accumulate `chat_delta` frames. A v1
> reference client is rejected at the `identified` step.

### Stub servers worth having

The UE5 client's integration checklist exercises failure paths that a healthy
server never produces. Small stubs make them reproducible:

| Stub behaviour | What it verifies on the client |
|---|---|
| Accepts TLS, never sends `identified` | Pending-utterance cap; no unbounded growth |
| Sends `identified` without `session_token` | v1 detection — loud failure, no silent downgrade |
| Sends a few `chat_delta` then stops | Chat timeout fires; UI does not hang on "생각 중..." |
| Streams `chat_delta` for >60 s then completes | Long generations are **not** timed out |
| Floods frames as fast as possible | Inbound cap closes the connection, then reconnect |
| Floods `action_request` | Rate limiter replies `rate limited`, framerate holds |
| Accepts TCP but speaks plaintext | Client refuses to downgrade; retries as TLS |
| Presents a certificate with a different key | Pin mismatch rejected |
