# MCL-S1 benchmark, round 1: what the specifications say

Status: **Research Draft.** Selects nothing. Round 1 measures the dimensions
obtainable from the normative documents. Code size, peak RAM and the ten
MCL-specific experiments in `mcl-s1-benchmark-design.md` §5.4 need
implementations and are round 2.

Every figure below is from the cited specification, not from recall. Where a
document gives no number, this says so rather than estimating one.

Sources: EDHOC RFC 9528; COSE RFC 9052; the Noise Protocol Framework
revision 34; DTLS 1.3 RFC 9147; OSCORE RFC 8613 as prior art.

## 1. Handshake cost

**EDHOC** — three mandatory messages, two round trips, plus an optional
`message_4`. RFC 9528 Table 1 gives worked sizes in bytes:

| Authentication | msg 1 | msg 2 | msg 3 | total |
|---|--:|--:|--:|--:|
| Static DH + `kid` | 37 | 45 | 19 | **101** |
| Static DH + `x5t` | 37 | 58 | 33 | 128 |
| Signature + `kid` | 37 | 102 | 77 | 216 |
| Signature + `x5t` | 37 | 115 | 90 | 242 |

**Noise** — `XX` is three messages; `IK` is two. Sizes derive from the pattern
rather than a table: Curve25519 ephemeral 32 B, encrypted static 32+16 B, and a
16-byte AEAD tag per encrypted field. `XX` is roughly 32 / 96 / 64 ≈ **192 B**;
`IK` roughly 96 / 96 ≈ **192 B**.

**DTLS 1.3** — RFC 9147 gives no single handshake total. RFC 9528 states that
for raw-public-key authentication over CoAP, an EDHOC handshake "can be less
than 1/7 of the DTLS 1.3 handshake", which places DTLS well above 700 B.

### What this means against MCL's actual limits

Every candidate's largest single handshake message fits inside the 1024-byte
Link payload with room to spare. **Handshake size is therefore not the
discriminator on IP or BLE**, and choosing on compactness alone would be
choosing on the dimension that happens not to bind.

It binds on air, and the architecture already answers that. The smallest
option — EDHOC static DH + `kid`, 101 B total — is at the 64-byte
`AP-BOOTSTRAP-1` payload cap **three frames and roughly 8 seconds of airtime**,
before contention or retries. This is the arithmetic behind
`ap-bootstrap-requirements-v0.1.md` §2: no credential, key or handshake byte
crosses the acoustic bootstrap, and authentication happens after migration.
Round 1 confirms that decision rather than revisiting it.

## 2. Downgrade binding — requirement §1

| Candidate | Binding of MCL's own negotiation |
|---|---|
| **Noise** | **Native.** The prologue exists for exactly this: arbitrary data hashed into `h`, with both parties confirming their prologues are identical, and the specification names "confirm that prior negotiation was not modified by a man-in-the-middle" as its purpose. MCL's `CAPABILITY`/`NEGOTIATION` exchange goes in the prologue and needs no new mechanism. |
| **EDHOC** | **Available, via a field intended for it.** `EAD_1` is carried in `message_1` and is therefore inside the transcript, so the MCL negotiation transcript can be bound there. EDHOC additionally protects its *own* cipher-suite negotiation: the Responder verifies that no suite preceding the selected one in `SUITES_I` is supported. |
| **DTLS 1.3** | Protects its own negotiation. Binding MCL's would require an extension. |

Note what none of them fixes: the **stripped-feature** attack of
`mcl-s1-benchmark-design.md` §1. If the security bit is cleared in both
directions no handshake starts, so no transcript exists in any candidate. That
half is the deployment-profile policy floor, and it is a property of MCL's
layering rather than of any suite.

## 3. Post-handshake protection — requirement §2

This is where the candidates genuinely separate, and it inverts the ranking that
handshake size alone would produce.

| Candidate | What it provides after key establishment |
|---|---|
| **Noise** | `Split()` returns **two `CipherState` objects**, one per direction, with `EncryptWithAd`/`DecryptWithAd`, a 64-bit nonce incremented per message and a 128-bit AEAD tag. A working record layer, included. |
| **EDHOC** | **Nothing.** `EDHOC_Exporter(label, context, length)` yields keying material and stops there. EDHOC is key establishment; protecting subsequent traffic is another protocol's job — which is precisely why OSCORE exists beside it. |
| **DTLS 1.3** | A complete record layer: unified header from **2 octets** minimum, encrypted sequence numbers, an IPsec-style sliding replay window, `KeyUpdate` with epoch increment. |

**EDHOC scores best on handshake size and worst on the decision rule.** Choosing
it means MCL defines its own protected-record layer — nonce discipline, replay
window, key schedule, AAD construction — and every one of those is a piece
nobody outside this project has reviewed. `mcl-s1-benchmark-design.md` §6 fixed
that rule before these numbers existed, which is the only reason it can be
applied to them now.

## 4. Migration — requirement §5.2

| Candidate | Survives a bearer change? |
|---|---|
| **Noise** | Trivially. `CipherState` binds to no address; it belongs to whatever holds it, so MCL keeps it with the contact. |
| **EDHOC** | Trivially. Transport-agnostic by construction — RFC 9528 states EDHOC "is not bound to a particular transport" — and exporter keys carry no endpoint. |
| **DTLS 1.3** | By design, via **Connection ID**: an association survives a change of source address or port when both endpoints negotiate a CID. The closest thing to a purpose-built answer, at the cost of the rest of DTLS. |

## 5. The finding round 1 did not expect: reordering

MCL's bearers are not uniformly reliable. IP-DATAGRAM is UDP. Acoustic is lossy.
BLE-GATT is more reliable but not a stream.

- **Noise's transport nonces are implicit and strictly incrementing**, and the
  specification is explicit that reaching the maximum signals an error. Nothing
  in Noise tolerates reordering or gaps; `Rekey()` does not reset `n`. Over an
  unreliable bearer, MCL would have to add explicit sequence numbers and a
  replay window on top — the very machinery Noise appeared to supply.
- **DTLS 1.3 and OSCORE both carry an explicit sequence number and a sliding
  replay window**, because both were designed for exactly this. In DTLS replay
  detection is optional but specified; the window "MUST NOT be updated until
  that record has been deprotected successfully".
- **EDHOC** has no position here, having no record layer at all.

So the §3 ranking softens. Noise supplies a record layer that assumes
in-order delivery; MCL's bearers do not all provide it. **The residual
MCL-specific machinery is not zero for any candidate**, and the honest summary
is that it is smallest for Noise-plus-an-explicit-sequence-number, smaller still
for DTLS if its datagram semantics can be tolerated, and largest for EDHOC.

## 6. Standing scorecard

Round 1 only. Empty cells need implementations.

| Dimension | EDHOC+COSE | Noise | DTLS 1.3+CID |
|---|---|---|---|
| Smallest mutual-auth handshake | **101 B** | ~192 B | >700 B (derived) |
| Round trips | 2 | 2 (`IK`) / 3 (`XX`) | more |
| Largest message vs 1024 B Link payload | fits | fits | fits, needs its own fragmentation |
| Credential references | **kid, x5t, kcwt, kccs** | static keys only | certificates / PSK |
| Binds MCL negotiation | via `EAD_1` | **native prologue** | needs an extension |
| Record layer included | **no** | yes, in-order only | **yes, with replay window** |
| Replay window for unreliable bearers | no | **no** | **yes** |
| Key update | exporter re-derivation | `Rekey()`, nonce not reset | `KeyUpdate` + epoch |
| Survives migration | yes | yes | yes, via CID |
| Cipher-suite negotiation | yes, downgrade-checked | **none, by design** | yes |
| Identity protection | yes | pattern-dependent | yes |
| Fragmentation provided | n/a, messages small | **no** | yes |
| Transport neutrality | **explicit** | explicit | assumes datagram semantics |
| Code size | round 2 | round 2 | round 2 |
| Peak RAM | round 2 | round 2 | round 2 |
| Residual MCL machinery | **largest** | medium | **smallest, with a caveat** |

## 7. What round 1 does not decide

**No selection.** Two of the matrix's most decision-relevant rows — code size
and peak RAM on an ESP32-S3-class target — are empty, and the decision rule
weighs residual machinery, which cannot be settled from prose alone.

Round 2 needs three prototype integrations behind the `SECURITY` carrier and the
ten experiments in `mcl-s1-benchmark-design.md` §5.4, including glare, the
BLE→IP→BLE migration pair, and an altered protected `HANDOFF`.

Nothing is assigned: no suite, no frame-class value, no feature bit, no profile
identifier.

**One thing round 1 does settle**, because it is arithmetic rather than
preference: handshake compactness does not discriminate on IP or BLE, where
every candidate fits comfortably inside one Link frame. It discriminates only on
acoustic, where the architecture already forbids all of them. So MCL-S1 must not
be chosen for being small.
