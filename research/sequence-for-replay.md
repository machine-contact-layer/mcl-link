# Can the existing Link `sequence` carry MCL-S1 replay state?

Status: **Research.** Selects nothing and assigns nothing. Answers one question
the MCL-S1 benchmark must settle *before* round 2, because the wrong answer in
either direction is expensive: inventing a second counter that duplicates an
existing field wastes bytes on every protected frame forever, and reusing a
field that cannot bear the weight produces a security mechanism that silently
fails.

Date: 2026-09-06.

## 1. The question, stated precisely

Link major 1 already carries an optional 16-bit `sequence`, selected by
`MCL_LINK_FLAG_SEQUENCE`. A protected-record layer needs two things that look
like a counter:

- **A nonce input**, so that no two records are ever encrypted under the same
  key with the same nonce. Reuse is catastrophic for every AEAD MCL would
  plausibly use — it costs authenticity, and for the counter-mode
  constructions it costs confidentiality too.
- **A replay window**, so a record a receiver has already accepted is refused
  when it arrives again.

The question is whether `sequence` can serve either, both, or neither.

## 2. Answer

**Neither, without changing it — and it should not be changed.**

Four properties are missing, and they are missing for good reasons that have
nothing to do with security.

### 2.1 It is optional

`sequence` is present only when `MCL_LINK_FLAG_SEQUENCE` is set, and every
transmitter chooses. A nonce input that the sender may omit is not a nonce
input: the receiver cannot distinguish "this peer does not use sequence
numbers" from "this record has been stripped of its sequence number by
someone", and the second is exactly the attack a replay window exists to stop.

Making it mandatory is a wire change, and Link major 1 is cut.

### 2.2 It is 16 bits, and the wrap is not hypothetical

65 536 records. At a modest 20 protected frames per second on an IP bearer that
is **55 minutes** before the first repeat. A long-lived contact — which is the
whole point of a contact layer that survives bearer changes — reaches it.

A nonce may never repeat under one key. So a 16-bit counter forces a rekey
roughly hourly, and a rekey is precisely the machinery a compact profile was
trying to avoid. The usual repair is to make the counter wide enough that the
wrap is unreachable; 16 bits is not that.

### 2.3 It is not bound to a key epoch

Nothing in the frame says *which* key a record was protected under. After a
rekey, sequence 7 under the new key and sequence 7 under the old key are the
same two bytes. A receiver holding both keys during a transition — which it
must, or it drops records in flight — cannot tell them apart, and a replay
window keyed on the number alone rejects a legitimate new record or accepts a
replayed old one depending on which way it guesses.

### 2.4 It does not survive migration cleanly, and must not be made to

This is the one that settles it.

MCL contacts change bearer. `session_ref` persists across migration by design;
`sequence` is a per-frame field with no stated relationship to a migration at
all. Two readings are available and both are wrong:

- **Continue the counter across the migration.** Then the counter is contact
  state, and every bearer must agree on it at the moment of handoff — including
  the frames in flight on the abandoned bearer, whose fate is by definition
  unknown. That is a distributed-consensus problem introduced into a field
  whose job was frame ordering.
- **Restart the counter on the new bearer.** Then nonces repeat under the same
  key immediately, which is the failure this section exists to prevent.

### 2.5 What it is actually for

`sequence` is a **transport-ordering hint**. It lets a receiver notice
reordering and duplication from the medium, on one bearer, over a short window.
It has no cryptographic contract, is not covered by any integrity mechanism —
`frame_check` is a CRC-32 and `link.h` is explicit that it is not integrity —
and an attacker may set it to anything.

Reusing it would be the precise error `ARCHITECTURE_CHARTER.md` §2.11 forbids:
naming a mechanism for a property it does not have.

## 3. What MCL-S1 needs instead

A counter that lives **inside the protected record**, covered by the AEAD, and
therefore unforgeable rather than merely present.

Minimum requirements, in the order they were derived above:

| Requirement | Why |
|---|---|
| Mandatory whenever protection is on | §2.1 — an omissible nonce is not a nonce |
| Wide enough that the wrap is unreachable, or explicitly rekeyed before it | §2.2 |
| Bound to a key epoch | §2.3 — so a rekey does not alias |
| Defined behaviour across migration | §2.4 — and the definition must not be "restart" |
| Covered by the AEAD, not sitting beside it | §2.5 |
| Tolerant of reordering, not merely of loss | round 1 |

That last row is the round-1 finding that already narrowed the field: **Noise's
transport nonces are strictly incrementing and tolerate no reordering**, which
MCL's UDP and acoustic bearers require. A construction that needs in-order
delivery is not a candidate for a bearer-neutral contact layer, whatever its
handshake costs.

## 4. Consequence for the benchmark

Round 2 must **not** score candidates on the assumption that MCL supplies a
usable sequence number. It does not. Every candidate carries its own record
layer or needs one built, and that cost belongs in the comparison:

- **EDHOC** provides no record layer at all, so this cost is entirely additive
  — which is what made it worst against the round-1 decision rule despite being
  smallest on the wire at 101 bytes.
- **Noise** provides one via `Split()`, but with the ordering constraint above.
- **DTLS 1.3** provides the most complete datagram record machinery, including a
  replay window and epochs, which is precisely §2.3 and §2.4 solved — and it is
  the largest.

The decision rule fixed before the numbers stands: *the winner solves both key
establishment and protected contact with the least new MCL-specific security
machinery.* This document says the "new machinery" column may not be reduced by
borrowing `sequence`, for any candidate.

## 5. What this does not decide

It does not select MCL-S1, does not assign the `SECURITY` frame class, does not
assign a feature bit, and does not size the counter. Those wait on round 2,
which needs prototype integrations to measure code size and peak RAM — the two
axes on which the candidates actually differ, and the two that cannot be
obtained by reading specifications.

**None of this is claimed by v1.0.** `MCL Secure-Stranger 1` is a reserved name
with no specification, and `V1_SCOPE.md` says so.
