# Where security messages legally travel

Status: **Research Draft.** Decides no cryptography and selects no suite. It
answers the one structural question that must be settled *before* a crypto
provider interface is designed, because building callbacks first would fix the
interface around whatever carrier turned out to be convenient.

## 1. The question

`mcl-core/research/TWO_BUILDER_AUDIT.md` §3.6 found the chain missing its middle
term:

```text
security protocol   ->   ???   ->   crypto provider
```

`sign()` and `verify()` are the far end. The near end is a named security
profile. Between them is the question nothing in MCL answers: **when two peers
run an authenticated exchange, which bytes on which frames carry it?**

## 2. What the tree actually constrains

Facts, read rather than assumed:

| Constraint | Where |
|---|---|
| `frame_class` is the **low nibble** of the first byte — 4 bits, 16 values | `spec/link-v0.md` §3.1 |
| Values 0–9 are assigned; **10–15 are unassigned** | `spec/link-class-disposition-v1.md` §3 |
| `ADAPT` = 7 is **Reserved**: MUST be refused, MUST NOT be emitted, never reassigned | same, §4 |
| A major-1 decoder refuses an unknown class | same, §2 |
| `CONTACT` and `DATA` carry a canonical Wire Tier-0 object and nothing else | same, §3 |
| `CAPABILITY` and `NEGOTIATION` are fixed 9- and 7-byte controls | `link-negotiation-v1.md` |
| `features` is a `uint16` bitmask, **zero bits assigned**, unknown bits fail closed by `AND` | same, §6 |
| Link payload ceiling is 1024, frame ceiling 1048 | `include/mcl/link.h` |
| Wire major 1 carries exactly three Tier-0 kinds | `V1_SCOPE.md` §3.2 |
| The Wire extension envelope is Stable with an empty assignment table; blocks cap at 256 bytes | `mcl-wire/include/mcl/extension.h` |
| Security state is held separately from Link state | `ARCHITECTURE_CHARTER.md` |

## 3. The candidates

### 3.1 A new Tier-0 object kind — rejected

Add `SECURITY_MESSAGE` to Wire and carry it in `DATA`.

**Rejected on two independent grounds.** Wire major 1 admits three kinds and
refuses everything else, so a fourth is a Wire major 2 — the most expensive
change available. And it is wrong even if it were free: a security handshake is
not a statement about the physical world, and putting it in the semantic
registry makes every future reader of that registry ask why authentication is a
kind of announcement.

### 3.2 A Wire extension block on a Tier-0 object — rejected

Ride the Stable extension envelope, which is the one mechanism that can add
bytes inside major 1 without a version change.

**It would work and it is still wrong.** It attaches security state to a
semantic object, so a `PRESENCE` becomes the thing that carries half a
handshake. That is the orthogonality violation the charter exists to prevent,
and it would force every implementation to look for security state in a place
that has nothing to do with security. The 256-byte block cap is a secondary
problem; the coupling is the disqualifying one.

### 3.3 Reusing `ADAPT` (7) — forbidden

**Not available, and the reasoning is already written down.** §4 of the
disposition specification makes 7 a permanent tombstone precisely so it is never
reassigned: a peer that once meant "adapt" by 7 would be misread rather than
refused. Reusing it would also break exactly the implementations that follow the
specification, since they MUST refuse it.

### 3.4 Outside MCL entirely — the status quo, and insufficient

Put MCL inside DTLS, or inside BLE Secure Connections. This is what `SECURITY.md`
recommends today and it remains legitimate for a deployment.

It is not sufficient as the *only* answer, because it is precisely what makes
two builders non-interoperable: Builder A choosing DTLS with P-256 and Builder B
choosing something else have both "secured MCL" and still cannot authenticate
each other. That is the finding that opened row 35.

### 3.5 A new Link class, gated by a negotiated feature bit — recommended

Assign **class 10, `SECURITY`**, and make it legal between two peers **only when
both have advertised a feature bit for it** in `CAPABILITY`.

#### First: this was not obviously legal, and the first draft of this section assumed it was

`spec/link-class-disposition-v1.md` §5 stated a normative rule of Link major 1:

```text
frame_class > 9   ->   reject, MCL_LINK_ERR_RANGE
```

Read literally, that closes the namespace and makes class 10 illegal at major 1
outright — so the arithmetic argument below, however sound, would have been
building on a rule it contradicted. **Justifying an addition by how neatly it
fails closed is not the same as establishing that governance permits it**, and
the two were conflated here before this paragraph existed.

The reconciliation is now written down rather than assumed. `ARCHITECTURE_CHARTER.md`
§4 states that *"new assigned values, errata, and compatible optional behavior
do not consume a major wire version"*, and that it is *incompatible
reinterpretation of existing canonical bytes* that requires a new major.
Assigning a never-used value is the former, not the latter.

So §5's phrasing described today's assignments and closed the namespace as a
side effect. It has been restated as "unassigned, or assigned but not
negotiated → reject" — **identical behaviour for every implementation that
exists** — and `spec/link-class-disposition-v1.md` §6 now records the four
conditions a new class must meet. `registries/frame-classes-v0.1.json` is
created to hold the namespace, because it did not exist while values 0–9 were
being assigned.

That the disposition specification is still `proposed` rather than Stable is
what made this cheap. After promotion it would have been an erratum against a
frozen document.

**No value is assigned.** Six remain in a namespace that can never grow, and
spending one on a mechanism that might still change shape is how registries
acquire tombstones.

#### Why the mechanism is right, given that it is permitted

This is the mechanism the project already built and deliberately left empty.
`link-negotiation-v1.md` §6 assigns zero feature bits and states that the field
has defined behaviour for every possible value on day one, because the selection
is `L.features & P.features` — a bit a peer does not know is a bit it did not
set, so the `AND` clears it.

The consequences fall out arithmetically rather than needing rules:

- A v1.0 implementation sets no bits, so the `AND` yields zero, so a
  security-capable peer **must not** send class 10 to it. No v1.0 decoder ever
  sees a class it would refuse.
- A peer that receives class 10 without having negotiated it refuses it as an
  unknown class. That is existing major-1 behaviour, unchanged.
- Nothing about major 1 is reinterpreted for anyone who did not opt in, so no
  frame that means one thing today comes to mean another.

The payload is a profile identifier and opaque message bytes:

```text
u8    security_profile_id     assigned in a registry; 0 reserved
u8[]  message                 opaque to Link
```

Link neither parses nor understands `message`. It is a carrier, which is the
whole point: the same frame carries whatever the named profile defines, so AP
pronounces those bytes, BLE fragments them and IP puts them in a datagram
without any of them knowing what they are.

**Security state is held separately from Link state.** The `SECURITY` class
moves bytes; it does not add a lifecycle state, and the link lifecycle does not
gate on the handshake's progress. This preserves the separation the charter
requires and keeps the two state machines from becoming a third source of truth
about each other — the mistake already made and removed once, when
`mcl_contact_link_state()` claimed a mapping between the lifecycle and contact
continuity.

## 4. What this places on `MCL-S1`, before any suite is chosen

### 4.1 It must bind the negotiation transcript

`link-negotiation-v1.md` §7 states plainly that MCL has **no downgrade
protection**: negotiation is unauthenticated, so an active attacker on an open
medium can clear the security feature bit and both peers will honestly negotiate
down to no security at all.

**That cannot be fixed at the negotiation layer**, because fixing it there needs
authentication, which is what the negotiation is trying to reach. It has to be
fixed *inside* the security profile, by binding the `CAPABILITY` and
`NEGOTIATION` exchange into the handshake transcript so that a tampered
negotiation produces a failed authentication rather than a silent downgrade.

This is a hard requirement on suite selection, not a preference. A suite with no
transcript-binding facility cannot be `MCL-S1`.

### 4.2 It must fit, or say how it segments

One `SECURITY` frame carries at most 1024 payload bytes, minus the profile
identifier. That is comfortable for compact handshakes and **not** comfortable
for large credentials: a 2420-byte ML-DSA-44 signature does not fit in one Link
frame on any bearer.

So a suite either fits in single frames, or `MCL-S1` defines segmentation above
Link. The audit already recommends the better path — credential *references*
and compact exchanges rather than transporting credentials whole.

### 4.3 It must not assume the acoustic path

`AP-BOOTSTRAP-1` carries three Tier-0 kinds and no Link frames at all, so it
carries no `SECURITY` class by construction. Authentication happens after
migration, on the richer bearer. That is the intended architecture and this
carrier does not disturb it.

### 4.4 It must bind the contact, not the connection

This is the requirement most likely to be got wrong, because the obvious thing
to bind is the thing in front of you.

`MCL-S1` must establish that **the peer speaking on the richer bearer is the
same cryptographic peer with which this MCL contact is being secured.** If the
security context is bound to an IP address, a UDP port, a BLE address or an
acoustic carrier, then migration — the property that makes MCL a contact layer
rather than a message format — breaks the security model the moment it is
exercised. A contact that survives a change of medium cannot rest on a context
that does not.

So the external context is drawn from the transport-independent contact:
protocol identifier, Wire major, Link major, security profile identifier, both
contact correlation references, `session_ref` where established, the negotiated
capability context, and the transcript role. It excludes every address,
endpoint and bearer parameter that a migration is allowed to change.

**Which fields exactly is not settled here.** They must be derived from the
contact model in `spec/link-contact-ownership-v0.1.md` and from an attack
analysis against `research/secure-contact-threat-model.md` — not copied from a
sketch. What is settled is the rule the derivation has to satisfy: *nothing that
migration may change may appear in the context.*

### 4.5 It must not add states to the link lifecycle

The temptation is a ladder:

```text
ESTABLISHED -> AUTHENTICATED -> TRUSTED -> AUTHORIZED
```

`ARCHITECTURE_CHARTER.md` forbids exactly this collapse, and `mcl-core/README.md`
states the separation as `reception != identity != authenticity != authority !=
trust != obligation`. Three independent state machines, and they stay
independent:

```text
communication state    the Link lifecycle, unchanged by this design
security state         NONE / IN_PROGRESS / PROPERTIES_ESTABLISHED
local authorisation    the deployment's policy, never MCL's
```

"Properties established" is deliberately not "trusted". A completed handshake
proves a cryptographic property. Whether that property *means* anything is a
trust question the deployment answers with its own anchors, and whether it
*permits* anything is an authorisation question the machine answers with its own
policy. `MCL-S1` may deliver the first and must not claim the other two.

## 5. What is not decided here

- **No suite.** EDHOC, COSE and the alternatives are benchmarked separately, on
  message size, code size, credential-reference support, replay and downgrade
  properties. No primitive is invented.
- **No feature-bit value and no class assignment.** Both are Standards Action
  and neither is spent before a suite exists. Spending a value on a mechanism
  that might change shape is how registries acquire tombstones.
- **No credential format, trust-anchor interface or authorisation semantics.**
  Those are a deployment's, and `mcl-core/spec/deployment-profile-v1.md` is where
  a deployment will name them.
- **No claim that this is secure.** It is a carrier. Carrying handshake bytes
  correctly is necessary and nowhere near sufficient, and nothing in this
  document should be read as the latter.
