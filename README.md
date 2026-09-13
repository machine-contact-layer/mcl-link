<p align="center">
  <img src="https://raw.githubusercontent.com/machine-contact-layer/.github/main/profile/banner.png" alt="Machine Contact Layer (MCL) banner: black and white checkerboard with the OJOBIT wordmark" width="100%">
</p>

<h1 align="center">MCL Link</h1>

<p align="center"><strong>Keeping one contact alive — through framing, sessions, and a change of transport.</strong></p>

<p align="center">
  Machine contact lifecycle, framing, sessions and transport migration for the
  Machine Contact Layer (MCL): move a live machine-to-machine contact between
  BLE, IP and acoustic links without starting over. Freestanding C99.
</p>

<p align="center">
  <a href="https://github.com/machine-contact-layer/mcl-link/actions/workflows/ci.yml"><img alt="CI status" src="https://github.com/machine-contact-layer/mcl-link/actions/workflows/ci.yml/badge.svg"></a>
  <a href="https://github.com/machine-contact-layer/mcl-link/blob/main/LICENSE"><img alt="License: Apache-2.0" src="https://img.shields.io/badge/license-Apache--2.0-blue"></a>
  <img alt="Link major 1: Stable" src="https://img.shields.io/badge/link%20major-1%20Stable-brightgreen">
  <img alt="Language: freestanding C99" src="https://img.shields.io/badge/C99-freestanding-informational">
</p>

<p align="center">
  <a href="https://github.com/machine-contact-layer/mcl-sdk"><b>SDK</b></a> ·
  <a href="https://github.com/machine-contact-layer/mcl-core"><b>MCL overview</b></a> ·
  <a href="#specifications"><b>Specifications</b></a> ·
  <a href="https://github.com/machine-contact-layer/mcl-wire"><b>mcl-wire</b></a> ·
  <a href="https://github.com/machine-contact-layer/mcl-core/blob/main/REPORTING.md"><b>Report a defect</b></a>
</p>

---

A contact that dies when the radio changes is not a contact. MCL Link is what
survives the change: framing, session identity, negotiation, refusal, and
migration from one transport to another without starting over.

Every transport binding — [IP](https://github.com/machine-contact-layer/mcl-ip),
[BLE](https://github.com/machine-contact-layer/mcl-ble),
[acoustic](https://github.com/machine-contact-layer/mcl-ap),
[UWB](https://github.com/machine-contact-layer/mcl-uwb) — implements this same
link and session contract, which is why an object means the same thing over a
loudspeaker and over UDP. On real hardware, one logical contact was carried
across **104 changes of medium**, including 100 alternating BLE/IP migrations.

> **Building a product?** Start with [**mcl-sdk**](https://github.com/machine-contact-layer/mcl-sdk),
> which drives this layer for you. Come here for the frame layout, the state
> machines and the migration protocol.

## What MCL Link provides

- **The Link frame** — frame class, session reference, sequence, addressing and
  a frame check, with nine Stable frame classes
- **Contact lifecycle** — discovery, capabilities, negotiation, establishment,
  close
- **Transport negotiation** — agreeing which bearer a contact moves to
- **Transport migration as an on-wire protocol** — offer, accept, path
  validation, commit and confirm
- **Endpoint rendezvous** — resolving an `endpoint_token` to a real endpoint on
  the candidate transport
- **Deterministic refusal** — malformed, truncated, reserved and future-major
  frames are rejected before any semantic decoding

## Two state machines, and neither derives the other

A node holds two, and they answer different questions.

**`mcl_link_t` — the protocol lifecycle.** Nine states, defined in
[`spec/link-v0.md`](spec/link-v0.md):

```text
IDLE → DISCOVERED → CAPABILITIES → NEGOTIATING → ESTABLISHED
                                                   ↕
                                    ADAPTING / HANDOFF / FALLBACK
                                                   ↓
                                                 CLOSED
```

**`mcl_contact_t` — transport continuity.** Which medium carries this contact,
and whether a change of medium is under way:

```text
ACTIVE → OFFERED → AGREED → VALIDATING → VALIDATED → COMMITTING → ACTIVE
                                                                    ↓
                                                                  CLOSED
```

Neither implies the other. A machine can be settled on a transport having
negotiated nothing; it can be mid-negotiation with no migration in sight.

They cross in exactly one place, and the SDK owns it: a migration may only be
driven while the lifecycle is `ESTABLISHED` or `HANDOFF`, and the lifecycle may
not leave those states while a migration is outstanding. See
[`spec/link-contact-ownership-v0.1.md`](spec/link-contact-ownership-v0.1.md).

Sending and receiving ordinary frames is **not** gated on the lifecycle: first
contact necessarily happens before establishment.

## Transport migration on the wire

`TRANSPORT_OFFER` and `TRANSPORT_ACCEPT` are Wire objects with canonical bytes.
The rest of the migration belongs to Link, because it describes this Link's own
change of transport:

```text
TRANSPORT_OFFER / TRANSPORT_ACCEPT   current transport
PATH_CHALLENGE / PATH_RESPONSE       candidate transport
COMMIT / CONFIRM                     candidate transport
```

- [`spec/link-handoff-control-v0.1.md`](spec/link-handoff-control-v0.1.md) — the normative bytes
- [`registries/handoff-ops-v0.1.json`](registries/handoff-ops-v0.1.json) — the operation registry
- [`conformance/vectors/handoff-v0.1.json`](conformance/vectors/handoff-v0.1.json) — positive and negative vectors
- [`include/mcl/handoff.h`](include/mcl/handoff.h) — the codec
- [`include/mcl/endpoint_rendezvous.h`](include/mcl/endpoint_rendezvous.h) — endpoint token resolution

Two design decisions worth knowing before you implement it:

- **There is no ABORT.** Negative outcomes are expressed by absence and by the
  caller-enforced validity of the offer, because this library has no clock. An
  abort would add a faster path to a state the timeout already reaches, and one
  an observer could send.
- **A lost `CONFIRM` is repaired by retransmission.** Otherwise a single dropped
  frame would leave one peer on the new transport and the other on the old one.
  See §8 of the specification and `mcl_contact_commit_repeat`.

Once `COMMIT` is transmitted there is no rollback.

## Stable surface: Link major 1

Link major 1 is Stable. What it freezes is the class dispositions in
[`spec/link-class-disposition-v1.md`](spec/link-class-disposition-v1.md) — nine
Stable frame classes, with `ADAPT` permanently reserved and refused. The frame
layout itself is byte-identical to major 0.

**Choosing the major.** `mcl_link_frame_encode` still emits major 0, because
v1.0 promises source compatibility. Use `mcl_link_frame_encode_at_major` to emit
the Stable major.

## Security

MCL Link provides no confidentiality, authenticity or peer authentication.
`frame_check` is a CRC-32: it detects accidental corruption and provides no
protection against deliberate modification. `session_ref` correlates frames to
a conversation; it does not identify the peer. See
[`SECURITY.md`](https://github.com/machine-contact-layer/mcl-core/blob/main/SECURITY.md).

Design notes on secure contact across a transport change are in
[`research/`](research/).

## Specifications

| Document | Maturity |
|---|---|
| [`spec/link-v0.md`](spec/link-v0.md) | Stable for the Link frame layout at major 1 |
| [`spec/link-class-disposition-v1.md`](spec/link-class-disposition-v1.md) | Stable |
| [`spec/link-negotiation-v1.md`](spec/link-negotiation-v1.md) | Stable |
| [`spec/link-contact-ownership-v0.1.md`](spec/link-contact-ownership-v0.1.md) | Stable for the contact lifecycle and ownership rule |
| [`spec/link-handoff-control-v0.1.md`](spec/link-handoff-control-v0.1.md) | Stable for the migration control sequence |
| [`registries/transport-ids-v0.1.json`](registries/transport-ids-v0.1.json) | Transport identifiers |

Other documents in `spec/` are Research Drafts. The per-document answer is
[`mcl-core/SPECIFICATION_INDEX.md`](https://github.com/machine-contact-layer/mcl-core/blob/main/SPECIFICATION_INDEX.md).

## Related repositories

[mcl-wire](https://github.com/machine-contact-layer/mcl-wire) (canonical bytes) ·
[mcl-sdk](https://github.com/machine-contact-layer/mcl-sdk) (developer SDK) ·
[mcl-ip](https://github.com/machine-contact-layer/mcl-ip) ·
[mcl-ble](https://github.com/machine-contact-layer/mcl-ble) ·
[mcl-ap](https://github.com/machine-contact-layer/mcl-ap) ·
[mcl-uwb](https://github.com/machine-contact-layer/mcl-uwb)

## License

Apache-2.0. See [`LICENSE`](LICENSE).
