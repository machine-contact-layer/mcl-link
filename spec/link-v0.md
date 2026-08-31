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
    integrity?
}
```

Exact bit layout is delegated to MCL Wire / transport profile integration work.

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
- `HANDOFF`
- `CLOSE`

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
