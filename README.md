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

## Status

Private research repository. Pre-v0.1 candidate specification.

See [`spec/link-v0.md`](spec/link-v0.md).
