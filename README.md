# MCL Link

`mcl-link` defines contact establishment, framing, sessions, addressing, quality-of-service, negotiation, and adaptation for the Machine Contact Layer.

It is transport-profile neutral. MCL-AP, MCL-IP, MCL-BLE, MCL-UWB, and future bindings implement the same link/session contract through different physical transports.

## Scope

- contact discovery lifecycle
- ephemeral node/session identifiers
- framing and multiplexing
- request/reply timing
- capability exchange
- transport/profile negotiation
- session establishment and teardown
- QoS and semantic priority handling
- replay/freshness hooks
- fallback and transport handoff
- extension negotiation

## Core state machine

```text
IDLE
  ↓ discovery
CONTACT
  ↓ capability exchange
NEGOTIATING
  ↓ profile/transport agreement
ESTABLISHED
  ↕ adaptation / QoS / context
HANDOFF or FALLBACK
  ↓
ESTABLISHED / IDLE
```

Acoustic-specific sounding and spectrum convergence are defined in `mcl-ap`, not here.

## Security research track

MCL's normal condition is that neither machine yet has a reason to trust the
other, and its first-contact medium is assumed observable. Nothing in MCL today
provides confidentiality, authenticity, or peer authentication, and no part of
the codebase implies otherwise.

Two research notes state the problem before any mechanism is chosen:

- [`research/secure-contact-threat-model.md`](research/secure-contact-threat-model.md)
  — what a secure contact must withstand, which security properties are distinct
  and must stay distinct, and what is already true in the code.
- [`research/secure-contact-candidate.md`](research/secure-contact-candidate.md)
  — prior art worth adopting rather than reinventing, and the one property
  (contact continuity across a transport change) that is MCL's own to define.
- [`research/contact-continuity-experiment.md`](research/contact-continuity-experiment.md)
  — the next experiment, and the eight attacks it has to survive.

The central result so far is a negative one: **proving knowledge of the contact
transcript proves nothing**, because first contact is observable and any
listener can compute the same value. A continuity proof must depend on secret
state both peers committed *during* the contact, which forces the key exchange
to begin on the first medium and finish on the second.

The Link frame's `frame_check` is a CRC-32. It detects accidental corruption and
provides no protection against a deliberate modification. It is named so that it
cannot be mistaken for a cryptographic mechanism.

## Migration as an on-wire protocol

`TRANSPORT_OFFER` and `TRANSPORT_ACCEPT` are Wire semantic objects with
canonical bytes. The rest of the migration — `PATH_CHALLENGE`,
`PATH_RESPONSE`, `COMMIT`, `CONFIRM` — belongs to Link, because it describes
this Link's own change of transport and means nothing outside it.

- [`spec/link-handoff-control-v0.1.md`](spec/link-handoff-control-v0.1.md) —
  the normative bytes
- [`registries/handoff-ops-v0.1.json`](registries/handoff-ops-v0.1.json) —
  the operation registry
- [`conformance/vectors/handoff-v0.1.json`](conformance/vectors/handoff-v0.1.json)
  — positive and negative vectors
- [`include/mcl/handoff.h`](include/mcl/handoff.h) — the codec
- [`include/mcl/rendezvous.h`](include/mcl/rendezvous.h) — resolving the
  `endpoint_token` to a real endpoint on the candidate transport

Until this existed the four controls were local function calls, and the sequence
diagram in `contact.h` named four messages that had no representation on any
wire. **A hardware demonstration driven by direct calls to `mcl_contact_*` on
both machines proves the radios work, not that the migration is specified.**

Two decisions worth knowing before reading the spec:

- **There is no ABORT.** Negative outcomes are expressed by absence and by the
  caller-enforced validity of the offer, because this library has no clock. An
  abort would add a faster path to a state the timeout already reaches, and one
  an observer of the references could send.
- **A lost `CONFIRM` is repaired by retransmission, not by abort.** Without it, a
  single dropped frame leaves one peer on the new transport and the other back
  on the old one, permanently, with no adversary involved. See §8 of the spec
  and `mcl_contact_commit_repeat`.

## Status

Private research repository. Pre-v0.1 candidate specification.

See [`spec/link-v0.md`](spec/link-v0.md).
