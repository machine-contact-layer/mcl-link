# MCL-S1: what to measure, and why the question had to be repaired first

Status: **Research Draft.** Selects nothing, assigns nothing, and contains no
numbers yet. It fixes the question so the benchmark measures the right thing.

`security-carrier-design.md` settled where handshake bytes travel. Four things
were wrong or missing in the research question that followed, and each would
have been discovered *after* a suite was chosen, which is the expensive time to
discover them.

## 1. Downgrade resistance has two parts, and transcript binding is only one

`security-carrier-design.md` §4.1 requires `MCL-S1` to bind the `CAPABILITY` and
`NEGOTIATION` exchange into its transcript, so that a tampered negotiation
produces a failed authentication rather than a silent downgrade.

**That is necessary and it does not cover the obvious attack.**

Both machines support `MCL-S1` and advertise it. An attacker clears the bit in
both directions:

```text
A CAPABILITY: security bit = 1   ->  altered to 0
B CAPABILITY: security bit = 1   ->  altered to 0

features = L.features & P.features = 0
```

Neither machine starts `MCL-S1`. **There is no security transcript in which to
detect the manipulation**, because the exchange that would have carried the
binding never happens. A perfect handshake is bypassed by preventing it from
starting.

The fix is not cryptographic. It comes from the layers built in steps 2 and 6:

```text
POLICY FLOOR              prevents security being stripped entirely
  a deployment profile requiring MCL-S1 means:
    - the implementation MUST advertise the MCL-S1 feature;
    - failing to negotiate it is a SECURITY REQUIREMENT FAILURE;
    - it MUST NOT silently continue unsecured.

        +

TRANSCRIPT BINDING        detects alteration of an exchange that proceeds
  when MCL-S1 runs, its authenticated transcript binds the exact
  CAPABILITY/NEGOTIATION exchange that selected it.
```

Neither half is sufficient alone. The floor turns "the attacker removed
security" from a silent success into a refusal; the binding turns "the attacker
edited what security we chose" into a failed handshake.

**This is a requirement on the deployment profile schema, not only on the
suite.** `mcl-core/spec/deployment-profile-v1.md` currently forces
`security.profile` to `null` because no profile exists. When `MCL-S1` exists,
naming it there must carry the floor above, and the validator must derive
"guarantees SECURE" from it exactly as it derives the other two guarantees
today. The refusal must also be *legible* — the same requirement §5.1 of the
conformance profiles places on the no-common-bearer outcome. A machine that
silently declines to talk is indistinguishable from a broken one.

## 2. The largest gap: nothing protects the traffic after the handshake

The carrier design answers *where handshake messages travel*. It does not answer
what happens next, and that omission would have survived into a suite choice.

Suppose `MCL-S1` completes. The peer is authenticated. Then a machine sends:

```text
HANDOFF   DATA   ACK   NACK   CLOSE   migration controls   future semantics
```

If those remain clear and forgeable, **we have not built secure contact**. An
attacker waits for authentication to succeed and then tampers with everything
that matters. `HANDOFF` is the sharpest case: a mechanism that authenticates the
peer but leaves migration controls unauthenticated can have its *contact
continuity* attacked, and contact continuity is the thing MCL exists to provide.

So `MCL Secure-Stranger 1` must mean more than "the peer proved who it is":

```text
authenticated key establishment
        v
contact-bound security context
        v
protected subsequent MCL traffic

required:   peer cryptographic authentication
            message authenticity
            message integrity
            replay protection
            safe continuation across migration

profile decision, measured not assumed:
            confidentiality
```

### 2.1 Three shapes for protected records, all to be measured

**A — `SECURITY` carries a protected inner canonical Link frame.**

```text
outer Link SECURITY { security_profile_id, counter, protected(inner Link frame) }
        v  authenticate / decrypt
inner canonical Link frame  ->  ordinary Link processing
```

Architecturally the cleanest: Link keeps defining communication semantics and
`MCL-S1` merely carries protected canonical Link records. Every existing class
is protected without any of them changing. It costs a second Link header per
frame, which is a real cost on a constrained bearer and is exactly why it gets
**measured rather than rejected on overhead intuition**.

**B — a negotiated protected Link-frame form.** Authenticated header, protected
payload, one header total. Cheaper on the wire; couples the security design into
the frame layout, and needs care that the authenticated header covers everything
that matters.

**C — an existing transport-independent record construction**, adopted rather
than invented.

No preference is recorded here. All three are measured on the matrix in §4.

## 3. The candidate taxonomy was wrong

The carrier design said "EDHOC, COSE and the alternatives are benchmarked
separately". That is a category error and would have produced a meaningless
comparison.

**EDHOC uses COSE.** EDHOC is an authenticated key-establishment protocol; COSE
is a CBOR-based framework of cryptographic message and key structures. COSE is
not a stranger-handshake protocol and never competed with EDHOC.

The corrected comparison:

| Candidate | Why it is in the set |
|---|---|
| **EDHOC + COSE** | Compact authenticated key establishment designed for constrained devices, with credential references, cipher-suite negotiation, identity protection and exporter keys. Closest fit to the stated goals — which is a reason to measure it, not to choose it. |
| **Noise** | Two properties fit MCL unusually well: the **prologue** exists precisely to bind previously negotiated protocol data into the handshake, which is §1's transcript binding as a native feature; and a completed handshake yields **two transport cipher states**, which is §2's protected-record layer without inventing one. |
| **DTLS 1.3 + Connection ID** | A mature complete baseline — handshake, record layer, replay window, key update — and its Connection ID shows security state surviving endpoint change. Counter-consideration: it preserves datagram semantics, so genuine bearer-neutrality across BLE, IP and acoustic may be a poor architectural fit. Measured as the conventional baseline it is. |
| **OSCORE** | **Prior art, not a candidate.** CoAP-specific, so not adoptable directly. Its sender sequence number, replay window, AEAD context and external-AAD design are directly relevant to whatever protected-record layer MCL defines. |

## 4. The measurement matrix

Every candidate, every cell measured rather than argued:

| Dimension | Why MCL cares |
|---|---|
| Encoded handshake bytes, per message | Airtime and fragmentation on constrained bearers |
| Round trips | Time to secure contact |
| Code size, peak RAM | The ESP32-S3 class target of Experiment 008 |
| Required primitives | What a builder's crypto backend must supply |
| Credential-reference support | The alternative to shipping credentials whole — see `TWO_BUILDER_AUDIT.md` §3.4 |
| Identity exposure | What an observer of first contact learns |
| Downgrade binding | Whether §1's transcript binding is native or bolted on |
| Deterministic role / glare handling | §5.1 |
| Maximum record size vs the 1024-byte Link payload | Whether segmentation is needed at all |
| Segmentation / fragmentation burden | What MCL must build if it is |
| Key update and rekey | Long-lived contacts |
| Replay state shape | §5.3 |
| **Residual MCL-specific machinery** | The decision variable — see §6 |

## 5. MCL-specific behaviour the benchmark must exercise

These are the cases a general protocol comparison will not surface, because they
come from MCL's own shape.

### 5.1 Security-role glare

MCL negotiation deliberately has no initiator and no controller. EDHOC, Noise
and DTLS all have Initiator and Responder roles. If A and B both begin `MCL-S1`
at once, the outcome must be deterministic.

**The role must not be derived from an IP address, a BLE address, or who
connected first** — every one of those changes when the contact migrates, which
would make the role unstable across exactly the event MCL is built around. Derive
it from immutable contact material, or allow both handshakes and collapse them
deterministically. The migration tiebreaker in `mcl/contact.h` already solves a
structurally identical problem by comparing `(source_ref, migration_ref)` as one
logical key, and is the obvious place to look first.

### 5.2 Migration after authentication

Establish `MCL-S1` over BLE, migrate the contact to IP, continue protected
traffic **without re-authenticating merely because the bearer changed**. Then
migrate back. This is a required test, not a nice-to-have: a security context
that does not survive migration contradicts §4.4 of the carrier design and makes
`Secure-Stranger` a claim MCL cannot keep.

### 5.3 Replay and rekey state belongs to the contact

Sequence numbers, replay windows, traffic keys and key-update state **must not
live inside an IP or BLE adapter**. They belong to the transport-independent
contact, or they are lost at exactly the moment §5.2 exercises. OSCORE's
security context — sender sequence number, recipient replay window — is the
prior art for the shape.

### 5.4 The mandatory experiment list

```text
simultaneous security initiation (glare)
tampered CAPABILITY / NEGOTIATION
stripped required-security feature          -> must fail loudly, not continue
duplicate, reordered and replayed security messages
credential-reference resolution
authentication failure
establish over BLE, migrate BLE -> IP, continue protected
return migration IP -> BLE
replay state preserved across bearer change
altered protected HANDOFF / control frame   -> must be rejected
```

## 6. The decision rule, fixed before the numbers exist

Recorded now so it cannot be adjusted to fit whichever candidate measures best
on the dimension that happens to look good.

> **The winner solves both key establishment and protected contact with the
> least new MCL-specific security machinery.**

Not the smallest handshake. Compactness matters — airtime is this project's
scarcest resource — but a compact handshake that leaves MCL to invent its own
record layer, its own replay window and its own key schedule has moved the work
rather than done it, and every piece MCL invents is a piece nobody else has
reviewed.

A candidate that is larger on the wire and leaves MCL nothing to invent beats a
smaller one that does not.

## 7. What is still not decided

No suite. No frame-class value. No feature bit. No profile identifier. No crypto
provider API — that is designed around the selected mechanism's actual
requirements, which is the point at which `mcl-core/README.md`'s claim that MCL
defines the interface a mechanism plugs into finally becomes true in code
instead of in prose.
