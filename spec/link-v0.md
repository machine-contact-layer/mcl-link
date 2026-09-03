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

A transport binding MUST size its carriage — reassembly buffers, length
prefixes, MTU accounting — against that bound rather than against a limit
chosen independently. A binding whose limit is lower cannot carry a legal frame,
and because small frames are the common case, the failure surfaces only under
load and looks like a transport fault rather than a specification mismatch.

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

## 4. Frame classes

Working classes:

- `CONTACT`
- `CAPABILITY`
- `NEGOTIATION`
- `DATA`
- `ACK`
- `NACK`
- `KEEPALIVE`
- `ADAPT`
- `HANDOFF` — payload defined by [link-handoff-control-v0.1.md](link-handoff-control-v0.1.md)
- `CLOSE`

Of these, only `HANDOFF` currently has a defined payload contract. The rest
carry either a canonical Wire object (`CONTACT`, `DATA`, `CAPABILITY`,
`NEGOTIATION`) or nothing yet specified. **Before a stable Link major, every
assigned class must have a defined payload and behaviour, an explicitly empty
payload contract, or be reserved.** A class that is accepted by the decoder but
whose payload nobody has specified is an interoperability failure waiting for
its first independent implementation.

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

The old transport stays active until `CONFIRM`. A migration that fails at any
stage returns to it; a failed migration must never destroy the contact.

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
