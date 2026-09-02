# Contact Continuity: Experiment Design

Status: **Research Note** — non-normative. This defines an experiment, not a
specification. Nothing here is a candidate security profile, and no byte layout
in this document is proposed for adoption.

Companions:
[`secure-contact-threat-model.md`](secure-contact-threat-model.md) states the
problem; [`secure-contact-candidate.md`](secure-contact-candidate.md) surveys
what to adopt rather than invent.

## 1. The question

Every transport is now individually demonstrated: acoustic at E3/E4, IP over
2.4 GHz at E4, BLE at E4. That answers *can MCL frames cross these media*. It
does not answer the question that makes MCL more than a multi-radio framing
library:

> When a contact begins on one medium and continues on another, can each peer
> establish that the machine on the second medium is the machine it met on the
> first?

Call this **contact continuity**. It is the property MCL owes itself, because no
existing mechanism spans acoustic first contact and a later BLE or IP channel.

## 2. The constraint that shapes everything

From the threat model §5.1: **a proof built on public values proves nothing.**

First contact is observable. A passive listener obtains every nonce, contact
reference, capability advertisement and transport offer, and can compute any
hash over them. A peer that proves knowledge of that hash has proved only that
it was in the room.

Therefore:

> The continuity proof MUST depend on secret state committed by **both** peers
> during the original contact.

This forces a structural decision. The cryptographic exchange cannot start after
migration — by then it is too late for the first medium to have committed
anything. **It must begin on the first-contact medium and complete on the
second.**

If only the initiator has contributed before migration, an attacker can still
take the responder's place on the new channel. Both contributions must precede
the transport change, or the responder must commit separately while still on the
first medium.

## 3. Two distinct roles, never confused

```text
authenticated context     public     the thing being bound
handshake secret          private    the thing doing the binding
```

The transcript is the *context*. It is not a secret, is not a credential, and
must never be treated as one — the same discipline TLS 1.3 applies to its
exporter-derived channel bindings.

## 4. The logical contact transcript

The context must be authenticated, so it must be identical on both peers. It
therefore cannot be a hash of observed bytes.

Acoustic transmission repeats frames. BLE fragments them. Transports retry.
Sequence and freshness values differ per copy. Two entirely correct
implementations can observe the same negotiation through different byte
histories, and hashing byte history would make them disagree.

The transcript is therefore **logical**: a canonical encoding of the negotiation
as accepted, not as transmitted.

**Bound:** protocol domain separator; initiator and responder roles and
direction; contact references; fresh nonces from both peers; the complete
security-profile offers from both peers; the complete transport offers from both
peers; the selected security profile; the selected transport; and the rendezvous
parameters that were actually used.

**Excluded:** acoustic repetitions; BLE fragment boundaries; transport-local
retries and acknowledgements; `frame_check` values; any per-copy sequence or
freshness value that is not itself a negotiated term.

Binding the complete offers from both sides, rather than only the selection, is
what makes downgrade detectable: an attacker who suppressed a stronger profile
altered a value that is inside the authenticated context, so the mismatch
surfaces once keys exist.

## 5. Candidate message placements

The leading mechanism is EDHOC (RFC 9528), chosen because its transport is
supplied by the application profile — so one exchange may legitimately span two
media. Three placements, to be measured rather than assumed.

**Candidate A — messages 1 and 2 on AP, 3 and confirmation on BLE.** Preferred
starting point. Both peers commit secret state before migration, so the
transport race is pushed back into the first-contact adversarial model where it
belongs. Costs two acoustic messages.

**Candidate B — message 1 on AP, the rest on BLE.** Cheaper acoustically and
**rejected as the initial design**: only the initiator has committed before
migration, so an attacker can become the responder on the new channel. Viable
only with an independent responder commitment on AP, which is a design in its
own right.

**Candidate C — the entire exchange on AP.** A control, and architecturally
useful on its own: it demonstrates that a protected MCL session can exist where
no richer transport is available at all. Slow. Migration then becomes an ordinary
protected handoff rather than a security event.

Run A as the primary and C as the control. B is documented so that its weakness
is recorded rather than rediscovered.

## 6. Required negative tests

The honest path succeeding proves little — it would show only that three radios
carry the same bytes, which is established. **These are the experiment.**

| | Attack | Required outcome |
|---|---|---|
| A | Passive observer records every acoustic byte, computes the transcript, races onto BLE and attempts to continue | **Fail** — holds no handshake secret |
| B | A different peer runs a fresh, entirely valid key exchange after observing the contact | **Fail** continuity binding |
| C | Replay of a previous acoustic contact | **Fail** freshness and session binding |
| D | Security-profile offer altered in flight | Detected as authenticated-context mismatch |
| E | Stronger profile suppressed to force downgrade | Detected once keys exist |
| F | Transport selection or endpoint altered | Mismatch |
| G | Correct peer and valid handshake, invalid manufacturer credential | Continuity **succeeds**, `peer_identity_verified` stays false |
| H | Valid credential, locally forbidden request | Authentication **succeeds**, authorization **denied** |

G and H matter as much as A–F. They are what keep the properties separate in
practice rather than only in the charter: a run in which G or H "passes" by
granting more than it should would indicate the properties had quietly collapsed
into one.

## 7. What the experiment records

Per charter §2.11, separately and never summarised:

```text
contact_continuity
channel_confidentiality
channel_authenticity
peer_identity_verified
attestation_status
proximity_evidence
freshness_status
local_authorization
```

**The C representation of this vector is deliberately not designed yet.** It is
to be derived from what the experiment shows is actually needed. Freezing a
struct before the first run would be the same error as freezing a waveform
before an experiment earns it — a rule this project already follows in `mcl-ap`.

No `trusted` boolean, on any interface, in any repository.

## 8. Implementation constraints

- **No cryptographic primitive is written for MCL.** Adopt reviewed
  implementations.
- **No security code enters `mcl-link` during the experiment.** It lives in the
  experiment harness. `mcl-link` gains a specification only after the experiment
  has taught what the specification should say.
- The reference stack's freestanding constraints
  ([IMPLEMENTATION_CONTRACT](../../mcl-core/governance/IMPLEMENTATION_CONTRACT.md))
  still bind the protocol libraries. A crypto backend reached through callbacks
  does not violate them; a crypto library linked into `mcl-link` would.
- Lab credentials with explicit trust anchors first, so success and failure are
  unambiguous. A lab anchor is not a trust ecosystem and must never be described
  as one.
- No session resumption, no 0-RTT, no long-lived pairing shortcut. Each adds
  replay surface and buys nothing before the fresh-contact flow is secure.

## 9. Dependency evaluation gate

Before any external key-exchange implementation is used, it must be assessed
against, at minimum: conformance to its specification; validation against that
specification's published test vectors; licence compatibility with MCL's
intended royalty-free direction; bounded-memory and no-hidden-allocation
operation; whether private keys can be held through handles rather than exposed
as raw bytes, so a secure element or TrustZone can back them later; buildability
for the ESP32-S3 target; and the current published errata for the specification
it implements.

`libedhoc` is the candidate to assess first. **That assessment has not been
performed**, and this note does not claim it has.

## 10. Success milestone

```text
unknown machines X and Y
  -> contact over real MCL-AP
  -> both commit cryptographic material before migration
  -> migrate to real BLE
  -> complete the same exchange
  -> prove continuity to the original contact
  -> exchange a lab credential, only after protection exists
  -> apply local authorization
  -> reject a racing third peer            <- the result that matters
```

Retain exact logs and digests, as the transport campaigns did. The board's own
record and the host's record must agree, and where they disagree the run
describes an instrument rather than a protocol.

**Stop before declaring any security profile normative.** The output of this
experiment is knowledge, not a standard.
