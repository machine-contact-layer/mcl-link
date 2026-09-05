# MCL Link HANDOFF Control Payload v0.1

**Status:** **Stable** for the migration control sequence -- offer, accept, challenge, response, commit, confirm -- and its idempotence and retransmission rules. Promoted 2026-09-06 under `mcl-core/governance/V1_SCOPE.md` section 6 on E4 over-air evidence: 104 migrations with both radios live on both peers.
**Layer:** Link
**Frame class:** `HANDOFF` (8)
**Control version:** 0
**Registry:** `mcl-link/registries/handoff-ops-v0.1.json`
**Reference implementation:** `mcl-link/include/mcl/handoff.h`, `mcl-link/src/handoff.c`

This document is normative for the bytes. The reference implementation is
subordinate to it: where they disagree, this document is correct and the code is
a defect.

---

## 1. What this closes

`mcl-link/spec/link-v0.md` §8 says a session may negotiate another transport,
and `mcl/contact.h` describes the sequence that does it:

```
OLD TRANSPORT     TRANSPORT_OFFER   ->
                  TRANSPORT_ACCEPT  <-

CANDIDATE         PATH_CHALLENGE    ->
                  PATH_RESPONSE     <-

NEW TRANSPORT     COMMIT            ->
                  CONFIRM           <-
```

The first pair are Wire semantic objects with canonical bytes and published
conformance vectors. The second and third pairs had no encoding at all. They
existed as local function calls, which means two implementations written from
the specification could agree on the offer, agree on the acceptance, and then be
unable to exchange one further byte of the migration.

A hardware demonstration does not close this gap. A harness that calls
`mcl_contact_*` on both machines proves the radios carry frames; it does not
prove the migration is specified. **Direct in-process calls to the contact API
are not on-wire migration and must never be recorded as such.**

## 2. Layer ownership

The four controls belong to Link, not Core.

A Wire semantic object is something one machine *means* to another. It survives
relay, storage and re-encoding under a different context, and means the same
thing wherever it arrives. A handoff control means nothing outside the
particular Link that is migrating: it describes that Link's own change of
transport and is meaningless to a third party or to the same peers a minute
later. Encoding it as a semantic object would place a purely local negotiation
into the vocabulary every MCL implementation must agree on permanently, which
the governing rule forbids — freeze only what future implementers must agree
on, at the layer that owns it.

`TRANSPORT_OFFER` and `TRANSPORT_ACCEPT` stay in Wire because they *are*
meaning: one machine proposes a way to keep talking, and the other agrees.

## 3. Carriage

A control is carried as the **entire payload** of one Link frame whose
`frame_class` is `HANDOFF` (8).

- No other frame class may carry a handoff control. A receiver that finds one in
  another class MUST reject the frame rather than interpret it: the class is
  what tells a receiver which registry the payload's first bytes belong to, and
  a payload that is interpreted under two classes is a payload with two
  meanings.
- A `HANDOFF` frame MUST NOT carry anything other than a control.
- The frame's `payload_len` delimits the control exactly. There is no length
  field inside the control and no padding.
- The frame **MUST** set `MCL_LINK_FLAG_SESSION`, and its `session_ref` MUST
  equal the control's. A receiver MUST reject a `HANDOFF` frame that does not
  set it, and MUST reject one whose two session references disagree.

  This was a `SHOULD` in the first draft of this document, checked only when
  the flag happened to be present. That made the field a multi-contact receiver
  depends on for ROUTING optional. These controls arrive on the candidate
  transport, which the contact has not been using, so a machine holding several
  contacts must decide which one a freshly arrived frame belongs to *before* it
  can parse a class-specific payload — otherwise generic frame routing has to
  reach into every class's payload format, and every new class becomes a change
  to the router. Requiring it costs four bytes on frames of 10 or 18 payload
  bytes, on a transport just chosen for being better than the one where bytes
  were scarce.

  It is still not a substitute for the control's own `session_ref`: a frame's
  optional fields are selected by flags, and a control must remain
  interpretable from its own bytes.

### 3.1 The transport a control is admissible on

> A post-acceptance control MUST be transmitted on, and MUST have arrived on,
> the transport the current transaction is being conducted over: the
> **candidate** while a migration is in progress, the **active** transport
> otherwise. A control arriving on any other transport MUST be refused.

The first half is the point of validation — the sequence exists to prove the
candidate works, and a control sent on the old path proves nothing about the
new one.

The second half is the half that is easy to omit and expensive to omit. A
`PATH_RESPONSE` delivered over the **old** path, naming the right transaction
and the right session and echoing the right challenge, is correct in every
field. Nothing else in the sequence can detect it. Accepting it would declare a
candidate reachable on the strength of bytes that never crossed it, which is the
entire content of what path validation establishes. An implementation whose
receive path does not know which transport bytes arrived on **cannot perform
this check at all**, which is why it is stated as a carriage requirement rather
than left to integration.

The "active transport otherwise" clause covers the peer that has already
completed the move: a `COMMIT` retransmitted after a lost `CONFIRM` arrives
where that peer now lives, which is the transport it migrated to.

## 4. Canonical layout

Network byte order. All fields are fixed-width and unaligned reads are not
required.

```
offset  size  field
------  ----  -----------------------------------------------
     0     1  control_version    = 0
     1     1  operation          registry value
     2     4  migration_ref      non-zero
     6     4  session_ref        non-zero
    10     8  challenge          PATH_CHALLENGE / PATH_RESPONSE only
```

| Operation | Value | Total size |
|---|---|---|
| `PATH_CHALLENGE` | 1 | 18 |
| `PATH_RESPONSE` | 2 | 18 |
| `COMMIT` | 3 | 10 |
| `CONFIRM` | 4 | 10 |

The operation determines the length exactly. A control whose length disagrees
with its operation is malformed.

### 4.1 Why both references appear on every control

`migration_ref` identifies the transaction, so `session_ref` may look redundant.
It is not.

These controls arrive on a transport the contact has **not been using**. Before
a receiver can check the transaction, it must decide which of possibly several
contacts a freshly arrived frame belongs to. `session_ref` answers *which
contact*; `migration_ref` answers *which transport change of that contact*.
Carrying only the transaction reference would force a receiver to search its
contacts by transaction, which is precisely the cross-contact ambiguity that
lets one contact's transaction be applied to another.

Both are zero-forbidden, so a zeroed buffer can never decode as a valid control.

### 4.2 What the references are not

`migration_ref`, `session_ref` and the challenge all cross an observable medium
in the clear. Anyone within range can read them and quote them back. A peer that
completes this entire sequence has demonstrated **reachability on the candidate
path and nothing else**. It has not demonstrated that it is the machine the
contact began with.

The challenge should be unpredictable so that guessing it is harder than
receiving the frame that contains it. That is race hardening, not a security
property.

## 5. Decoding rules

A decoder MUST apply these in order and MUST NOT interpret a control that fails
any of them.

1. Fewer than 10 bytes → **truncated**. The operation is not yet readable, so
   the required length is not yet knowable.
2. `control_version` ≠ 0 → **incompatible version**. Checked before the
   operation, so that a future control version is reported as a version
   mismatch rather than as an unknown operation. That distinction is what lets a
   peer report "I am too old" instead of "you are malformed".
3. `operation` not assigned in the registry → **rejected**. Every handoff
   operation is critical (§6).
4. Length ≠ the length the operation requires → **truncated** if short,
   **rejected** if long. Trailing bytes are a malformation, not a framing
   question: the frame already declared its payload length, so a control that
   does not fill it means the two ends disagree about the format.
5. `migration_ref` = 0 or `session_ref` = 0 → **rejected**.

The reference implementation maps these to `MCL_LINK_ERR_TRUNCATED`,
`MCL_LINK_ERR_INCOMPATIBLE_VERSION` and `MCL_LINK_ERR_RANGE`. The distinction
between truncation and malformation is the same one the Link frame decoder
makes, and for the same reason: a stream carriage must be able to tell "more
bytes may complete this" from "these bytes can never be valid".

A control that decodes is not thereby accepted. It is then checked against the
contact state (§7), which is a separate step and rejects for separate reasons.

## 6. Criticality and extension

**Every handoff operation is critical.** An implementation that does not
recognise an operation MUST reject the control rather than skip it. The assigned
operations are exactly the steps that move the state machine; skipping one would
mean continuing a migration whose steps were not performed.

`control_version` changes only if the fixed header changes shape. A new
operation is added by assigning a new registry value, not by incrementing the
version.

Operation ranges: `0` reserved permanently; `1–4` assigned; `5–191`
Specification Required; `192–255` Experimental Use. An implementation not party
to an experiment rejects an Experimental operation exactly as it rejects an
unassigned one, which is the correct outcome and not a failure.

## 7. State rules

| Operation | Accepted in | Result |
|---|---|---|
| `PATH_CHALLENGE` | `AGREED` | → `VALIDATING`, challenge recorded |
| `PATH_CHALLENGE` | `VALIDATED` | duplicate — re-send `PATH_RESPONSE`, no change |
| `PATH_RESPONSE` | `VALIDATING` | → `VALIDATED` if refs and echo match exactly |
| `PATH_RESPONSE` | `VALIDATED` | duplicate — no change |
| `COMMIT` | `VALIDATED` | → `ACTIVE` on the candidate directly; reply `CONFIRM` |
| `COMMIT` | `ACTIVE` | duplicate — re-send `CONFIRM`, no change; see §8 |
| `CONFIRM` | `COMMITTING` | → `ACTIVE` on the candidate |
| `CONFIRM` | `ACTIVE` | duplicate — no change |

**Every operation has a duplicate row, and that is a requirement rather than a
convenience.** On a lossy medium the only repair available is retransmission,
so each control must be answerable a second time with the same outcome. An
implementation that handles only the first copy of each works perfectly in a
harness and strands a contact the first time a radio drops a frame.

The duplicate rules, precisely:

> A repeat of a control naming the SAME transaction and carrying the SAME
> content MUST succeed and MUST change nothing. A repeat naming the same
> transaction with DIFFERENT content MUST be refused.

The second half matters as much as the first. A retransmitted `PATH_CHALLENGE`
that carries a *different* challenge under a transaction already validated is
not a retransmission — an honest one repeats itself — and echoing it would mean
returning bytes of a third party's choosing on a path this machine has already
committed to probing. It is answered with silence.

`PATH_CHALLENGE` in `VALIDATED` is the row that was missing from the first
draft, and it is the one a radio would have found first:

```
    A -> B   PATH_CHALLENGE
             B echoes, reaching VALIDATED
    B -> A   PATH_RESPONSE          LOST
    A -> B   PATH_CHALLENGE         retransmission, correct behaviour
             B is no longer in AGREED -> refused
```

One dropped frame, no adversary, both peers behaving correctly, and the
migration is dead. The repair is the same as for the lost `CONFIRM` in §8:
remember enough to give the same answer again.

A duplicate acceptance deserves particular care and is covered by the contact
state machine rather than by a control: it MUST NOT return a contact from
`VALIDATING`, `VALIDATED` or `COMMITTING` to `AGREED`. A delayed copy of a step
already completed would otherwise undo the progress made after it, and on a
medium that reorders, a delayed copy is ordinary.

**`COMMITTING` belongs to the sender of `COMMIT` alone.** A peer that receives
`COMMIT` moves from `VALIDATED` to `ACTIVE` in one step and never occupies it.
The state therefore means exactly one thing — *I sent a commit and do not know
whether it arrived* — which is what makes the rule in §8.1 enforceable. A design
in which both peers passed through `COMMITTING` cannot distinguish the peer that
may safely roll back from the peer that may not, because they look identical.

A control received in any other state MUST be refused without changing state.
Specifically:

- `COMMIT` before `VALIDATED` MUST be refused. Committing an unvalidated path is
  the defect the state machine exists to prevent.
- `CONFIRM` before `COMMITTING` MUST be refused.
- A `PATH_RESPONSE` whose echo, `migration_ref` or `session_ref` does not match
  MUST be refused, and MUST leave the contact in `VALIDATING` rather than
  failing the migration — a wrong response is one wrong frame on a shared
  medium, not proof that the path is bad.
- A control carrying a `migration_ref` from an abandoned transaction MUST be
  refused. This is the case `migration_ref` exists for: without it, a delayed
  control from an abandoned attempt is indistinguishable from the live one,
  because transport and profile normally repeat across a retry.

**No control changes link state on reception alone.** Receiving a handoff
control grants no identity, authenticity, authority, trust or authorization, and
a refused or malformed control MUST NOT damage the old working path. Charter
§2.3 and §2.11.

## 8. Retransmission, and the lost CONFIRM

Consider A sending `COMMIT`, B accepting it, completing the migration, and
replying `CONFIRM` — and the `CONFIRM` is lost.

B is on the new transport. A is still `COMMITTING`. **The two machines disagree
about which transport carries the contact, and no adversary is involved: one
dropped frame is enough.**

An abort operation does not fix this, because the peers are no longer on a
common transport to abort over. Retransmission does.

### 8.1 A sent commit is irrevocable

> Once `COMMIT` has been transmitted, a peer MUST NOT unilaterally abandon the
> migration and return to the old transport.

An earlier revision of this design allowed exactly that, and described the
sender as *eventually giving up and returning to the old transport* — in the
same document that explained the divergence doing so produces. That was a
contradiction rather than a policy, and it is resolved here.

The reason is that abandoning encodes an inference the sender cannot make:

```
    "I did not hear CONFIRM,  therefore  the peer did not commit."
```

On an unreliable channel that is simply false. The absence of an
acknowledgement is evidence about the channel, not about the peer. This is the
ordinary uncertainty of distributed commit — no finite exchange lets the sender
of the last message learn that it arrived — and MCL resolves it the way
protocols that must work in the field do: the decision becomes irrevocable when
it is transmitted, and is retried until confirmed.

From `COMMITTING` there are exactly two admissible outcomes:

1. **Retransmit `COMMIT`** until `CONFIRM` arrives. §8 makes repeated commits
   idempotent, so this is safe however many times it takes.
2. **Declare the contact lost.** Not the migration — the contact. That is
   honest, and it is what a peer does when a candidate path has genuinely died.

The old transport MAY remain physically open throughout, and the caller MAY
keep using it for its own traffic. What is forbidden is *declaring the
migration failed* while the peer may already have completed it.

Before `COMMIT` is sent — in `OFFERED`, `AGREED`, `VALIDATING` and `VALIDATED` —
abandonment is unconditionally safe, because the peer cannot have switched
without having received a commit. The reference implementation enforces the
split in `mcl_contact_abandon_migration`, which refuses from `COMMITTING`.

> A peer that has completed a migration MUST answer a `COMMIT` that matches that
> completed transaction with `CONFIRM` again, and MUST NOT change state when it
> does.

This requires remembering the reference of the most recently completed
transaction after the pending one is cleared. TCP and QUIC both keep exactly
this kind of short memory, for exactly this reason. The reference implementation
exposes it as `mcl_contact_commit_repeat`, which is `const` because
re-confirming must not re-run anything: a repeated `COMMIT` must never become a
way to make a settled contact move again.

A replayed `COMMIT` from a listener is answered the same way, and correctly so:
the answer restates a transport change that already happened and reveals nothing
the listener did not already hear.

Retransmission of `PATH_CHALLENGE` and `PATH_RESPONSE` needs no such memory,
because both are re-sendable from the state they are already in.

### 8.2 When exactly a commit becomes irrevocable

The rule in §8.1 is stated in terms of transmission, and transmission has an
edge that an implementation has to get right:

> A commit becomes irrevocable when the frame **may** have been transmitted, not
> when it is known to have been. A sender MUST enter `COMMITTING` if its
> transport reports success OR cannot report the outcome, and MUST NOT enter it
> if the transport reports definitely that nothing was sent.

Both halves matter and they fail in opposite directions.

If an uncertain outcome did not commit, a sender could roll back over a frame
the peer actually received — the split §8.1 exists to prevent, reached by a
different route.

If a definite failure did commit, a contact would be stranded irrevocably in
`COMMITTING` over a frame that never left the machine: unable to roll back, and
with nothing on the other side to confirm it.

This has a consequence for the API shape, which is recorded because it is easy
to get wrong: the state transition belongs to the **transmit operation**, not to
a separate call the caller makes beforehand. An interface where the caller marks
the contact committing and then asks the transport to send has already made the
decision irrevocable before it knows whether there was anything to be
irrevocable about.

A transport that genuinely cannot distinguish the two MUST report uncertainty.
Claiming certainty it does not have is the failure this rule exists to prevent,
and retransmission is always safe: §8 makes every one of these controls
idempotent.

### 8.3 Ordinary traffic during the cutover

Between the transmission of `COMMIT` and the arrival of `CONFIRM` the two peers
genuinely disagree about which transport carries the contact. The receiver of a
`COMMIT` is on the new transport immediately; the sender cannot know whether its
`COMMIT` arrived. The asymmetry is inherent, not a defect.

> From the transmission of `COMMIT` until `CONFIRM`, ordinary non-handoff
> traffic for that contact MUST be quiesced, unless a profile explicitly defines
> duplicate-safe transition behaviour. No profile does yet.

Without this, a sender in `COMMITTING` continues to use `active_transport` and
puts ordinary traffic onto a medium the peer may already have left — losing
semantic operations while believing they were delivered.

Quiescing rather than duplicating is the base rule because duplicating onto both
media would deliver some operations twice, and nothing at this layer knows which
operations are safe to repeat. That judgement belongs to the deployment (charter
§2.10.1). The window is bounded by the `CONFIRM` exchange, which is the shortest
it can be made without the sender guessing.

Handoff controls are explicitly unaffected: completing that exchange is what
ends the window.

## 9. Why there is no ABORT

Deliberately not assigned.

Negative outcomes in MCL are already expressed by absence: an offer that is not
accepted simply is not accepted, and `mcl_contact_record_offer` carries a
`validity` the caller enforces, because this library has no clock and must not
have one. An abort would add a second and faster path to a state the timeout
already reaches — and one that an observer of the references could send in order
to cancel a migration, turning a dropped frame into a cancelled one.

The cost is convergence latency on a failed migration, paid by the machine that
was already failing to migrate. If a deployment demonstrates that this latency
matters, an abort can be assigned an operation value later without a version
change (§6).

## 10. Conformance

Vectors: `mcl-link/conformance/vectors/handoff-v0.1.json`, mirrored as byte
arrays in `mcl-link/tests/test_handoff.c`.

Positive: one vector per operation.

Negative, all required to be refused: truncated header; truncated challenge;
trailing byte; reserved operation 0; unassigned operation 5; Experimental
operation 192; wrong control version; zero `migration_ref`; zero `session_ref`;
a `COMMIT` sized as though it carried a challenge; a `PATH_CHALLENGE` sized as
though it did not.

State-level negatives are in `test_contact.c`: stale `migration_ref`, wrong
`session_ref`, wrong challenge echo, `COMMIT` before `VALIDATED`, `CONFIRM`
before `COMMITTING`, and duplicate `COMMIT` after completion.

Once published, a vector file is never edited. A legitimate byte change gets a
new version.

## 11. Status

This is a research draft at Link major 0. It has passed no independent
implementation and has crossed no radio. Software conformance and physical
evidence advance separately: see `mcl-core` conformance classes C0–C6 and the
per-transport evidence ladder.
