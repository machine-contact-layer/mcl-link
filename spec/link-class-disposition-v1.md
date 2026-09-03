# Link frame classes — disposition for Link major 1

Status: **proposed**, satisfying `mcl-core/governance/V1_SCOPE.md` §5.3
Date: 2026-09-04

## 1. What this document decides

`V1_SCOPE.md` §3.4 disposes Link frame classes as *Stable per class* and adds
the constraint that governs every entry below:

> No class may be accepted by a Stable decoder while its semantics are merely
> implied.

Ten classes exist at Link major 0. This assigns each one of four dispositions
for Link major 1, and states what a conforming major-1 decoder does with it.

The four dispositions:

| Disposition | Meaning for a major-1 decoder |
|---|---|
| **Stable** | Frozen contract. MUST be implemented and accepted. |
| **Stable, optional to emit** | Frozen contract. MUST be accepted; an implementation need not ever send one. |
| **Reserved** | MUST be refused on receipt. MUST NOT be emitted. The value is permanently allocated and never reassigned. |
| **Excluded** | Not present at major 1. |

## 2. The dispositions

| Class | Value | Disposition | Contract |
|---|---:|---|---|
| `CONTACT` | 0 | **Stable** | Canonical Wire Tier-0 object. §3.1 of `link-frame-classes-v0.1.md`. |
| `CAPABILITY` | 1 | **Stable** | Link capability control, 9 bytes. [`link-negotiation-v1.md`](link-negotiation-v1.md). |
| `NEGOTIATION` | 2 | **Stable** | Link negotiation control, 7 bytes. Same specification. |
| `DATA` | 3 | **Stable** | Canonical Wire Tier-0 object. Decodes identically to `CONTACT`; the distinction is intent. |
| `ACK` | 4 | **Stable** | Fixed 4-byte control. Correlation by `acked_sequence`. |
| `NACK` | 5 | **Stable** | Fixed 4-byte control, same layout, carries a reason. |
| `KEEPALIVE` | 6 | **Stable, optional to emit** | Empty payload. A receiver MUST accept one; nothing requires an implementation to send them. |
| `ADAPT` | 7 | **Reserved** | Already refused at both encode and decode. See §4. |
| `HANDOFF` | 8 | **Stable** | `link-handoff-control-v0.1.md`. Carries the migration sequence proven by the E4 dual-radio evidence. |
| `CLOSE` | 9 | **Stable** | Link CLOSE control. |

**Nine Stable, one reserved.** No class is excluded: every value 0–9 is
permanently allocated, and the reserved value is refused rather than recycled.

This table was first written with `CAPABILITY` and `NEGOTIATION` conditional.
The condition — §5.4 of the v1 scope — has since been met, so the fallback
branch in §3 did not need to be taken. It is left in place as the record of
what would have happened otherwise.

## 3. `CAPABILITY` and `NEGOTIATION` — resolved

### The problem, as it stood

Both classes were accepted and handed to the Wire decoder:

```c
/* mcl-sdk/src/sdk.c */
if (frame->frame_class != MCL_LINK_CLASS_CONTACT &&
    frame->frame_class != MCL_LINK_CLASS_DATA &&
    frame->frame_class != MCL_LINK_CLASS_CAPABILITY &&
    frame->frame_class != MCL_LINK_CLASS_NEGOTIATION) {
    return MCL_SDK_OK;
}
```

A received `CAPABILITY` frame was therefore decoded as whatever Tier-0 object
its payload happened to encode. There is no `CAPABILITY` semantic object — the
seven Tier-0 objects do not include one — so the class had a *transport* but no
*contract*. `link-frame-classes-v0.1.md` §3.2 stated this honestly and told
implementations they MUST NOT assume a Link control payload in these classes.

That was an acceptable state for major 0. It is exactly the state §3.4 of the
scope forbids at major 1: the class is accepted, and its semantics are implied.

### Resolved: §5.4 landed, so the first branch applies

`mcl-link/spec/link-negotiation-v1.md` defines both control payloads, and
`src/negotiation.c` implements them with `tests/test_negotiation.c` covering
every obligation in its §8 — 4241 checks. Both classes are now Stable, carry
Link control payloads, and are no longer handed to the Wire Tier-0 decoder.

The analysis below is retained because it records why the class could not have
shipped in its previous state, and what the fallback would have been.

### The disposition was conditional on §5.4, and both branches were decided in advance

```text
IF the minimum capability/version exchange (V1_SCOPE 5.4) lands before
   Link major 1 is cut:

      CAPABILITY   -> Stable, carrying that exchange's control payload
      NEGOTIATION  -> Stable, carrying the version/profile control payload

      Both stop being handed to the Wire Tier-0 decoder. They carry Link
      control payloads, like ACK, NACK, HANDOFF and CLOSE do.

ELSE:

      CAPABILITY   -> Reserved at Link major 1
      NEGOTIATION  -> Reserved at Link major 1

      Refused on receipt, never emitted, values permanently allocated,
      promoted at a later Link major once the contract exists.
```

**Neither branch permits shipping them as they are.** Carrying a Tier-0 semantic
object under a class named `CAPABILITY` — with no rule about which objects are
legal there, what the class adds over `CONTACT`, or what a receiver does with
one — is the "merely implied" case. The fallback is deliberately the same
treatment `ADAPT` already receives, so it needs no new mechanism: the code to
refuse a reserved class exists and is tested.

**Recommended branch: the first.** §5.4 is on the critical path regardless, it
is small, and `V1_SCOPE.md` §3.4 already observes that a specification claiming
peers negotiate versions while no interoperable way to negotiate them exists is
the one outcome that cannot survive. But the second branch is a real fallback,
not a threat — it ships a smaller honest Link rather than a larger implied one,
and this document commits to it in advance so the choice is not made under
release pressure.

## 4. `ADAPT` — why reserved and not excluded

`ADAPT` was assigned for transport adaptation — changing rate, profile or
parameters without a full migration — and nothing was built on it. No payload
was designed and no mechanism uses it. Both halves of the codec already refuse
it, and the decoder carries the reasoning.

Reserved rather than excluded because the value is spent either way. Excluding
it would invite value 7 to be reassigned to something else at a later major,
and a peer that once meant "adapt" by 7 would then be misread rather than
refused. A refused value is a permanent tombstone, which is what
`REGISTRY_POLICY.md` requires of every retired assignment.

## 5. What a conforming major-1 decoder does

```text
frame_class > 9                  -> reject, MCL_LINK_ERR_RANGE
frame_class is Reserved          -> reject, never guessed
frame_class is Stable            -> decode per its frozen contract
reserved flag bit set            -> reject
payload longer than the maximum  -> reject
```

Unknown meaning is refused, never inferred. That rule already holds at major 0
and does not change.

## 6. What this document does not do

- **It does not change the frame layout.** Every disposition above describes
  behaviour that exists and is tested. Resolving §3 did change one thing in the
  SDK: `CAPABILITY` and `NEGOTIATION` are no longer passed to the Tier-0
  decoder, because they now carry Link control payloads.
- **It does not cut Link major 1.** That is `V1_SCOPE.md` §5.8, and it happens
  after the meanings close.
- **It does not settle `frame_check`.** The per-profile `frame_check`
  requirement still has nowhere normative to live until §5.6 freezes the two
  transport profiles. That is a profile gap, not a class gap, and it is tracked
  there.
