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

typedef uint8_t mcl_contact_state_t;
enum {
    MCL_CONTACT_STATE_NONE       = 0u,  /* not begun */
    MCL_CONTACT_STATE_ACTIVE     = 1u,  /* live on active_transport */
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
 */
mcl_link_status_t mcl_contact_validation_response(
    mcl_contact_t *contact,
    uint32_t migration_ref,
    uint32_t session_ref,
    const uint8_t echo[MCL_CONTACT_CHALLENGE_SIZE]);

/* Send the commit for a validated candidate path. */
mcl_link_status_t mcl_contact_commit_begin(mcl_contact_t *contact);

/*
 * Confirm the commit; the candidate becomes the active transport.
 *
 * Committing is a two-step exchange so the peers cannot end up disagreeing
 * about which transport is authoritative, which is what a single unilateral
 * switch would allow.
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
 * Abandon a migration at any stage and remain on the current transport.
 *
 * Valid from OFFERED, AGREED, VALIDATING, VALIDATED and COMMITTING, because a
 * migration can fail at any of them: the offer expires, the peer declines, the
 * candidate path never validates, or the commit is never confirmed.
 *
 * A failed migration must never destroy the contact. The machines can still
 * talk on the medium where they actually met, and returning there is the
 * correct outcome rather than a degraded one.
 */
mcl_link_status_t mcl_contact_abandon_migration(mcl_contact_t *contact);

/* Close the contact. A closed contact is inert and cannot be revived. */
mcl_link_status_t mcl_contact_close(mcl_contact_t *contact);

/* Query the transport currently carrying the contact. */
mcl_link_status_t mcl_contact_active_transport(
    const mcl_contact_t *contact,
    uint8_t *transport_id);

/*
 * Map the contact state onto the Link lifecycle state a frame should carry.
 *
 * Exists so the two state machines cannot drift apart. They are related but not
 * identical: the Link lifecycle describes communication, and the contact
 * machine describes which transport carries it.
 *
 * This is NOT a security or authorization state. Charter 2.11.1: communication
 * state, security state and local authorization are orthogonal, and the Link
 * lifecycle must never grow an AUTHENTICATED or TRUSTED state.
 */
mcl_link_status_t mcl_contact_link_state(
    const mcl_contact_t *contact,
    mcl_link_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* MCL_CONTACT_H */
