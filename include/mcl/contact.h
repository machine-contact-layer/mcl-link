#ifndef MCL_CONTACT_H
#define MCL_CONTACT_H

#include "mcl/link.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Contact migration: continuing one logical contact across a change of
 * transport.
 *
 * This is the capability that distinguishes MCL from a set of framing libraries
 * that happen to share a byte layout. Two machines meet on whatever medium
 * exists -- typically acoustic, because it needs no prior network -- agree on a
 * different transport, prove that transport actually works, and continue the
 * SAME contact there.
 *
 *   OLD TRANSPORT     TRANSPORT_OFFER   -> migration_ref, transport, profile,
 *                                          endpoint_token, validity
 *                     TRANSPORT_ACCEPT  <- migration_ref, transport, profile,
 *                                          session_ref
 *                     (old transport stays ACTIVE)
 *
 *   CANDIDATE         PATH_CHALLENGE    -> migration_ref, session_ref, challenge
 *                     PATH_RESPONSE     <- the same challenge echoed
 *
 *   NEW TRANSPORT     HANDOFF_COMMIT    -> migration_ref, session_ref
 *                     HANDOFF_CONFIRM   <- migration_ref, session_ref
 *                     (new transport becomes ACTIVE)
 *
 *
 * WHY ACCEPTANCE IS NOT ARRIVAL
 *
 * An earlier revision switched transports as soon as anything appeared carrying
 * the right session reference. That proves neither that the candidate path
 * works in both directions nor that the peer will remain reachable on it, even
 * with no adversary anywhere. QUIC settled this: probe the candidate path,
 * validate it, and if validation fails do not fail the connection while the old
 * path still works. MCL takes the principle, not the packet format.
 *
 *
 * WHAT THIS PROVES, AND WHAT IT DOES NOT
 *
 * Proves: transaction correlation, path reachability, ordinary session
 * continuity.
 *
 * Does NOT prove: identity, authenticity, authority, or cryptographic contact
 * binding.
 *
 * `session_ref`, `migration_ref` and the challenge all travel in the clear over
 * an observable medium. Anyone in range can read them and quote them back. An
 * attacker that heard the first contact can complete this entire sequence and
 * be accepted, exactly as an honest peer would be. That is the honest limit of
 * what correlation and reachability can establish without cryptography, and it
 * is stated here rather than discovered later.
 *
 * Do not build an authorization decision on any of it. Charter 2.11.2: where
 * local policy requires a property, the absence of the mechanism that
 * establishes it must cause refusal, not acceptance.
 *
 * The majority of deployments need exactly what is here and nothing more.
 * Charter 2.10 makes those first-class.
 * ============================================================ */

/*
 * Transport identifiers, mirroring the authoritative assignment in
 * mcl-link/registries/transport-ids-v0.1.json. Zero is permanently reserved so
 * a zeroed or uninitialised field is never a valid transport.
 */
#define MCL_CONTACT_TRANSPORT_RESERVED 0u
#define MCL_CONTACT_TRANSPORT_AP       1u
#define MCL_CONTACT_TRANSPORT_IP       2u
#define MCL_CONTACT_TRANSPORT_BLE      3u
#define MCL_CONTACT_TRANSPORT_UWB      4u

/*
 * Profile identifiers are TRANSPORT-SCOPED: profile 1 under IP and profile 1
 * under BLE are unrelated assignments and must never be compared. This module
 * therefore does not know what any profile means, and deciding whether a given
 * one is acceptable is deployment policy (charter 2.10.1).
 *
 * It knows one thing about them: zero is permanently reserved in every
 * transport's profile registry, for the same reason transport zero is, so that
 * a zeroed or uninitialised field names no profile. An offer carrying profile
 * zero is malformed by the registries' own rule, and that rule was stated in
 * all four registries while nothing anywhere enforced it.
 */
#define MCL_CONTACT_PROFILE_RESERVED 0u

/* Reserved sentinels, so an uninitialised field can never match a live value. */
#define MCL_CONTACT_SESSION_NONE   0u
#define MCL_CONTACT_MIGRATION_NONE 0u

/*
 * Path-validation challenge size.
 *
 * Eight opaque bytes, supplied by the caller. The library contains no random
 * number generator and must not: it is freestanding C99 with no entropy source
 * of its own, and inventing one here would be worse than requiring the caller
 * to supply what its platform already has.
 *
 * The challenge should be unpredictable, so that guessing it is harder than
 * actually receiving the frame containing it. That is a reachability property,
 * not a security one -- an observer of the candidate path sees the challenge.
 */
#define MCL_CONTACT_CHALLENGE_SIZE 8u

typedef uint8_t mcl_contact_role_t;
enum {
    /* Roles order negotiation. They confer no authority whatever. */
    MCL_CONTACT_ROLE_INITIATOR = 0u,
    MCL_CONTACT_ROLE_RESPONDER = 1u
};

/*
 * WHAT THIS STATE MACHINE IS, AND WHAT IT IS NOT
 *
 * It is a TRANSPORT-CONTINUITY machine and nothing else. Each state answers one
 * question: which medium carries this contact, and is a change of medium under
 * way?
 *
 * It does NOT describe the Link lifecycle (mcl_link_state_t) and does not map
 * onto it. Those two answer different questions -- "have we discovered,
 * exchanged capabilities and negotiated?" versus "which transport are we on?"
 * -- and neither implies the other. A machine can be settled on a transport
 * having negotiated nothing, and can be mid-negotiation without any migration.
 *
 * An earlier revision provided mcl_contact_link_state(), which claimed a
 * mapping between the two. It has been REMOVED. A function that tells you what
 * one state machine "ought" to look like, next to a second machine that is
 * independently mutable, is a third source of truth that drifts from both. The
 * single place the two genuinely cross is owned by the SDK and stated in
 * spec/link-contact-ownership-v0.1.md.
 */
typedef uint8_t mcl_contact_state_t;
enum {
    MCL_CONTACT_STATE_NONE       = 0u,  /* not begun */
    /*
     * Settled on active_transport with no migration in progress.
     *
     * This asserts nothing about negotiation, capabilities, identity or trust.
     * It is true from the moment a contact is begun, because a machine is
     * always on some medium and is not always changing it.
     */
    MCL_CONTACT_STATE_ACTIVE     = 1u,
    MCL_CONTACT_STATE_OFFERED    = 2u,  /* a transport change is proposed */
    MCL_CONTACT_STATE_AGREED     = 3u,  /* accepted; candidate not yet proven */
    MCL_CONTACT_STATE_VALIDATING = 4u,  /* challenge outstanding on the candidate */
    MCL_CONTACT_STATE_VALIDATED  = 5u,  /* candidate path proven reachable */
    MCL_CONTACT_STATE_COMMITTING = 6u,  /* commit sent, awaiting confirmation */
    MCL_CONTACT_STATE_CLOSED     = 7u
};

/*
 * Outcome of a simultaneous-offer collision.
 *
 * Two autonomous machines will eventually offer at the same moment. If each
 * simply rejects the other's offer, both wait forever: a deadlock, not a
 * refusal. ICE calls this glare and resolves it with a deterministic
 * tiebreaker rather than mutual rejection; MCL needs the same property without
 * needing ICE.
 */
typedef uint8_t mcl_contact_collision_t;
enum {
    MCL_CONTACT_COLLISION_LOCAL_WINS = 0u, /* keep our offer; peer withdraws */
    MCL_CONTACT_COLLISION_PEER_WINS  = 1u, /* adopt the peer's offer */
    MCL_CONTACT_COLLISION_TIE_ABORT  = 2u  /* both abandon; retry with new refs */
};

/*
 * Caller-owned. No heap, no hidden globals, no libc at runtime.
 *
 * Every reference below is distinct on purpose, and none may be reused as
 * another because they happen to be the same width:
 *
 *   local_ref / peer_ref  participant correlation within this contact
 *   migration_ref         one transport-change transaction
 *   endpoint_token        rendezvous reference for the candidate endpoint
 *   session_ref           the continued logical contact
 *
 * None is identity. None is authorization. A Wire context_id is a fifth,
 * separate thing and must never be copied into session_ref.
 *
 *
 * SESSION_REF LIFETIME: ONE VALUE FOR THE LIFE OF THE CONTACT
 *
 * Two models were possible and only one can be true:
 *
 *   A. session_ref names the continuing logical contact and persists across
 *      every migration.
 *   B. session_ref names one transport epoch and rotates on each hop.
 *
 * MCL chooses A, and enforces it: the value is bound by the first acceptance
 * and mcl_contact_agree REFUSES a later acceptance that names a different one.
 *
 * A is chosen because continuity across a change of medium is the property this
 * whole module exists to provide, and under B the identifier that is supposed
 * to express it changes exactly when it is needed. A peer that missed one hop
 * could not tell a continuing contact from a new one.
 *
 * Abandoning a migration does NOT unbind it. The value survives a failed
 * migration for the same reason the contact does: the machines are still the
 * same two machines on the medium where they met.
 *
 * If a rotating per-epoch identifier is ever needed -- for unlinkability, say
 * -- it must be a SIXTH reference with its own name, not this one reused.
 */
typedef struct {
    uint32_t local_ref;
    uint32_t peer_ref;
    uint32_t session_ref;

    uint8_t  role;
    uint8_t  state;
    uint8_t  active_transport;

    uint32_t pending_migration_ref;
    uint8_t  pending_transport;
    uint8_t  pending_profile;
    uint32_t pending_endpoint_token;
    uint8_t  pending_validity;

    uint8_t  challenge[MCL_CONTACT_CHALLENGE_SIZE];
    uint8_t  challenge_valid;

    uint8_t  peer_ref_valid;
    uint8_t  session_valid;
    uint16_t migration_count;

    /*
     * The migration reference of the most recently completed transport change,
     * retained after the pending transaction is cleared so that a COMMIT
     * retransmitted by a peer whose CONFIRM was lost can still be answered.
     * See mcl_contact_commit_repeat. Zero when no migration has completed.
     */
    uint32_t completed_migration_ref;
} mcl_contact_t;

/*
 * Begin a contact on a transport. `local_ref` is this machine's contact
 * reference for correlation; it is not identity and should be rotated per
 * contact so it cannot be used to track the machine over time.
 */
mcl_link_status_t mcl_contact_begin(
    mcl_contact_t *contact,
    mcl_contact_role_t role,
    uint32_t local_ref,
    uint8_t transport_id);

/*
 * Record the peer's contact reference, learned during first contact.
 *
 * Idempotent, and first-write-wins: repeating the same value succeeds, and a
 * DIFFERENT value is refused. A peer that could silently change its contact
 * reference mid-contact would break the correlation this whole module provides,
 * and would let a third party redirect a contact by asserting a new reference.
 */
mcl_link_status_t mcl_contact_set_peer_ref(
    mcl_contact_t *contact,
    uint32_t peer_ref);

/* ============================================================
 * DUPLICATES AND RETRANSMISSION
 *
 * Every control in the migration sequence can be lost, and the only repair
 * available on a lossy medium is retransmission. So every control must be
 * answerable a second time without changing the outcome. An implementation that
 * handles only the first copy of each works perfectly in a test harness and
 * strands a contact the first time a radio drops a frame.
 *
 * The rule for the whole sequence:
 *
 *   A repeat of a control that names the SAME transaction and carries the SAME
 *   content succeeds and changes nothing. A repeat that names the same
 *   transaction with DIFFERENT content is refused -- it is either a bug or a
 *   third party rewriting a step, and there is no correct way to choose between
 *   two versions of one step.
 *
 * Applied per control:
 *
 *   duplicate OFFER           mcl_contact_record_offer, same refs and params
 *   duplicate ACCEPT          mcl_contact_agree, from any post-agreement state
 *   duplicate PATH_CHALLENGE  mcl_contact_challenge_repeat, re-echo
 *   duplicate PATH_RESPONSE   mcl_contact_validation_response, from VALIDATED
 *   duplicate COMMIT          mcl_contact_commit_repeat, re-confirm
 *   duplicate CONFIRM         mcl_contact_commit_confirm, from ACTIVE
 *
 * A duplicate ACCEPT deserves particular care: it must NOT return the contact
 * to AGREED from VALIDATING, VALIDATED or COMMITTING. A delayed copy of a step
 * already completed would otherwise undo the progress made after it, and on a
 * medium that reorders, a delayed copy is ordinary.
 * ============================================================ */

/*
 * Record a proposed transport change, whether this machine sent the offer or
 * received it.
 *
 * `migration_ref` identifies this transaction and must be non-zero. `validity`
 * is retained for the caller to enforce expiry: this library has no clock, and
 * inventing one would tie a freestanding protocol library to a platform's
 * notion of time.
 *
 * Whether a given transport is acceptable is deliberately NOT decided here.
 * That is deployment policy (charter 2.10.1); this module enforces only what
 * two implementations must agree on.
 */
mcl_link_status_t mcl_contact_record_offer(
    mcl_contact_t *contact,
    uint32_t migration_ref,
    uint8_t transport_id,
    uint8_t profile_id,
    uint32_t endpoint_token,
    uint8_t validity);

/*
 * Resolve a peer offer that arrived while our own offer was outstanding.
 *
 * The tiebreaker compares (source_ref, migration_ref) as one logical key: the
 * larger key wins the migration-controller role. Both machines compare the same
 * two values and therefore reach the same conclusion without another round
 * trip. An exact tie aborts both transactions, because a tie means the two keys
 * cannot be distinguished and continuing would leave the peers disagreeing
 * about who controls the migration.
 *
 * The role decides negotiation ordering only. It grants no authority.
 *
 * On PEER_WINS the peer's offer replaces ours and the contact stays OFFERED.
 * On TIE_ABORT the contact returns to ACTIVE with no pending migration.
 */
mcl_link_status_t mcl_contact_resolve_offer_collision(
    mcl_contact_t *contact,
    uint32_t peer_source_ref,
    uint32_t peer_migration_ref,
    uint8_t peer_transport_id,
    uint8_t peer_profile_id,
    uint32_t peer_endpoint_token,
    uint8_t peer_validity,
    mcl_contact_collision_t *outcome);

/*
 * Agree the proposed change, binding a session reference to it. Both peers call
 * this: the accepting peer when it chooses the reference, the offering peer
 * when it receives the acceptance.
 *
 * The migration reference, transport and profile must all match what was
 * offered. Without the migration reference a delayed acceptance belonging to an
 * abandoned offer would be indistinguishable from the acceptance of the current
 * one, since transport and profile are normally identical across a retry.
 */
mcl_link_status_t mcl_contact_agree(
    mcl_contact_t *contact,
    uint32_t migration_ref,
    uint8_t transport_id,
    uint8_t profile_id,
    uint32_t session_ref);

/*
 * Begin path validation by recording the challenge sent on the candidate
 * transport. The caller supplies the bytes.
 */
mcl_link_status_t mcl_contact_validation_begin(
    mcl_contact_t *contact,
    const uint8_t challenge[MCL_CONTACT_CHALLENGE_SIZE]);

/*
 * Complete path validation with the peer's echoed challenge, received on the
 * candidate transport. Every field must match.
 *
 * Success means the candidate path carried a frame in both directions. It does
 * not mean the peer is the machine the contact began with.
 *
 * Idempotent from VALIDATED: a duplicate response naming the same transaction
 * and echoing the same challenge succeeds and changes nothing. Refusing it
 * would turn an ordinary retransmission into a migration failure.
 */
mcl_link_status_t mcl_contact_validation_response(
    mcl_contact_t *contact,
    uint32_t migration_ref,
    uint32_t session_ref,
    const uint8_t echo[MCL_CONTACT_CHALLENGE_SIZE]);

/*
 * Should a repeated PATH_CHALLENGE be answered with PATH_RESPONSE again?
 *
 * WHY THIS EXISTS
 *
 * A sends PATH_CHALLENGE. B echoes it, reaching VALIDATED, and the
 * PATH_RESPONSE is lost. A, hearing nothing, retransmits PATH_CHALLENGE -- the
 * correct thing to do. But B has left AGREED, so the state machine that accepts
 * a challenge only in AGREED refuses the duplicate, and the migration dies
 * from one dropped frame with both peers behaving correctly.
 *
 * This is the same shape as the lost CONFIRM (mcl_contact_commit_repeat) and
 * has the same answer: remember enough to give the same reply again.
 *
 * Sets *reecho to 1 only when the contact is VALIDATED, the transaction matches
 * and the challenge bytes are IDENTICAL to the ones already echoed. A different
 * challenge under the same migration_ref is refused: an honest retransmission
 * repeats itself, and a new challenge for a transaction already validated is
 * either a bug or a third party trying to have bytes of its choosing echoed
 * back on a path this machine has already committed to probing.
 *
 * Changes nothing. Re-echoing must not re-run validation.
 */
mcl_link_status_t mcl_contact_challenge_repeat(
    const mcl_contact_t *contact,
    uint32_t migration_ref,
    uint32_t session_ref,
    const uint8_t challenge[MCL_CONTACT_CHALLENGE_SIZE],
    uint8_t *reecho);

/*
 * Begin committing a validated candidate path. Called by the peer that SENDS
 * COMMIT, immediately before it transmits.
 *
 * Entering COMMITTING is the point of no return. From here this machine cannot
 * know whether the peer received the commit, so it cannot know which transport
 * the peer is on. See mcl_contact_abandon_migration.
 */
mcl_link_status_t mcl_contact_commit_begin(mcl_contact_t *contact);

/*
 * Accept a commit received on the candidate path, in one step. Called by the
 * peer that RECEIVES COMMIT, which then sends CONFIRM.
 *
 * This exists so that COMMITTING has exactly one meaning: "I sent COMMIT and do
 * not know whether it arrived." An earlier revision had the receiving peer pass
 * through COMMITTING too, on its way from VALIDATED to ACTIVE. That made the
 * state ambiguous -- rollback is safe for a peer that has not yet sent CONFIRM
 * and unsafe for a peer awaiting one -- and the two cases cannot be told apart
 * from the state alone.
 *
 * The transaction is checked before anything moves, so a commit that does not
 * match leaves the contact in VALIDATED with the old transport intact.
 */
mcl_link_status_t mcl_contact_commit_accept(
    mcl_contact_t *contact,
    uint32_t migration_ref,
    uint32_t session_ref);

/*
 * Confirm the commit; the candidate becomes the active transport.
 *
 * Committing is a two-step exchange so the peers cannot end up disagreeing
 * about which transport is authoritative, which is what a single unilateral
 * switch would allow.
 *
 * Idempotent from ACTIVE: a duplicate CONFIRM naming the migration that most
 * recently completed succeeds and changes nothing. A CONFIRM naming anything
 * else from ACTIVE is refused.
 */
mcl_link_status_t mcl_contact_commit_confirm(
    mcl_contact_t *contact,
    uint32_t migration_ref,
    uint32_t session_ref);

/*
 * Should a COMMIT that arrives with no migration in progress be answered with
 * CONFIRM again?
 *
 * WHY THIS EXISTS
 *
 * Suppose A sends COMMIT, B accepts it, completes the migration and replies
 * CONFIRM -- and the CONFIRM is lost. B is now on the new transport. A is still
 * COMMITTING, eventually gives up, and returns to the old transport, which is
 * the correct behaviour for a migration that failed. But B's did not fail. The
 * two machines now disagree about which transport carries the contact, and
 * nothing in the sequence corrects it. No adversary is involved; one dropped
 * frame is enough.
 *
 * An abort message does not fix this, because the peers are no longer on a
 * common transport to abort over. Retransmission does: A resends COMMIT, and B
 * must be able to answer it a second time. That requires B to remember the
 * transaction it just completed, which is why `completed_migration_ref`
 * survives the clearing of the pending transaction. TCP and QUIC both keep
 * exactly this kind of short memory, for exactly this reason.
 *
 * Sets *reconfirm to 1 only when the contact is ACTIVE, no migration is in
 * progress, and both references match the migration that most recently
 * completed. The caller then re-sends CONFIRM.
 *
 * This function changes nothing. Re-confirming must not re-run a migration, and
 * a repeated COMMIT must not become a way to make a settled contact move again.
 * It also establishes nothing about who sent the COMMIT: the references crossed
 * an observable medium, so a listener can replay them and be answered. The
 * answer only restates a transport change that already happened.
 */
mcl_link_status_t mcl_contact_commit_repeat(
    const mcl_contact_t *contact,
    uint32_t migration_ref,
    uint32_t session_ref,
    uint8_t *reconfirm);

/*
 * Abandon a migration and remain on the current transport.
 *
 * Valid from OFFERED, AGREED, VALIDATING and VALIDATED. In all of those, no
 * COMMIT has been sent, so the peer cannot have switched and returning to the
 * old transport is a statement about this machine alone. A failed migration
 * must never destroy the contact: the machines can still talk on the medium
 * where they actually met, and going back there is the correct outcome rather
 * than a degraded one.
 *
 * REFUSED FROM COMMITTING, AND THAT IS THE POINT.
 *
 * Once COMMIT has been sent, this machine cannot know whether the peer received
 * it. If it did, the peer is already on the new transport. Rolling back here
 * would be asserting something unknowable:
 *
 *     "I did not hear CONFIRM, therefore the peer did not commit."
 *
 * That inference is false on any unreliable channel, and acting on it produces
 * exactly the split the retransmission rule exists to prevent -- one peer on
 * the new transport, one back on the old, permanently. An earlier revision
 * allowed this and simultaneously documented the divergence it caused, which
 * was a contradiction rather than a policy.
 *
 * This is the ordinary uncertainty of distributed commit: no finite exchange
 * of acknowledgements lets the sender of the last message know it arrived. MCL
 * resolves it the way protocols that must actually work do -- by making the
 * decision irrevocable once transmitted, and retrying until it is confirmed.
 *
 * From COMMITTING there are exactly two honest outcomes:
 *
 *   - retransmit COMMIT until CONFIRM arrives. The peer answers a repeated
 *     COMMIT idempotently (mcl_contact_commit_repeat), so this is safe however
 *     many times it takes.
 *   - give up on the CONTACT, not on the migration: mcl_contact_close. The
 *     contact is lost, which is honest, rather than silently continuing on a
 *     transport the peer may have left.
 *
 * The old path may stay physically open throughout as a fallback for the
 * caller's own traffic. What is forbidden is declaring the migration failed.
 */
mcl_link_status_t mcl_contact_abandon_migration(mcl_contact_t *contact);

/* Close the contact. A closed contact is inert and cannot be revived. */
mcl_link_status_t mcl_contact_close(mcl_contact_t *contact);

/* Query the transport currently carrying the contact. */
mcl_link_status_t mcl_contact_active_transport(
    const mcl_contact_t *contact,
    uint8_t *transport_id);

/*
 * Is a transport-change transaction outstanding at any stage?
 *
 * A fact about this machine, replacing the removed mcl_contact_link_state().
 * It reports what is true rather than what another state machine ought to look
 * like, which is the difference between an accessor and a second source of
 * truth.
 */
mcl_link_status_t mcl_contact_migration_active(
    const mcl_contact_t *contact,
    uint8_t *active);

/* ============================================================
 * WHICH TRANSPORT DO THESE BYTES GO ON?
 *
 * During a migration a contact spans TWO media at once, and "the transport" is
 * not one thing. The offer and acceptance travel on the old one, the four
 * handoff controls travel on the candidate, and ordinary traffic travels on
 * whichever the cutover has reached. A send path with one transport handle
 * cannot express that, and a receive path that does not know where bytes ARRIVED
 * cannot check it -- which is how a PATH_RESPONSE fed in from the old path
 * validates a candidate nobody ever probed.
 *
 * These two functions are the whole answer, and they are here rather than in
 * the SDK because they are protocol rules, not integration convenience.
 * ============================================================ */

/*
 * The transport on which a handoff control for the current transaction MUST be
 * sent, and on which one MUST have arrived to be accepted.
 *
 * While a migration is in progress this is the CANDIDATE, because every one of
 * the four controls exists to establish or complete the move to it. Otherwise
 * it is the active transport, which is where a retransmitted COMMIT for an
 * already-completed migration arrives -- the peer that finished the move is
 * listening there and nowhere else.
 *
 * A control arriving anywhere else MUST be refused. Accepting a PATH_RESPONSE
 * that arrived over the old path would mean the candidate was declared
 * reachable on the strength of bytes that never crossed it, which is the one
 * thing path validation exists to establish.
 */
mcl_link_status_t mcl_contact_control_transport(
    const mcl_contact_t *contact,
    uint8_t *transport_id);

/*
 * The transport ordinary (non-handoff) traffic uses, and whether it is
 * currently quiesced.
 *
 * DATA-PLANE CUTOVER
 *
 * Between the transmission of COMMIT and the arrival of CONFIRM the two peers
 * genuinely disagree about which transport carries the contact, and the
 * disagreement is not a bug: the receiver of a COMMIT is on the new transport
 * immediately, while the sender cannot know whether its COMMIT arrived. See the
 * note on mcl_contact_abandon_migration.
 *
 * In that window ordinary traffic sent on `active_transport` may go out on a
 * medium the peer has already left. So *quiesced is set to 1 while the contact
 * is COMMITTING, and the caller MUST NOT send ordinary traffic for this contact
 * until it clears. The window is bounded by the CONFIRM exchange, which is the
 * shortest it can be made without the sender guessing.
 *
 * Quiescing rather than duplicating is the base-version rule. Duplicating a
 * semantic object onto both media would deliver some operations twice, and at
 * this layer nothing knows which operations are safe to repeat. A profile that
 * defines duplicate-safe transition behaviour may do better; none does yet.
 */
mcl_link_status_t mcl_contact_data_transport(
    const mcl_contact_t *contact,
    uint8_t *transport_id,
    uint8_t *quiesced);

#ifdef __cplusplus
}
#endif

#endif /* MCL_CONTACT_H */
