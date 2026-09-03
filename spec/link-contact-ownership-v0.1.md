# Link Lifecycle and Contact Continuity: Ownership v0.1

**Status:** Research Draft
**Layer:** Link, with a requirement on the SDK
**Applies to:** Link major 0, and the contract that must be settled before a stable Link major

This document decides which of two state machines owns what, because until now
neither did and both were independently mutable.

---

## 1. The problem

An MCL node holds two state machines:

```
mcl_link_t      the protocol lifecycle
                IDLE -> DISCOVERED -> CAPABILITIES -> NEGOTIATING ->
                ESTABLISHED -> {ADAPTING, HANDOFF, FALLBACK, CLOSED}

mcl_contact_t   transport continuity
                ACTIVE -> OFFERED -> AGREED -> VALIDATING -> VALIDATED ->
                COMMITTING -> ACTIVE(new)
```

They could disagree from the instant a node was initialised. `mcl_link_init`
starts a link `IDLE`; `mcl_contact_begin` starts a contact `ACTIVE`. A third
function, `mcl_contact_link_state()`, mapped `ACTIVE` to `ESTABLISHED` — so a
freshly initialised node held a link saying `IDLE`, a contact saying `ACTIVE`,
and a mapping function asserting that the link "ought to be" `ESTABLISHED`.

Worse, the two drifted with use. The SDK's handoff operations mutated the
contact and left the link alone, while a separate `mcl_node_link_transition()`
moved the link with no reference to the contact.

Three sources of truth for two facts.

## 2. The decision

**Neither machine owns the other. They answer different questions, and neither
answer implies the other. The SDK owns the single place they cross.**

```
mcl_link_t      "have we discovered a peer, exchanged capabilities,
                 negotiated, established?"

mcl_contact_t   "which medium carries this contact, and is a change of
                 medium under way?"
```

A machine can be settled on a transport having negotiated nothing — that is
every node before its first contact. A machine can be mid-negotiation with no
migration in sight — that is every node during first contact. Neither state
tells you anything about the other.

### 2.1 What was removed

`mcl_contact_link_state()` is **deleted**.

A function reporting what one state machine *ought* to look like, next to a
second machine that is independently mutable, is a third source of truth that
drifts from both. It was introduced to stop drift and instead gave the drift
somewhere to hide: whichever of the three a reader consulted, another disagreed.

It is replaced by `mcl_contact_migration_active()`, which reports a fact about
the contact and makes no claim about the link.

### 2.2 What `MCL_CONTACT_STATE_ACTIVE` means

Restated, because the old wording ("live on active_transport") invited the
reading that a contact in this state was an established session:

> `ACTIVE` means **settled on `active_transport` with no migration in progress**.
> It asserts nothing about negotiation, capabilities, identity, authenticity,
> authority or trust. It is true from the moment a contact is begun, because a
> machine is always on some medium and is not always changing it.

Under that definition a node whose link is `IDLE` and whose contact is `ACTIVE`
is not a contradiction. It is a machine that is on a medium and has not yet met
anyone.

## 3. The one invariant, and who enforces it

> A contact migration may be **driven** only while the link lifecycle is
> `ESTABLISHED` or `HANDOFF`, and the link MUST NOT leave those states while a
> migration transaction is outstanding.

The SDK enforces both halves:

- `mcl_node_send_handoff` and `mcl_node_apply_handoff` refuse to act outside
  those two states.
- `mcl_node_link_transition` refuses to leave them with a transaction
  outstanding.

The second half is not symmetry for its own sake. Leaving those states
mid-transaction would abandon the migration by a route that does not pass
through `mcl_contact_abandon_migration()` — and from `COMMITTING` that would
discard a commit the peer may already have acted on, which §8.1 of
[link-handoff-control-v0.1.md](link-handoff-control-v0.1.md) forbids. A rule
enforced at one door and not the other is not enforced. The caller closes the
contact, or finishes the migration.

### 3.1 What is deliberately NOT gated

Sending and receiving ordinary frames is not gated on the link lifecycle.

First contact necessarily happens before establishment. A layer whose first
frame required an established session could never send a first frame, and MCL's
entire premise is that two machines with no prior relationship can begin.

## 4. Consequence: the transport boundary

A contact spans two media during a migration, and one untagged transmit path
cannot express that:

```
TRANSPORT_OFFER / ACCEPT        old transport
rendezvous                      candidate transport
PATH_CHALLENGE / PATH_RESPONSE  candidate transport
COMMIT / CONFIRM                candidate transport
ordinary traffic                depends on cutover state, and is quiesced
                                between COMMIT and CONFIRM
```

Therefore:

> An SDK transmit callback MUST be told which transport the bytes are for, and
> an SDK receive path MUST be told which transport bytes arrived on.

The egress half is expressiveness: without it an integrator must infer the
bearer from the *order of calls*, which is to say the requirement is documented
and then made the caller's problem to guess.

The ingress half is correctness, and it is the one that cannot be worked around.
A `PATH_RESPONSE` fed in from the old path validates a candidate that has never
carried a byte. Every reference in that frame is correct, so no other check in
the sequence can detect it. **An implementation whose receive path does not know
the arrival transport cannot perform the one check path validation depends on.**

The transport for each is derived from the contact, not chosen by the caller:

| | function | result |
|---|---|---|
| handoff controls | `mcl_contact_control_transport` | candidate while migrating, else active |
| ordinary traffic | `mcl_contact_data_transport` | active, plus a quiesced flag |

A handoff control sent on the wrong medium proves nothing about the medium it
claims to be establishing, so the caller does not get to override it.

## 5. Status

Decided here: the ownership split, the removal of the mapping function, the
meaning of `ACTIVE`, the crossing invariant and its enforcement, and the
transport-aware SDK boundary.

Still open:

- Multi-contact nodes. One `mcl_node_t` holds one contact; a machine needing
  several instantiates several nodes. A caller-owned contact pool with no hidden
  allocation is the intended shape and is not built.
- The link lifecycle's own transitions are not yet driven by anything on the
  wire — `CAPABILITY` and `NEGOTIATION` have no control payloads yet, so the
  caller walks the lifecycle by hand. When those exist, the lifecycle becomes
  driven and this document's invariant is where they connect.

No part of this has crossed a radio.
