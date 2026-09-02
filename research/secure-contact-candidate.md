# Secure Contact: Candidate Mechanisms

Status: **Research Note** — non-normative. No mechanism is adopted here.

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

**EDHOC (RFC 9528).** Compact authenticated Diffie-Hellman for constrained
devices: transport-independent, mutually authenticated, forward secret, with
identity protection, transcript hashes, connection references, credential
agility, an exporter for derived keys, and a slot for external authorization
data. The property list is close to what §1 leaves undelegated, and the
transcript hashes are directly relevant to contact continuity. Its CBOR/COSE
representation would belong **inside an optional security profile only**;
importing it into MCL Wire would undo the deterministic-codec design for no
benefit.

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

## 3. The shape this suggests

```text
MCL Core / Wire            unchanged
        |
MCL Link                   contact, session, migration
        |
        +-- optional Secure Contact Profile
                |
                +-- adopted authenticated key exchange
                +-- contact transcript binding   (MCL-defined)
                |
        AP / BLE / IP / UWB / future transport
```

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

```text
AP first contact
   ephemeral references, nonces, capabilities, no stable identity
        |
TRANSPORT_OFFER  ->  BLE or Wi-Fi
        |
real connection on that transport
        |
authenticated key exchange at the MCL layer
        |
proof of the same contact transcript          <- the property MCL owns
        |
credential exchange, now private
        |
local authorization decision
        |
handoff to the application protocol
```

The success criterion is stronger than any transport test run so far, and it is
falsifiable: an attacker that connects over BLE without having participated in
the acoustic contact must fail the transcript proof.

That failure case is the experiment. Demonstrating the honest path succeeds is
the easy half, and on its own would prove only that three radios can carry the
same bytes — which is already established.

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
