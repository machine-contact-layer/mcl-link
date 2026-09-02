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

## Status

Private research repository. Pre-v0.1 candidate specification.

See [`spec/link-v0.md`](spec/link-v0.md).
