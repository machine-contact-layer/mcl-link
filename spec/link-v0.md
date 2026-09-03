# MCL Link v0

Status: **Research Draft**

## 1. Objective

MCL Link defines how two or more MCL-capable machines move from no prior relationship to a bounded contact session capable of exchanging MCL Core semantics.

It does not assume a specific transport.

## 2. Contact lifecycle

Working lifecycle:

```text
IDLE
  ↓
DISCOVERED
  ↓
CAPABILITIES
  ↓
NEGOTIATING
  ↓
ESTABLISHED
  ↕
ADAPTING
  ↓
HANDOFF / FALLBACK / CLOSED
```

### IDLE

No active MCL peer/session state.

### DISCOVERED

A valid MCL presence/contact indication has been observed.

### CAPABILITIES

Peers exchange the minimum information needed to select compatible semantic versions, link features, and transport/profile choices.

### NEGOTIATING

Peers select:

- MCL Core version
- MCL Wire version
- transport binding
- profile(s)
- frame size limits
- acknowledgement behavior
- priority support
- optional credential/freshness mechanisms

### ESTABLISHED

Peers exchange MCL semantic objects under a defined session context.

### ADAPTING

A transport may change rate, FEC, carrier map, endpoint, or other profile-specific parameters without changing MCL semantic meaning.

### HANDOFF

Peers migrate richer traffic to a negotiated transport while retaining the MCL contact/session relationship as allowed by policy.

## 3. Logical link frame

Transport profiles map this logical frame into their own physical/link representation.

```text
LinkFrame {
    link_version
    frame_class
    flags
    source_ref
    destination_ref?
    session_ref?
    sequence?
    freshness?
    wire_payload
    frame_check?
}
```

### 3.1 Canonical layout v0

The layout is now defined. It was deliberately left open until an MCL frame had
survived a physical channel, so that the mandatory fields are the ones contact
actually needs rather than the ones that seemed likely in advance.

All multi-byte fields are network byte order.

```text
u8   link_major (high nibble) | frame_class (low nibble)
u8   flags
u32  source_ref                       always
u32  destination_ref                  if FLAG_DESTINATION (0x01)
u32  session_ref                      if FLAG_SESSION     (0x02)
u16  sequence                         if FLAG_SEQUENCE    (0x04)
u16  freshness_ms                     if FLAG_FRESHNESS   (0x08)
u16  payload_len                      always
u8   payload[payload_len]
u32  frame_check (CRC-32/IEEE)        if FLAG_FRAME_CHECK (0x10)
```

Minimum frame size is 8 bytes plus payload. `link_major` is 0 for this draft.
Flag bits 5..7 are reserved.

`payload_len` MUST NOT exceed 1024, so the largest frame this version can
produce is 1048 bytes: 8 mandatory, 16 of optional fields when every flag is
set, and 1024 of payload.

#### There are three limits, not one

An earlier revision said only that a binding MUST size its carriage against
1048, which would force every future acoustic, optical or low-energy profile to
buffer a kilobyte because Link *permits* one — on exactly the media where bytes
and RAM are scarcest. That is not what the bound is for.

```
architectural maximum   1024 payload / 1048 frame
                        fixed by this version. The largest frame that can
                        exist at Link major 0.

profile maximum         <= architectural maximum
                        what a transport profile commits to carrying. A
                        profile MAY be smaller and MUST state its value.

negotiated maximum      min(peer A, peer B, active profile)
                        what these two peers will actually send each other.
```

The rules:

- An implementation MUST NOT emit a frame larger than the negotiated maximum.
- A decoder MUST reject a `payload_len` exceeding the maximum in force, and MUST
  NOT reject one merely for exceeding what it prefers.
- A transport binding MUST size its carriage — reassembly buffers, length
  prefixes, MTU accounting — against its **profile** maximum, and MUST state
  that maximum. A binding whose buffers are smaller than the limit it advertises
  cannot carry a legal frame, and because small frames are the common case, the
  failure surfaces only under load and looks like a transport fault rather than
  a specification mismatch.
- A profile maximum is a property of the profile, not of a peer's mood. It does
  not change during a contact; the negotiated maximum can only go down from it.

Capability exchange is where a peer states its maximum (§6). Until that exists,
an implementation has only the architectural maximum to work with, and every
current binding uses it.

### 3.2 Decoding rules

A conforming decoder MUST reject, never interpret:

- a `link_major` it does not implement, as an incompatible version;
- a `frame_class` outside the assigned set;
- any frame with a reserved flag bit set, as non-canonical;
- any buffer too short for the fields the flags declare;
- a `payload_len` exceeding the profile maximum;
- a frame whose `frame_check` is present and does not verify.

A decoder MUST report a buffer that ended early distinctly from a buffer that is
complete but malformed. The two demand opposite responses: a short buffer may
become a valid frame once more bytes arrive, so a stream carriage waits, while
malformed bytes never will, so the carriage must resynchronise instead. A
decoder that reports both identically forces the carriage either to stall on
corruption or to discard recoverable reads. Bindings MUST preserve this
distinction when translating into their own status vocabulary.

### 3.2.1 What the frame check is not

`frame_check` is a CRC-32. A CRC detects accidental corruption. It is **not** a
cryptographic mechanism and provides no protection against deliberate
modification: an adversary who alters a frame recomputes the CRC over the
altered bytes.

The field was originally called `integrity`. That name invited exactly the
wrong reading in a layer whose central rule is that reception is not identity,
authenticity, authority or trust, and it has been renamed. The wire bit is
unchanged; only the name is. Cryptographic authenticity, when MCL gains it,
belongs to a security profile above this layer and MUST be a separate mechanism
under a separate name, so that an implementation can never satisfy an
authenticity requirement by setting a CRC flag.

Absence of a `frame_check` does not make a frame trusted. It makes it
unchecked. Presence of one does not make a frame authentic. It makes it
undamaged. Carriage profiles decide whether to require the field, based on
whether the underlying medium already provides equivalent error detection.

A decoder reports the exact number of bytes the frame occupied, so a reliable
stream carriage can decode successive frames without rescanning. Trailing bytes
after a complete frame are not an error at this layer; carriage profiles that
forbid them enforce that against their own boundary.

### 3.3 What the references mean

`source_ref`, `destination_ref` and `session_ref` are contact correlation
references. They are not identity, not authority, and not trust, and a receiver
MUST NOT treat them as proof of any of those. `freshness_ms` bounds the useful
lifetime of the payload after decode; it is not a clock and does not require
synchronised time between peers.

#### Link `source_ref` and Wire `source_ref` are different fields

A framed semantic object carries two:

```
Link.source_ref     the immediate MCL peer that transmitted this frame
Wire.source_ref     the semantic origin of the object inside it
```

**They are related but MUST NOT be assumed equal, and neither may be substituted
for the other.** In the ordinary two-party case they will be the same value, and
that is a coincidence of topology rather than a rule.

They differ the moment anything relays. A machine that heard a `HAZARD` and
passes it on is the Link source of the frame it transmits; it is not the origin
of the hazard report, and rewriting the Wire `source_ref` to say so would
destroy the only record of who observed the hazard. Conversely, replying to the
Wire `source_ref` sends a frame to a machine that may be nowhere in range.

Rules:

- An implementation MUST NOT require the two to be equal, and MUST NOT reject a
  frame because they differ.
- An implementation MUST NOT copy one into the other.
- A receiver correlating a **contact** uses `Link.source_ref`. A receiver
  attributing a **claim** uses `Wire.source_ref`.
- Neither is identity. Attribution here means "the object says it came from
  reference X", not "it did".

Relaying itself is not specified yet — no MCL mechanism forwards an object — so
this is a rule about not foreclosing it, and about two implementations not
disagreeing over which reference identifies the claimant.

#### `destination_ref` and filtering

A frame with no `destination_ref` is for whoever hears it, which is how
broadcast first contact works. A frame that names one names it for a reason:

> A receiver that decodes a frame whose `MCL_LINK_FLAG_DESTINATION` is set and
> whose `destination_ref` is not its own MUST NOT deliver the payload to its
> semantic layer. It MAY report having heard the frame.

This is filtering, not access control. Nothing here is authenticated, the medium
is observable, and a `destination_ref` neither conceals a frame from anyone nor
proves who it was for. The rule exists so that on a shared bearer with several
nearby machines — which is the normal case for BLE and for acoustic — a node
does not act on objects explicitly addressed to its neighbour.

#### `session_ref` lifetime

`session_ref` names the **continuing logical contact** and persists for the life
of that contact, across every migration. It is bound by the first acceptance and
does not rotate per hop; an acceptance naming a different value MUST be refused.

The alternative model — one reference per transport epoch, rotating on each hop
— was rejected because continuity across a change of medium is the property the
reference exists to express, and under that model the identifier changes exactly
when it is needed. A peer that missed one hop could not tell a continuing
contact from a new one.

Abandoning a migration does not unbind it: the contact survives a failed
migration, and so does its reference.

If a rotating per-epoch identifier is ever needed — for unlinkability, say — it
must be a separate field with its own name, not this one reused.

## 4. Frame classes

Working classes:

| Class | Value | Payload |
|---|---|---|
| `CONTACT` | 0 | canonical Wire Tier-0 object |
| `CAPABILITY` | 1 | Wire Tier-0 object — **provisional** |
| `NEGOTIATION` | 2 | Wire Tier-0 object — **provisional** |
| `DATA` | 3 | canonical Wire Tier-0 object |
| `ACK` | 4 | Link ACK control, `mcl/control.h` |
| `NACK` | 5 | Link NACK control, `mcl/control.h` |
| `KEEPALIVE` | 6 | explicitly empty |
| `ADAPT` | 7 | **RESERVED — a conforming decoder MUST reject it** |
| `HANDOFF` | 8 | Link handoff control, [link-handoff-control-v0.1.md](link-handoff-control-v0.1.md) |
| `CLOSE` | 9 | Link CLOSE control, `mcl/control.h` |

Classes 10–15 are unassigned and MUST be rejected.

Each class's payload and behaviour is decided in
[link-frame-classes-v0.1.md](link-frame-classes-v0.1.md), which applies the
rule that **every assigned class must have a defined payload and behaviour, an
explicitly empty payload contract, or be reserved.** A class the decoder accepts
but whose payload nobody has specified is an interoperability failure waiting
for its first independent implementation.

`payload` is **not** "canonical Wire bytes" in general. The class selects which
contract the payload follows: `CONTACT` and `DATA` carry a Wire object,
`HANDOFF` carries a Link handoff control, `ACK`/`NACK`/`CLOSE` carry their own
Link controls, `KEEPALIVE` carries nothing, and `ADAPT` is reserved.

`ADAPT` being rejected is a **behaviour change** from earlier builds, which
accepted it with an undefined payload. Nothing sends one. See
[link-frame-classes-v0.1.md](link-frame-classes-v0.1.md) §5 for why reserving is
the correct outcome rather than specifying something.

## 5. Addressing

MCL must support first contact before stable identity has necessarily been verified.

Therefore link addressing distinguishes:

- ephemeral contact identifier
- session identifier
- semantic identity claim/reference
- transport-specific endpoint

A temporary contact ID MUST NOT be treated as proof of identity.

## 6. Capability model

Capabilities may include:

- supported MCL Core versions
- supported MCL Wire versions
- supported semantic extensions
- transport bindings
- transport profile IDs
- duplex mode
- maximum frame size
- supported priority classes
- supported security/freshness extensions

Profile-specific physical metrics remain in the profile repository.

## 7. QoS and semantic priority

MCL Link receives semantic priority metadata from MCL Wire/Core and maps it to transport-profile behavior.

Examples:

- retransmission budget
- stronger channel protection
- earlier contention slot
- lower latency budget
- field repetition
- discard policy

The mapping is transport-specific and must be observable in conformance tests.

## 8. Handoff

A contact session may negotiate another transport after initial contact.

Examples:

- acoustic → BLE
- acoustic → UWB
- acoustic → Wi-Fi/IP
- BLE → IP

MCL does not require handoff. A session may remain entirely on the initial transport.

Handoff does not imply trust. Local policy decides whether to accept a transport offer or expose an endpoint.

### 8.1 The sequence, and where each part is specified

```
old transport   TRANSPORT_OFFER   ->   Wire semantic object
                TRANSPORT_ACCEPT  <-   Wire semantic object

candidate       rendezvous beacon      mcl/rendezvous.h
                PATH_CHALLENGE    ->   HANDOFF control, operation 1
                PATH_RESPONSE     <-   HANDOFF control, operation 2
                COMMIT            ->   HANDOFF control, operation 3
                CONFIRM           <-   HANDOFF control, operation 4
```

The offer and the acceptance are Wire objects because they are things one
machine *means* to another. The four controls are not: they describe this
Link's own change of transport and mean nothing outside it. They are therefore
Link's, and are specified in
[link-handoff-control-v0.1.md](link-handoff-control-v0.1.md) with an operation
registry in [../registries/handoff-ops-v0.1.json](../registries/handoff-ops-v0.1.json).

The old transport stays active until `CONFIRM`. A migration that fails **before
`COMMIT` is transmitted** returns to it; a failed migration must never destroy
the contact.

Once `COMMIT` has been transmitted there is no return. The sender cannot know
whether it arrived, so it cannot know which transport the peer is on, and
rolling back would assert something unknowable. From there the only honest
outcomes are to retransmit until `CONFIRM` arrives, or to declare the **contact**
lost — not the migration. See
[link-handoff-control-v0.1.md](link-handoff-control-v0.1.md) §8.1.

Ordinary traffic is quiesced between `COMMIT` and `CONFIRM` (§8.3 of the same
document): in that window the peer may already have left the transport this
machine still considers active.

Which state machine owns what, and where the Link lifecycle and the contact
machine cross, is decided in
[link-contact-ownership-v0.1.md](link-contact-ownership-v0.1.md).

**Completing this sequence establishes reachability on the candidate path and
nothing else.** Every reference in it crosses an observable medium in the clear.
A listener that heard the original contact can complete the whole sequence and
be accepted exactly as an honest peer would. Establishing that the peer is the
one the contact began with requires cryptographic contact binding, which MCL
does not have.

## 9. Freshness and trust hooks

MCL Link reserves interfaces for:

- replay protection
- freshness/nonces
- credential references
- signature/MAC verification
- trust policy callbacks

A global credential system is outside the initial scope.

## 10. Multi-node behavior

The link layer must eventually define contention and broadcast behavior for multiple nearby nodes.

Initial research cases:

- one initiator / one responder
- one broadcaster / many receivers
- many machines hearing the same hazard
- multiple simultaneous discovery attempts
- authority broadcast with independent local responses

## 11. Failure behavior

Implementations must fail closed on ambiguous framing/context and preserve local control.

Examples:

- unknown mandatory version → refuse/ignore
- invalid context → request reset or drop delta
- negotiation timeout → return to discovery/fallback
- transport degradation → adapt or return to a robust profile
- unverified authority claim → expose claim to local policy, never auto-authorize solely from message type

## 12. Open questions

- minimum discovery/contact state needed before a session exists
- contention strategy for dense environments
- acknowledgement timing across very different propagation delays
- how much session state a receive-only node needs
- how link handoff should preserve context identifiers
- which behaviors belong in Link versus individual transport profiles
