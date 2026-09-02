# Secure Contact: Candidate Mechanisms

Status: **Research Note** — non-normative. No mechanism is adopted here.

**Scope.** Everything below concerns an *optional* security profile, which a
deployment may choose not to use at all (Architecture Charter §2.10.1). Nothing
here is required to use MCL.

Companion to [`secure-contact-threat-model.md`](secure-contact-threat-model.md),
which states the problem. This note surveys what already exists, so that MCL
adopts rather than invents.

The governing principle is the project's own: freeze only what future
implementers must agree on. Cryptography is the worst possible place to be
original, and every mechanism below has had review that a new design would not.

## 1. What MCL should own versus delegate

| MCL should own | MCL should delegate |
|---|---|
| Discovery and first contact on an exposed medium | Authenticated key exchange |
| The semantics of what may cross an exposed medium | Credential formats and PKI |
| Transport capability advertisement and migration | Attestation token formats |
| **Contact continuity across a transport change** | Transport-native link security |
| Keeping security properties separate (charter §2.11) | Certificate path validation |
| The local authorization boundary | Trust anchor distribution |

Contact continuity is bolded because it is the one property no existing
mechanism provides for MCL, since no existing mechanism spans acoustic first
contact and a later BLE or IP channel. It arises from MCL's structure and is
therefore MCL's to define.

## 2. Prior art, and what each teaches

**Wi-Fi Easy Connect (DPP).** Bootstraps public-key material over an
out-of-band channel, then provisions network credentials over an encrypted
protocol — so the network password never crosses the bootstrap medium. This is
almost exactly the shape MCL-AP → Wi-Fi wants, and suggests MCL-AP could serve
as a machine-readable out-of-band bootstrap carrier. It does not offer a
transport-neutral contact layer.

**Bluetooth LE Secure Connections.** Supports out-of-band association, which
again matches the AP-bootstraps-BLE shape. The critical lesson is negative:
"Just Works" pairing produces a link that is encrypted but unauthenticated
against an active attacker. The BLE experiment in `mcl-ble/evidence` used
exactly this, and its record says so. *BLE connected* must never imply *peer
authenticated*.

**EDHOC (RFC 9528).** IETF Standards Track authenticated ephemeral
Diffie-Hellman for constrained devices: mutually authenticated, forward secret,
with identity protection, transcript hashes, cipher-suite negotiation,
connection references, credential agility, an exporter for derived keys, and a
slot for external authorization data.

Two properties make it the leading candidate rather than merely a good one.
First, **the transport is supplied by the application profile rather than built
in**. Second, its connection identifiers exist for state correlation and
explicitly not as authentication identifiers, which maps directly onto MCL's own
separation of contact references from identity.

**A correction to how the first property was previously stated here.** An
earlier revision said this "means a single EDHOC exchange can legitimately begin
on one medium and finish on another." That overstates what RFC 9528 provides.
The RFC leaves it to an application profile to say how EDHOC is transported, how
messages are correlated and what parameters are agreed — it does **not**
standardise a mid-handshake transport change, and no published analysis covers
one. The accurate statement is:

> EDHOC permits an application profile to define its own transport. An MCL
> profile may therefore *experimentally* define a cross-transport exchange, but
> that composition is MCL-specific, is not inherited from RFC 9528, and must be
> analysed and tested on its own.

The distinction matters for a candidate standard: **security analysis is not
inherited by a composition nobody analysed.** Choosing a reviewed AKE buys a
great deal, and it does not buy assurance for the novel part, which is exactly
the part MCL is contributing.

Published trace sizes (RFC 9529) are in the tens of bytes per message depending
on method and credential choice, which is not absurd for an acoustic bootstrap —
though that is a reason to *measure* the split, not to assume all messages
belong on AP.

Its CBOR/COSE representation would belong **inside an optional security profile
only**; importing it into MCL Wire would undo the deterministic-codec design for
no benefit.

EDHOC is a candidate, not scripture. RFC 9528 has published errata, and MCL's
transport-spanning profile would in any case be novel integration logic that no
existing analysis covers.

**TLS 1.3 exporters and channel binding (RFC 9266).** The established way to
bind higher-level authentication to one specific channel. This is the correct
shape for the migration-race defence, applied to a transport family MCL already
has a binding for.

**FIDO Device Onboard and BRSKI.** Both separate bootstrap from operational
trust as distinct stages, and FDO supports binding a device to its eventual
owner late. Useful precedent for staging, though both are device-to-owner
onboarding rather than peer-to-peer encounter.

**RATS architecture (RFC 9334) and EAT (RFC 9711).** Separates *evidence*
produced by an attester from the *decision* made by a relying party. This is the
same separation charter §2.11 requires, arrived at independently, and it is
strong support for keeping attestation and authorization apart. EAT gives a
standard attestation container, so MCL need not invent a device-health format.

**Matter commissioning.** Staged discovery → secure session → device
attestation → provisioning → operational security, with attestation bound to a
session-derived challenge to prevent replay. Confirms both the staging and the
binding. Matter is a single application ecosystem; MCL is meant to precede
arbitrary ones.

**Noise Protocol Framework.** Kept as the architectural comparison rather than
the candidate, for one specific reason: Noise transitions from its handshake
into two established transport cipher states, so the *post-handshake protected
record* is part of the framework. EDHOC leaves that to a companion — usually
OSCORE, because EDHOC's home is constrained CoAP deployments. MCL is not CoAP,
so adopting EDHOC leaves a real gap that adopting Noise would not.

| | EDHOC | Noise |
|---|---|---|
| Standards maturity | IETF Standards Track RFC | framework; main spec still labelled unstable |
| Constrained devices | excellent | good |
| Handshake size | excellent | good |
| Credential ecosystem | COSE, X.509, C509, RPK | application-defined |
| Formal analysis | substantial | substantial pattern analysis |
| Post-handshake record | application must supply | built in |
| Live profile ecosystem | active IETF LAKE work | no standards equivalent |

EDHOC should lead, because MCL wants broad interoperability rather than the
smallest library. Noise stays in the comparison because it answers a question
EDHOC does not, and because a control implementation would quantify whether
EDHOC's standards and credential advantages justify the integration cost.

**Active IETF work is a reason not to freeze anything yet.** The LAKE working
group has an EDHOC Application Profiles draft in working-group last call, which
defines canonical profile descriptions, profile identifiers and a means of
advertising supported profiles, alongside implementation-considerations,
extensibility, authorization, attestation and PSK work. That machinery overlaps
almost exactly with what MCL would otherwise invent. MCL may end up needing to
define only *how an EDHOC application profile is invoked over MCL* and *what MCL
context it binds* — which is far less surface than a profile registry of its own.

## 3. The shape this suggests

```text
MCL Core / Wire            unchanged
        |
MCL Link                   contact, session, migration
        |
        +-- optional Secure Contact Profile
                |
                +-- security provider interface   (MCL-defined)
                |        |
                |        +-- builder's own crypto / secure element /
                |            platform PSA / EDHOC / Noise / future
                |
                +-- contact continuity binding    (MCL-defined)
                |
        AP / BLE / IP / UWB / future transport
```

### 3.1 MCL defines the interface, not the cryptography

This is the governing decision, and it follows from the project's own rule that
cryptography is the worst place to be original.

| MCL defines | The builder supplies |
|---|---|
| The provider interface a mechanism plugs into | Algorithms and cipher suites |
| The canonical context that must be bound | Credentials and trust anchors |
| Which properties count as established | Key storage, secure element, TEE |
| How the result is surfaced, separately | The mechanism itself |

MCL never holds a private key. A provider may keep keys behind opaque handles,
in a secure element or under TrustZone, and MCL is indifferent — which is what
makes the specification survive hardware it has never seen.

**MCL's actual contribution here is not cryptographic.** It is the precise
definition of *what must be bound* so a migrated contact cannot be stolen:
roles, direction, contact references, fresh nonces, both peers' complete offers,
and the selection. That definition is algorithm-independent, and it is the part
no existing standard supplies, because no existing standard spans an acoustic
first contact and a later radio channel.

### 3.2 The limit of "bring your own", and why one named profile is still needed

A provider interface alone cannot serve MCL's founding scenario. Two machines
built by different companies, meeting for the first time with no mechanism in
common, cannot negotiate security at all — each has its own and neither can
speak the other's.

So the answer is both, at different scopes:

```text
within a fleet, or between partners     bring your own provider
between strangers                       one fully specified named profile
```

The named profile is a *profile*, not the layer, and a deployment may ignore it
entirely. But without at least one, MCL security is usable only inside a single
vendor's ecosystem — which is the outcome an interoperability standard exists to
prevent.

### 3.3 The unowned problem: what protects traffic after the handshake

Worth stating plainly because it is easy to miss while focusing on the
handshake. An AKE produces keys. It does not define what protects the MCL frames
that follow, and the security-relevant fields are not only the payload:

```text
frame class            session reference       sequence
handoff negotiation    transport selection
```

are all worth protecting, and all sit in the Link header rather than the
payload. Whatever eventually fills this gap must not be invented casually, and
it is currently unowned by any part of the design.

Two consequences follow.

**MCL's security should sit above transport security, not depend on it.** BLE
encryption and TLS are defence in depth, not the security model. Otherwise MCL's
guarantees would differ per medium, which contradicts charter §2.1: the same
semantic object must mean the same thing on every transport, and that has to
include what has been established about the peer sending it.

**A security profile is optional and negotiated, never assumed.** A machine that
implements no profile must still be able to make contact, because the
alternative is that a safety-critical `HAZARD` cannot be heard by a peer that
lacks a credential system.

## 4. The experiment this points to

The transports are now individually demonstrated: acoustic at E3/E4, IP over
2.4 GHz and BLE both at E4. The next experiment is not a fourth radio. It is the
first flow that crosses them:

The correction in the threat model changes the shape of this experiment. Because
proving knowledge of a public transcript proves nothing, the cryptographic
exchange cannot begin *after* migration. **It must begin on the first-contact
medium and finish on the second**, so that both peers have committed secret
state before the transport changes:

```text
AP first contact
   ephemeral references, nonces, capabilities, no stable identity
        |
AP   ->  key exchange begins here          <- both peers commit secret state
AP   <-  peer's contribution
        |
TRANSPORT_OFFER  ->  BLE or Wi-Fi
        |
        |  migration
        v
BLE  ->  the same key exchange completes
BLE  <-  key confirmation
        |
continuity holds because the peer possesses handshake state
committed during the acoustic contact, not because it saw it
        |
credential exchange, now private
        |
local authorization decision
        |
handoff to the application protocol
```

The success criterion is falsifiable, which no transport test so far has been:
**a passive observer that heard every acoustic byte and then races onto BLE must
fail**, because it holds none of the secret state.

That failure case is the experiment. Demonstrating the honest path succeeds is
the easy half, and on its own would prove only that three radios can carry the
same bytes — which is already established.

The detailed design, message placement options and the required negative tests
are in [`contact-continuity-experiment.md`](contact-continuity-experiment.md).

## 4.1 Two adjacent mechanisms whose boundaries must be kept

**MCL-AP is an out-of-band medium; it is not an authenticated out-of-band
medium.** Bluetooth Secure Connections supports OOB association, which makes it
tempting to carry Bluetooth OOB material acoustically and declare the resulting
BLE link authenticated. That does not follow. The security argument for OOB
association assumes properties of the OOB channel, and MCL-AP is by construction
observable, injectable and relayable. Until AP itself has whatever property that
argument requires, BLE should be treated as a carrier with the MCL profile run
above it — not as a link made authentic by an acoustic hint.

**DPP solves Wi-Fi provisioning, and only that.** Wi-Fi Easy Connect is direct
precedent for the right shape: bootstrap information travels out of band, and
the actual network credential travels over an encrypted channel rather than over
the bootstrap medium. MCL-AP could serve as another OOB carrier for that
bootstrap. What DPP does not provide is MCL contact binding or machine identity;
a device correctly provisioned onto a network is not thereby a peer whose
identity has been established. The existing SoftAP evidence remains valid as
*transport* evidence and must not become a normative provisioning method.

The general rule both cases share: a mechanism that establishes something about
a *network* or a *link* has not established anything about a *peer*, and the two
must not be quietly merged.

## 4.2 A dependency note, since implementation feasibility shapes the design

`libedhoc` is the actively maintained C implementation of RFC 9528 and is the
obvious candidate to *experiment* with. Recorded facts, not an endorsement:

```text
MIT licensed              key handles, so raw private keys need not be exposed
version 2.2.x             stack, heap or custom memory backends
builds as C11             zcbor dependency; reference suites use PSA / mbedTLS
Zephyr support            active development through 2026
```

The decisive one is **C11, plus zcbor, plus a crypto backend**. That settles the
architecture rather than complicating it:

```text
mcl-link            MUST NOT depend on libedhoc, or on any crypto library
                    it remains freestanding C99 with zero cryptography

experiment  ->  security provider adapter  ->  libedhoc  ->  PSA / mbedTLS
```

This is the same conclusion §3.1 reaches from first principles, arrived at from
implementation constraints instead — which is a reason to trust it.

A full dependency evaluation against RFC 9528 conformance, the RFC 9529 vectors,
bounded-memory operation, ESP32-S3 buildability and the current published errata
**has not been performed**, and this note does not claim it has.

For a first laboratory benchmark, a compact P-256 / static-DH configuration is
the pragmatic choice — the corresponding RFC 9529 trace is roughly 39, 45 and 19
bytes for the three messages, which is tractable acoustically. Signature and
certificate modes must be measured too, since published totals range from around
101 bytes to around 242, and that difference is decisive over an acoustic
channel. **None of this is a cipher-suite recommendation.** It is a choice of
instrument for measuring feasibility.

## 4.3 An implementation hole to resolve before any experiment

`mcl-link` documents the Link payload as canonical MCL Wire bytes. EDHOC
messages are not Wire semantic objects, and quietly placing raw handshake bytes
into `wire_payload` would make the frame a lie about its own contents.

This must be decided before code, not during it. The recommendation is **not**
to create a permanent `SECURITY` Core category — that would freeze a decision no
evidence supports yet. For research, carry profile data through the existing
extension machinery or a clearly experimental carrier, conceptually:

```text
PROFILE_NEGOTIATION {
    experimental_profile
    message_stage
    opaque_profile_bytes
}
```

inside an experimental namespace. If the experiment shows that generic profile
payload carriage is needed by many Link profiles, promote the general mechanism
then — which is the project's own rule: freeze it after evidence shows it is
needed.

## 5. What must not happen next

- No cryptographic primitive is written for MCL. Adopt reviewed constructions.
- No security code enters `mcl-link` before the profile is specified. The
  threat model and this note come first, deliberately.
- No field named for a property it does not establish. The `frame_check` rename
  was the first instance of this rule; it will not be the last opportunity to
  break it.
- No `trusted` boolean, on any interface, in any repository.
- Nothing in the codebase may imply confidentiality or authenticity while none
  exists, which is today the case everywhere.
- No continuity proof built on public values. Knowing the transcript is not
  participating in the contact; see the threat model §5.1.
- No session resumption, no 0-RTT and no long-lived pairing shortcut until the
  fresh-contact flow is secure. Each adds replay surface that buys nothing yet.
- No `AUTHENTICATED` or `TRUSTED` state added to the Link lifecycle. Link state
  describes communication and stays orthogonal to security-profile state and to
  local authorization; a session is perfectly valid with neither. See §5.1.
- No security library in `mcl-link`, `mcl-wire` or any binding. The provider
  interface exists precisely so cryptography stays outside the C99 foundation.
- No claim that a cross-transport composition inherits RFC 9528's analysis.
- No post-quantum suite mandated because it exists. Design *agility* now —
  profile identifiers, suite negotiation, rejection of unknown critical
  profiles, a replacement path — and freeze algorithms later.

### 5.1 Why the Link lifecycle must not gain a security state

The lifecycle is `IDLE, DISCOVERED, CAPABILITIES, NEGOTIATING, ESTABLISHED,
ADAPTING, HANDOFF, FALLBACK, CLOSED`. It describes communication, and it must
keep describing only that. Three orthogonal axes:

```text
Link state          ESTABLISHED
Security state      none
Authorization       receive hazards only
```

```text
Link state          ESTABLISHED
Security state      channel protected, peer credential verified
Authorization       telemetry yes, actuator control no
```

Both are valid contacts. Collapsing the axes into one ladder —
`ESTABLISHED → AUTHENTICATED → TRUSTED → AUTHORIZED` — would make every
unauthenticated deployment look like an incomplete one, and would reintroduce
the single trust indicator charter §2.11 forbids, in the form of a state machine
rather than a boolean. **This separation matters more than the choice of
cryptographic algorithm.**
