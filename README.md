<p align="center">
  <img src=".github/banner.png" alt="OJOBIT" width="100%">
</p>

<h1 align="center">MCL Link</h1>

<p align="center"><strong>Keeping one contact alive — through framing, sessions, and a change of transport.</strong></p>

<p align="center">
  <a href="https://github.com/machine-contact-layer/mcl-link/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/machine-contact-layer/mcl-link/actions/workflows/ci.yml/badge.svg"></a>
  <a href="https://github.com/machine-contact-layer/mcl-link/blob/main/LICENSE"><img alt="License Apache-2.0" src="https://img.shields.io/badge/license-Apache--2.0-blue"></a>
  <img alt="Link major 1" src="https://img.shields.io/badge/link%20major-1%20Stable-brightgreen">
  <img alt="C99 freestanding" src="https://img.shields.io/badge/C99-freestanding-informational">
</p>

<p align="center">
  <a href="https://github.com/machine-contact-layer/mcl-sdk"><b>Use the SDK instead</b></a> ·
  <a href="https://github.com/machine-contact-layer/mcl-core"><b>Specifications</b></a> ·
  <a href="https://github.com/machine-contact-layer/mcl-wire"><b>mcl-wire</b></a>
</p>

---

> ### Most people should start with the SDK, not here
>
> This repository is a **specification**. If you are building a product, start
> with [**mcl-sdk**](https://github.com/machine-contact-layer/mcl-sdk): its quickstart runs two machines making
> contact, and the release ships a self-contained developer SDK — one CMake
> project, no sibling checkout. Come back here when you need to know exactly
> what a byte means, or when you are writing an independent implementation.

## Why this exists

A contact that dies when the radio changes is not a contact. MCL Link is what
survives the change: framing, session identity, negotiation, refusal, and
migration from one transport to another without starting over.

This is the layer every binding maps onto, which is why a `HAZARD` means the
same thing over a loudspeaker and over UDP. In one physical campaign a single logical
contact was preserved across **104 physical-medium changes**, including 100
alternating BLE/IP migrations.

**Link major 1 is Stable.** That migration campaign carried major-0 traffic;
Wire 1 inside Link 1 is evidenced separately, decoded over air on an embedded
target.

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

## Two state machines, and neither derives the other

A node holds two, and they answer different questions. Conflating them is the
mistake this section exists to prevent.

**`mcl_link_t` — the protocol lifecycle.** Nine states, defined normatively in
[`spec/link-v0.md`](spec/link-v0.md):

```text
IDLE → DISCOVERED → CAPABILITIES → NEGOTIATING → ESTABLISHED
                                                   ↕
                                    ADAPTING / HANDOFF / FALLBACK
                                                   ↓
                                                 CLOSED
```

**`mcl_contact_t` — transport continuity.** Which medium carries this contact,
and is a change of medium under way:

```text
ACTIVE → OFFERED → AGREED → VALIDATING → VALIDATED → COMMITTING → ACTIVE
                                                                    ↓
                                                                  CLOSED
```

**Neither implies the other, and neither is derived from the other.** A machine
can be settled on a transport having negotiated nothing; it can be
mid-negotiation with no migration in sight. An earlier revision provided a
function claiming a mapping between them, and it was removed: a third source of
truth beside two independently mutable ones drifts from both.

They cross in exactly **one** place, and the SDK owns it — a migration may only
be driven while the lifecycle is `ESTABLISHED` or `HANDOFF`, and the lifecycle
may not leave those states while a migration is outstanding. Recorded in
[`spec/link-contact-ownership-v0.1.md`](spec/link-contact-ownership-v0.1.md).

Sending and receiving ordinary frames is **not** gated on the lifecycle. First
contact necessarily happens before establishment, and a layer whose first frame
required an established session could never send one.

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
- [`include/mcl/endpoint_rendezvous.h`](include/mcl/endpoint_rendezvous.h) — resolving the
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

**Link major 1 is cut and is part of MCL v1.0.** What major 1 freezes is the
class dispositions in
[`spec/link-class-disposition-v1.md`](spec/link-class-disposition-v1.md) — nine
Stable frame classes, `ADAPT` permanently reserved and refused — not the frame
layout, which is byte-identical to major 0. The bump exists because the
project's version policy reserves major 0 for pre-standard work.

`mcl_link_frame_encode` still emits major 0, because v1.0 promises source
compatibility and silently moving an existing call to a new major would break
it invisibly. Use `mcl_link_frame_encode_at_major` to choose.

Other documents in this repository have their own explicit dispositions.
[`spec/link-v0.md`](spec/link-v0.md),
[`spec/link-negotiation-v1.md`](spec/link-negotiation-v1.md), and the contact
ownership and handoff-control specifications are Stable for their stated
major-1 scope; research documents remain below Stable.
`mcl-core/SPECIFICATION_INDEX.md` is the per-document answer, generated from
the tree rather than written by hand.
