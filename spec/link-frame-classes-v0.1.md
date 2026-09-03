# Link Frame Class Audit v0.1

**Status:** Research Draft
**Layer:** Link
**Applies to:** Link frame major 0, and the contract that must be settled before a stable Link major

This document decides, for every assigned Link frame class, what its payload is
and how it behaves. Before it existed, the decoder accepted all ten classes
while only some had any defined payload — a frame whose class is accepted but
whose payload nobody specified is an interoperability failure waiting for its
first independent implementation.

---

## 1. The rule this audit applies

> Every assigned frame class must have a defined payload and behaviour, an
> explicitly empty payload contract, or be reserved. There is no fourth option,
> and "the reference implementation happens to do X" is not one of the three.

`payload` is **not** "canonical Wire bytes". The class selects which contract
the payload follows. A payload interpreted under two classes is a payload with
two meanings, which is how independent implementations disagree while both
believe they decoded successfully.

## 2. Summary

| Class | Value | Payload | Status |
|---|---|---|---|
| `CONTACT` | 0 | canonical Wire Tier-0 object | defined |
| `CAPABILITY` | 1 | canonical Wire Tier-0 object | provisional — own contract pending |
| `NEGOTIATION` | 2 | canonical Wire Tier-0 object | provisional — own contract pending |
| `DATA` | 3 | canonical Wire Tier-0 object | defined |
| `ACK` | 4 | Link ACK control | defined here |
| `NACK` | 5 | Link NACK control | defined here |
| `KEEPALIVE` | 6 | **empty** | defined here |
| `ADAPT` | 7 | — | **reserved**, see §7 |
| `HANDOFF` | 8 | Link handoff control | [link-handoff-control-v0.1.md](link-handoff-control-v0.1.md) |
| `CLOSE` | 9 | Link CLOSE control | defined here |

## 3. Per-class contract

For each class below: allowed payload, required and forbidden flags, effect on
state, correlation, duplicate and retransmission behaviour, sequence and session
interaction, freshness, and what happens in the wrong state.

### 3.1 CONTACT (0) and DATA (3)

- **Payload** — exactly one canonical Wire Tier-0 object, filling `payload_len`
  exactly. Trailing bytes are a malformation.
- **Flags** — none required. `SESSION` permitted once a session exists.
- **State effect** — **none.** Reception is not identity, authority or trust
  (charter §2.3). An `AUTHORITY_CLAIM` arriving in a `CONTACT` frame does not
  become authority by arriving.
- **Correlation** — `source_ref`, and `session_ref` when present.
- **Duplicates** — not deduplicated by Link. A repeated object is delivered
  again; whether that matters is semantic, and the deployment decides.
- **Sequence** — ordinal of this frame when `SEQUENCE` is set. Advisory.
- **Freshness** — advisory; see §4.
- **Wrong state** — none defined; these are valid at any point in a contact.

The distinction between the two is intent, not format: `CONTACT` carries
first-contact and contact-management semantics, `DATA` carries ongoing
application-level semantics. Both decode identically.

### 3.2 CAPABILITY (1) and NEGOTIATION (2)

Currently carry a Wire Tier-0 object, decoded exactly as `CONTACT` does.

**These are provisional.** The minimum capability exchange and the
version/context controls both need their own Link control payloads, and when
those exist these classes will carry them rather than a semantic object. They
are recorded here as an open contract rather than frozen, because freezing them
now would freeze a shape chosen before the thing that uses it exists.

Until then, an implementation MUST NOT assume a Link control payload in these
classes.

### 3.3 ACK (4) and NACK (5)

Both carry a fixed 4-byte control:

```
offset  size  field
------  ----  ---------------------------------------------
     0     1  control_version = 0
     1     1  reason
     2     2  acked_sequence
```

- **Flags** — the acknowledged frame MUST have carried `SEQUENCE`. An
  acknowledgement of a frame with no sequence has nothing to name, and MUST NOT
  be sent. `SESSION` is required when the acknowledged frame carried one.
- **State effect** — **none.** An `ACK` is a statement about carriage, never
  about meaning, authority or acceptance of what a payload asked for. A peer
  that refuses a request answers with a semantic refusal in a `DATA` or
  `CONTACT` frame, not with `NACK`.
- **Correlation** — `acked_sequence`, and nothing else.
- **Duplicates** — an `ACK` may be re-sent freely and is idempotent. A receiver
  MUST tolerate duplicates.
- **Wrong state** — none; both are valid whenever the named frame could have
  been received.

For `ACK`, `reason` MUST be 0, and a receiver MUST reject an `ACK` whose reason
is not 0. A field with no defined meaning that a decoder accepts anyway is where
an undocumented sub-protocol starts.

#### `acked_sequence` is not the frame's own `sequence`

These are two different numbers and MUST NOT be conflated:

```
    frame.sequence     the ordinal of THIS acknowledgement frame
    acked_sequence     the ordinal of the frame BEING acknowledged
```

Overloading one field for both is the obvious byte saving and it is wrong. It
makes an acknowledgement indistinguishable from an ordinary frame that happens
to have that ordinal, and it makes an acknowledgement stream unorderable.

This is not hypothetical. The `mcl-ip` over-air harness sends `ACK` and `NACK`
with no correlation at all, and a single lost datagram shifted every later reply
by one — which presented as a **string of protocol failures that never
happened**, and cost real time to diagnose as an instrument fault. A
correlation field turns that from a mystery into a mismatch the receiver detects
on the first frame.

#### What may be acknowledged at all

> `ACK` and `NACK` correlate **only** a frame that was successfully framed and
> whose `SEQUENCE` field was present and valid. A frame that fails before that
> point is dropped locally, and the carriage resynchronises. **No `NACK` is
> sent.**

This is a correction of an earlier draft of this document, which listed
`MALFORMED`, `TRUNCATED`, `UNSUPPORTED_VERSION`, `UNSUPPORTED_CLASS` and
`FRAME_CHECK_FAILED` as `NACK` reasons. Every one of those is unsendable, for
the same reason in five forms:

**A receiver that rejects a frame cannot quote a trustworthy `acked_sequence`
from it.**

- An unsupported `link_major` is rejected *before* the receiver is entitled to
  interpret any later field. Reading bytes 2–3 as "this version's sequence" is
  exactly the interpretation the version check just forbade. "NACK sequence 42,
  unsupported version" is a self-contradiction: the 42 was read under a version
  the receiver said it does not implement.
- A truncated frame may not contain the sequence field at all.
- A frame whose CRC failed has no field a receiver may rely on, including that
  one — the CRC failing is the statement that its bytes are not what was sent.
- A malformed frame has no defined field positions.
- An unimplemented class is rejected by the header, before any payload contract
  applies.

There is a second reason, and it would be sufficient on its own. Nothing at
this layer is authenticated, and the media are open. A rule that answers
malformed bytes with a frame turns any transmitter in range into a source of
replies from every MCL node that hears it — an amplification and reflection
primitive obtained by transmitting noise. Answering only well-formed,
correlated frames removes it.

#### NACK reasons

Every reason below describes a frame that was **valid** and was **refused**.

| Value | Name | Meaning |
|---|---|---|
| 0 | RESERVED | never sent; a zeroed payload is not a valid NACK |
| 1 | PAYLOAD_REFUSED | the frame was well formed; its payload violated its class contract |
| 2 | POLICY_REFUSED | the deployment declined. **Not an error** — a policy outcome, and interoperable behaviour |
| 3 | RESOURCE_EXHAUSTED | no capacity to accept it now. The same frame may succeed later |
| 4 | STATE_REFUSED | not admissible in the receiver's current state |
| 5 | UNSUPPORTED_SEMANTIC | the payload decoded and named something this receiver does not implement |

`POLICY_REFUSED` is deliberately distinct. Two correctly implemented peers with
different configurations refuse each other at different points, and that is a
policy outcome rather than an interoperability failure (charter §2.10.1). A peer
must be able to say so without claiming the other sent something wrong.

`RESOURCE_EXHAUSTED` is distinct from all of them because it is the only one
where **retrying the identical frame is sensible**. Collapsing it into
`PAYLOAD_REFUSED` would make a sender treat a full buffer as a permanent
protocol error.

#### Sequence wrap and the acknowledgement window

`sequence` is 16-bit and wraps at 65536. Once a 16-bit `acked_sequence` is
normative, wrap stops being cosmetic: a delayed acknowledgement of sequence 7
is indistinguishable from an acknowledgement of the sequence 7 that comes 65536
frames later, and a sender that accepts the wrong one believes a frame arrived
that never did.

The rule, which is RFC 1982 serial-number arithmetic bounded well inside half
the space:

> An implementation MUST NOT have more than **16384** frames outstanding and
> unacknowledged on one contact. An `acked_sequence` is interpreted relative to
> the highest sequence sent: it names a live frame only if it lies within that
> window, going backwards. Anything else MUST be discarded rather than matched.

16384 is a quarter of the space rather than the half RFC 1982 permits, because
the boundary case at exactly half is ambiguous by construction, and a limit that
is ambiguous at its own edge is a limit an implementer will get wrong. Nothing
in MCL needs 16384 frames in flight; a contact that does has a queueing problem
this field cannot fix.

The sequence space is scoped to the **contact** and starts at 0 when the contact
begins. It is not reset by a migration: the contact continues across one, and so
does its numbering, for the same reason `session_ref` does.

### 3.4 KEEPALIVE (6)

- **Payload** — **explicitly empty.** `payload_len` MUST be 0. A non-empty
  `KEEPALIVE` is refused rather than ignored: accepting bytes nobody has defined
  is how an undocumented sub-protocol appears between two vendors.
- **Flags** — none required.
- **State effect** — none. It is evidence that something transmitted, and
  reception is not identity.
- **Response** — none required. It is not a ping/pong; a peer wanting a reply
  sends something that defines one.
- **Sequence** — permitted, and if a peer wants a `KEEPALIVE` acknowledged it
  must set `SEQUENCE` so an `ACK` has something to name.

### 3.5 CLOSE (9)

Carries a fixed 2-byte control:

```
offset  size  field
------  ----  ---------------------------------------------
     0     1  control_version = 0
     1     1  reason
```

| Value | Name |
|---|---|
| 0 | RESERVED |
| 1 | NORMAL — the contact is finished |
| 2 | GOING_AWAY — this machine is shutting down or leaving |
| 3 | POLICY — local policy ended it |
| 4 | TRANSPORT_LOST — the carrying transport is no longer usable |

- **State effect** — the sender treats the contact as closed. A receiver **MAY**
  close, and is not obliged to: `CLOSE` is a notification, not an instruction,
  and a frame that could force a peer to drop a contact is a denial-of-service
  primitive available to anyone in earshot. The medium is observable and nothing
  here is authenticated.
- **Duplicates** — idempotent. A closed contact is inert.
- **Wrong state** — a `CLOSE` for an unknown session is discarded silently.

### 3.6 HANDOFF (8)

Specified in [link-handoff-control-v0.1.md](link-handoff-control-v0.1.md).

One addition from this audit, for multi-contact routing:

> A `HANDOFF` frame carrying a post-acceptance control (`PATH_CHALLENGE`,
> `PATH_RESPONSE`, `COMMIT`, `CONFIRM`) MUST set `MCL_LINK_FLAG_SESSION`, and
> its `session_ref` MUST equal the control's. A receiver MUST reject a
> post-acceptance `HANDOFF` frame that does not set it — not merely decline to
> check it when absent.

A second requirement, from the same audit:

> A post-acceptance `HANDOFF` frame MUST be transmitted on, and MUST have
> arrived on, the transport the current transaction is being conducted over —
> the candidate while a migration is in progress, the active transport
> otherwise. A control arriving on any other transport MUST be refused.

Without that rule a `PATH_RESPONSE` delivered over the **old** path validates a
candidate that has never carried a byte. Every reference in such a frame is
correct, so nothing else in the sequence can detect it, and demonstrating that
the candidate carries traffic in both directions is the entire content of path
validation.

The reason is dispatch. These controls arrive on the **candidate** transport,
which the contact has not been using. A machine holding several contacts must
decide which one a freshly arrived frame belongs to *before* it can parse a
class-specific payload — otherwise generic frame routing has to reach into every
class's payload format, and every new class becomes a change to the router.

The Link header already carries the routing field. Requiring it costs four bytes
on frames that are 10 or 18 bytes of payload, on a transport that has just been
chosen for being better than the one where bytes were scarce.

## 4. Cross-cutting fields

### `sequence`

16-bit, contact-scoped, starting at 0 and wrapping at 65536. Link performs no
reordering, no retransmission and no duplicate suppression — it names frames so
that layers which do can. A receiver MUST NOT infer loss from a gap: transports
below MCL reorder and drop, and MCL-AP has no delivery guarantee at all.

Wrap is handled by the acknowledgement window in §3.3, which is the only place
a sequence is compared against another.

A sequence number is consumed by any frame that **may** have been transmitted,
not only by one confirmed sent. A transport that cannot report the outcome of a
transmission may still have transmitted it, and reusing the ordinal would give
two different frames one name.

### `freshness_ms`

Advisory, and interpreted by the **receiver's** clock policy. This library has
no clock and must not have one. A value of 0 means "no freshness claim", not
"expired". Freshness is not replay protection: nothing prevents a listener from
re-sending a frame within its own freshness window.

### `frame_check`

A CRC-32. Detects accidental corruption; stops no attacker, who recomputes it.
Not integrity, and named `frame_check` so it cannot be mistaken for it.

Whether it is required is a **transport profile** decision: a medium with its
own error detection may omit it, one without SHOULD require it. No profile has
been promoted to normative, so no such requirement exists yet.

## 5. Reserved: ADAPT (7)

`ADAPT` is **reserved** and MUST be rejected by a conforming decoder at Link
major 0.

It was assigned for transport adaptation — changing rate, profile or parameters
without a full migration. No MCL mechanism uses it, no payload has been
designed, and no evidence says it is needed: the cases considered so far are
served either by a transport's own adaptation, below MCL entirely, or by a
migration, which is specified. Freezing a class for a mechanism nobody has built
is exactly what the governing rule forbids.

Reserving costs nothing and can be undone; specifying it speculatively cannot.

## 6. What changes in the decoder

At Link major 0 the decoder accepts classes 0–9. This audit makes `ADAPT` (7)
rejected, which is a **behaviour change** and is recorded as such: a frame that
was previously accepted with an undefined payload is now refused. Nothing sends
one.

Classes 10–15 remain unassigned and rejected, as before.

## 7. Status and what is still open

Defined here: `ACK`, `NACK`, `KEEPALIVE`, `CLOSE`, and the `HANDOFF` session
requirement. Reserved: `ADAPT`.

Corrected here since the first draft: the `NACK` reason set, which previously
included five reasons a receiver cannot send because it has no trustworthy
`acked_sequence` to put in them; and the sequence wrap rule, which previously
said only that the field wraps.

Still open before a stable Link major:

- `CAPABILITY` and `NEGOTIATION` need their own control payloads, which depends
  on the minimum capability exchange and the version/context controls.
- No transport profile is normative, so the per-profile `frame_check`
  requirement has nowhere to live yet.
- Identifier lifetime, reuse and wrap rules are specified in prose here and need
  their own document.

No part of this has crossed a radio or been read by an independent
implementation.
