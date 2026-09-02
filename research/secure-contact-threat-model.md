# Secure Contact: Threat Model

Status: **Research Note** — non-normative, no mechanism is selected here.

This note states what a secure MCL contact would have to withstand. It
deliberately proposes no cryptography. A threat model written after a mechanism
is chosen tends to describe whatever that mechanism happens to defend.

## 1. The situation being defended

Two machines, built by unrelated organisations, encounter each other with:

```text
no shared secret
no common PKI
no trusted manufacturer root
no prior session
no human present
```

They must get from *I do not know you* to *I know enough about you to decide
what happens next*, over a medium anyone in range can observe.

The governing constraint follows from charter §2.10: the first-contact medium is
assumed observable and unauthenticated. Acoustic contact makes this obvious —
the channel is a loudspeaker — but the assumption holds for BLE advertising and
for any unprovisioned radio too. Nothing about MCL's security may depend on the
first medium being private.

## 2. What cannot be achieved, and must not be claimed

Two machines with no trust anchor in common **cannot** establish each other's
real-world identity by exchanging messages. They can establish a fresh shared
key and protect their conversation from outsiders. They cannot, from that alone,
establish that the peer is a particular manufacturer's certified machine.

Any design that appears to deliver identity without a trust anchor has hidden
the anchor somewhere, and the hiding place is the vulnerability.

## 3. Adversaries

**Passive listener.** Hears everything on the first-contact medium. Costs
nothing. Must be assumed always present. Defeats any design in which sensitive
material crosses the exposed channel in the clear, and — more subtly — any
design that emits a stable identifier, because correlation over time is itself
an attack.

**Active injector.** Transmits on the medium. Can forge frames, since a CRC is
recomputable and confers no protection. Can inject an `AUTHORITY_CLAIM`, an
`ADAPT`, or a `CLOSE` into someone else's contact.

**Replayer.** Records a valid exchange and re-emits it later. Defeats any design
whose acceptance depends only on a frame being well formed. `freshness_ms`
bounds a payload's useful lifetime but is not a replay defence: it is a
statement by the sender, and an attacker replays the statement along with the
frame.

**Relay / wormhole.** Forwards a real exchange between two locations in real
time, without modifying anything. Every participant sees valid traffic. This
attack defeats proximity claims specifically, which is why `mcl-uwb` exposes no
verified-distance field and why acoustic reception is described as proximity
*evidence* rather than proof of co-presence. A relay is not defeated by
cryptography alone.

**Transport-migration race.** The attack that the migration model in charter
§2.10 introduces. X meets Y acoustically. Y offers to continue over BLE. An
attacker connects to X over BLE first. X now believes it is talking to the
machine it just heard. Nothing was forged and nothing was broken; the attacker
simply arrived at the right moment. Defeating this requires binding the new
channel to the original contact — see §5.

**Downgrade.** Forces peers to a weaker transport, a weaker security profile, or
no security at all, by suppressing or altering capability advertisements.
Especially dangerous during first contact, where capabilities are exchanged
before any protection exists.

**Tracker.** Does not attack a session at all; correlates a machine's movements
over time from anything stable it emits. A permanent serial number is the
obvious hazard, but a stable public-key fingerprint or a long-lived contact
reference is equally usable. This adversary is the reason first contact should
prefer rotating references.

**Resource exhaustion.** Makes a receiver allocate, buffer, or compute on
unauthenticated input. Cheap on a broadcast medium. The existing bounds — a
maximum frame size, a bounded reassembly buffer, refusal of an implausible
declared length — are the current defences and are not sufficient on their own.

**Malicious authenticated peer.** A machine whose credentials are entirely
genuine and whose intentions are not. No authentication mechanism addresses
this, which is why authorization must remain a separate local decision. This
adversary is the reason charter §2.11 forbids a single trust indicator.

## 4. Properties, kept separate

Charter §2.11 requires these never to be summarised into one value. They are
listed here so a future profile can state precisely which ones it establishes.

| Property | Means |
|---|---|
| Contact continuity | the peer on this channel is the peer the contact began with |
| Channel confidentiality | outsiders cannot read the traffic |
| Channel authenticity | outsiders cannot modify the traffic undetected |
| Peer authentication | a credential has established some identity |
| Attestation | evidence about the peer's hardware or software state |
| Proximity evidence | measurements suggest the peer is nearby |
| Local authorization | this machine's policy permits a specific action |

Each is established by different means and each can hold while the others fail.
An encrypted BLE link established by Just Works pairing has confidentiality
against a passive listener and no authenticity against an active one. A verified
manufacturer certificate establishes identity and says nothing about
authorization. Proximity evidence survives none of the relay attack.

## 5. Contact continuity is the property MCL uniquely owes

The other properties have existing mechanisms MCL can adopt. Contact continuity
is the one that arises from MCL's own structure, because MCL is the only layer
that spans both transports.

The requirement is that after migration, both peers can establish:

> the peer I am now talking to participated in the contact I just had.

The natural construction is a transcript: a hash over the canonical Link frames
of the first-contact exchange, with explicit role and direction separation,
which the security exchange on the new transport must prove knowledge of. The
existing Link frame is already a canonical byte sequence, which is what makes
this feasible without inventing a second encoding.

Note what this does and does not give. It gives continuity. It does not give
identity: a relay that faithfully forwarded the original contact is still the
same participant by this definition. Continuity and identity are separate rows
in the table above and must stay separate.

## 6. Consequences for what first contact may carry

From the passive listener and the tracker together:

**Appropriate for exposed first contact:** ephemeral contact references, fresh
nonces, protocol versions, capability codes, security-profile capabilities,
transport offers, hazards, cryptographic commitments and ephemeral public keys.

**Not appropriate:** permanent serial numbers, operator or owner identity,
certificate chains, network credentials, stable machine identifiers, private
network topology.

The rule is not that sensitive data may never travel acoustically. Encrypted
traffic over a loudspeaker is still confidential against a passive listener. The
rule is:

> Sensitive data must not travel over an MCL channel until that channel has
> earned the properties local policy requires — on any medium, acoustic, BLE or
> IP alike.

## 7. What is already true in the codebase

Recorded so a future profile builds on the actual state rather than an assumed
one.

- Reception has no side effects: an `AUTHORITY_CLAIM` arriving in a frame
  changes no link state, and `mcl-sdk` has a test pinning this.
- `source_ref`, `destination_ref` and `session_ref` are documented as
  correlation references and never as identity.
- The Link frame check is a CRC-32 and is named `frame_check` precisely so it
  cannot be mistaken for cryptographic integrity.
- `mcl-uwb` exposes no verified-distance and no proximity-proved field, and its
  admissibility rule requires a drift-cancelling method and authenticated
  timestamps.
- Frames, payloads and reassembly buffers are bounded, and implausible declared
  lengths are refused rather than buffered.
- Transport migration exists as `HANDOFF` in the lifecycle, and the Link
  specification already states that handoff does not imply trust.

**What is missing:** every cryptographic property in §4. There is today no
confidentiality, no authenticity, no peer authentication and no contact
continuity anywhere in MCL. Nothing in the codebase claims otherwise, and
nothing should be built that implies otherwise until a profile exists.

## 8. Open questions

- Which mechanism, if any, MCL should adopt rather than invent — see
  [`secure-contact-candidate.md`](secure-contact-candidate.md).
- Whether the transcript hashes canonical Link frames directly, or a derived
  structure that survives retransmission and loss on an unreliable medium.
- How a security profile is negotiated during first contact without that
  negotiation itself becoming the downgrade surface.
- What a receive-only node can establish, given it cannot participate in a key
  exchange.
- Whether rotating contact references need a defined rotation policy, or whether
  that is properly an integrator decision.
