# MCL Link minimum capability and version negotiation v1

Status: **Candidate**
Satisfies: `mcl-core/governance/V1_SCOPE.md` §5.4
Frame classes: `CAPABILITY` (1) and `NEGOTIATION` (2)

## 1. What this negotiates, and what it deliberately does not

Two independent implementations cannot interoperate without agreeing which Wire
major, which Link major and what maximum frame size they will use. Nothing in
MCL currently lets them say so. This is that mechanism, and it is intentionally
the smallest one that closes the gap.

**Negotiated here:**

| Item | Why it has no other home |
|---|---|
| Wire major | The object layouts depend on it. `§4.7` of the v1 scope makes major 1 carry only Stable semantics, so which major is in use decides what may be sent. |
| Link major | Frame layout and class contracts depend on it. |
| Maximum frame size | A binding whose reassembly limit is below the peer's largest legal frame fails only under load. |
| Feature bits | The extension point. Zero bits are assigned in v1; see §6. |

**Deliberately NOT negotiated here:**

- **Transport and profile selection.** `TRANSPORT_OFFER` / `TRANSPORT_ACCEPT`
  already carry `transport_id` and `profile_id`, and migration is proven over
  real radios. Adding a second way to select a transport would create two
  vocabularies for one concept, which `REGISTRY_POLICY.md` directs reviewers to
  reject. The profile of the *active* transport is implicit — two peers that are
  exchanging frames already agree on it, or they could not be exchanging frames.
- **Context compression.** Deferred by `V1_SCOPE.md` §3.3. The
  `CONTEXT_OFFER`/`CONTEXT_ACCEPT` research draft is **not** the basis for this
  document: it negotiates a codec that does not exist. No `context_id`, no
  `generation`, no `ruleset_digest`.
- **Anything cryptographic.** No confidentiality, no peer authentication, no
  downgrade protection. See §7.

## 2. The two controls

Both are exact lengths. A control that does not fill its frame's `payload_len`
exactly is refused, as every other Link control is.

### 2.1 CAPABILITY — "what I support"

```
offset  size  field
------  ----  --------------------------------------------------
     0     1  control_version = 0
     1     2  wire_majors      bitmap, bit N set = Wire major N supported
     3     2  link_majors      bitmap, bit N set = Link major N supported
     5     2  max_frame        largest Link frame this node will ACCEPT
     7     2  features         feature bitmap
                               total 9 bytes
```

Major version fields are nibbles on the wire (0..15), so a 16-bit bitmap covers
the entire space exactly. There is no escape value and none is needed.

`max_frame` is what the sender will **accept**, not what it intends to send.
Advertising a receive limit is the only direction that is checkable by the peer
that must respect it.

### 2.2 NEGOTIATION — "what we will use"

```
offset  size  field
------  ----  --------------------------------------------------
     0     1  control_version = 0
     1     1  wire_major       the selected value, not a bitmap
     2     1  link_major       the selected value, not a bitmap
     3     2  max_frame        the selected value
     5     2  features         the selected set
                               total 7 bytes
```

## 3. The selection function

Given the local capability `L` and the peer capability `P`:

```text
wire_major = highest bit index set in (L.wire_majors & P.wire_majors)
link_major = highest bit index set in (L.link_majors & P.link_majors)
max_frame  = min(L.max_frame, P.max_frame)
features   = L.features & P.features

if (L.wire_majors & P.wire_majors) == 0  -> no common Wire major, refuse
if (L.link_majors & P.link_majors) == 0  -> no common Link major, refuse
if min(L.max_frame, P.max_frame) < FLOOR -> unusable link, refuse (§5)
```

**Every one of those four operations is symmetric.** `min` is commutative, `&`
is commutative, and the highest set bit of `A & B` does not depend on which
operand is which. Both peers therefore compute the same answer from the same
two inputs, in either order.

That property is the whole design, and §4 is its consequence.

## 4. Glare needs no tiebreaker here

If both peers send `CAPABILITY` simultaneously, both then hold both capability
sets, both compute the same selection because the function is symmetric, and
both send identical `NEGOTIATION` frames. The collision resolves itself.

This is deliberately **unlike** migration glare. Two simultaneous
`TRANSPORT_OFFER`s propose *different transports*, and both cannot proceed, so
`mcl_contact_resolve_offer_collision` compares `(source_ref, migration_ref)` and
the larger key wins. Here the two frames do not propose competing outcomes —
they supply the two halves of one computation. There is nothing to arbitrate.

No role is assigned. Neither peer is the initiator, the controller, or the
authority. Consistent with the constitutional rule, negotiation grants nothing.

## 5. Receiver rules

### 5.1 Receiving CAPABILITY

```text
control_version != 0                 -> refuse (PAYLOAD_REFUSED)
payload_len != 9                     -> refuse
wire_majors == 0 or link_majors == 0 -> refuse: a node that supports no major
                                        cannot be negotiated with
max_frame < FLOOR                    -> refuse: below the floor the link cannot
                                        carry MCL's own control frames
```

Otherwise record it. Receiving a `CAPABILITY` does not change any protocol
state by itself; it supplies one input to §3.

A second `CAPABILITY` from the same peer **replaces** the first. This is not a
duplicate to be suppressed: a peer whose configuration changed must be able to
say so, and the selection is a pure function of the current inputs. A peer that
re-advertises must expect a fresh `NEGOTIATION`.

### 5.2 Receiving NEGOTIATION

```text
control_version != 0     -> refuse
payload_len != 7         -> refuse
```

Then, and the distinction matters:

```text
IF the peer's CAPABILITY has been received:
    the selection MUST equal the locally computed f(L, P), field for field.
    Any difference -> refuse (PAYLOAD_REFUSED).

ELSE:
    the selection MUST be within the local capability:
        wire_major bit set in L.wire_majors
        link_major bit set in L.link_majors
        max_frame  <= L.max_frame  and  >= FLOOR
        features   a subset of L.features
    Any violation -> refuse.
```

The second branch is weaker on purpose and its limit is stated rather than
hidden: without the peer's advertisement, a receiver can verify that a selection
is *legal for itself*, but cannot verify the peer chose the highest common
major. A peer that selects a legal-but-lower major is suboptimal, not
incorrect, and v1 has no mechanism that could distinguish the two — see §7.

### 5.3 The floor

```text
FLOOR = MCL_LINK_FRAME_MIN_SIZE      (8)
      + MCL_LINK_FRAME_MAX_OPTIONAL  (16)
      + MCL_HANDOFF_CONTROL_MAX_SIZE (18)
      = 42 bytes
```

Every implementation MUST advertise `max_frame >= FLOOR`. Below it the link
cannot carry a `HANDOFF` control with a challenge, which is the largest payload
v1 requires — larger than the largest Tier-0 object at 17 bytes. A negotiation
that produced a link unable to carry the protocol's own migration frames would
succeed and then fail at the worst moment.

The constant is derived from the three sizes rather than written as `42`, and a
test asserts the derivation, so it cannot drift when any of them changes.

## 6. Feature bits: the mechanism ships, the table is empty

**Zero feature bits are assigned in MCL v1.0.** The field exists, the registry
exists, and the table is empty — the same disposition the extension-ID registry
already carries, where an empty Stable assignment table is explicitly a correct
outcome.

This is not the "Stable field nobody may use" trap that removed coordinates and
challenged `machine_class`. The difference is that `features` has a defined
behaviour for every possible value on day one, and a coordinate did not:

> **Unknown feature bits fail closed by construction.** The selection is
> `L.features & P.features`. A bit a peer sets that this implementation does not
> know is a bit this implementation did not set, so the `AND` clears it. An
> unrecognised feature can never end up in the negotiated set.

No unknown-feature rule has to be written, remembered, or tested against
implementations that got it wrong. The arithmetic is the rule.

## 7. What this does not provide

- **No downgrade protection.** Nothing here is authenticated, so an active
  attacker on an open medium can present a capability set claiming support for
  only the lowest major and both peers will honestly negotiate down to it. This
  is not a defect in the mechanism; it is the consequence of v1 having no
  cryptographic peer authentication, which `V1_SCOPE.md` §4.4 defers
  conspicuously. It is written here so that no reader infers protection from the
  presence of a negotiation.
- **No proof of capability.** A `CAPABILITY` frame is a *claim*, exactly as an
  `AUTHORITY_CLAIM` is. Reception is not verification. A peer that advertises a
  major and then cannot speak it produces an ordinary interoperability failure,
  refused at decode.
- **No identity.** Neither control carries or establishes one.

## 8. Test obligations

A conforming implementation must demonstrate, at minimum:

| Case | Expectation |
|---|---|
| Encode/decode round trip, both controls | exact |
| Wrong `control_version` | refused |
| Payload one byte short, one byte long | refused |
| `wire_majors == 0`, `link_majors == 0` | refused |
| `max_frame` below the floor | refused |
| Disjoint major sets | refused, no selection produced |
| Selection is symmetric: `f(L,P) == f(P,L)` | over exhaustive/randomised inputs |
| Glare: both compute identical `NEGOTIATION` | identical bytes |
| `NEGOTIATION` disagreeing with local computation | refused |
| `NEGOTIATION` outside local capability, peer set unknown | refused |
| Unknown feature bit set by peer | cleared from the selection |
| Repeated `CAPABILITY` replaces, does not duplicate | new selection |
