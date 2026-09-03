/*
 * Contact migration: continuing one logical contact across a transport change.
 *
 * The positive path is the easy half. What decides whether two independent
 * implementations can migrate without corrupting each other's state is what
 * gets REFUSED, so most of this file is refusals, and each says what would
 * break if the assertion failed.
 */

#include "mcl/contact.h"

#include <stdio.h>
#include <stdlib.h>

static int g_checks = 0;

#define CHECK_STATUS(call, expected) do { \
    const mcl_link_status_t st__ = (call); \
    ++g_checks; \
    if (st__ != (expected)) { \
        fprintf(stderr, "FAIL at %s:%d: %s returned %d, expected %d\n", \
                __FILE__, __LINE__, #call, (int)st__, (int)(expected)); \
        exit(1); \
    } \
} while (0)

#define CHECK_TRUE(expr) do { \
    ++g_checks; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL at %s:%d: (%s) is false\n", \
                __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static const uint8_t CHALLENGE_A[MCL_CONTACT_CHALLENGE_SIZE] =
    {0x51u, 0x1Cu, 0x0Fu, 0xFEu, 0xA7u, 0x3Bu, 0x90u, 0x22u};
static const uint8_t CHALLENGE_B[MCL_CONTACT_CHALLENGE_SIZE] =
    {0x51u, 0x1Cu, 0x0Fu, 0xFEu, 0xA7u, 0x3Bu, 0x90u, 0x23u}; /* differs in last byte only */

#define MIG_A UINT32_C(0x4D194201)
#define MIG_B UINT32_C(0x4D194202)
#define SESS  UINT32_C(0x5E5510C7)

/* Drive one contact through the full pipeline. */
static void run_full_migration(
    mcl_contact_t *c,
    uint32_t migration_ref,
    uint8_t transport,
    uint8_t profile,
    uint32_t session_ref)
{
    CHECK_STATUS(mcl_contact_record_offer(c, migration_ref, transport, profile,
                                          0xD00D0001u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(c, migration_ref, transport, profile,
                                   session_ref), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_begin(c, CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(c, migration_ref, session_ref,
                                                 CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_begin(c), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_confirm(c, migration_ref, session_ref), MCL_LINK_OK);
}

/* Two peers meet acoustically and continue over BLE, with no cryptography
 * anywhere. This is the flow the whole project exists to make possible. */
static void test_acoustic_to_ble_migration(void)
{
    mcl_contact_t initiator;
    mcl_contact_t responder;
    uint8_t transport = 0u;
    mcl_link_state_t link_state = 0u;

    CHECK_STATUS(mcl_contact_begin(&initiator, MCL_CONTACT_ROLE_INITIATOR,
                                   0xA1A1A1A1u, MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_begin(&responder, MCL_CONTACT_ROLE_RESPONDER,
                                   0xB2B2B2B2u, MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_set_peer_ref(&initiator, 0xB2B2B2B2u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_set_peer_ref(&responder, 0xA1A1A1A1u), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_record_offer(&initiator, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&responder, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_agree(&responder, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                   1u, SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&initiator, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                   1u, SESS), MCL_LINK_OK);

    /* Acceptance is not arrival: the contact is still on the acoustic path. */
    CHECK_STATUS(mcl_contact_active_transport(&initiator, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_AP);

    /* Link lifecycle tracks the contact machine rather than drifting from it. */
    CHECK_STATUS(mcl_contact_link_state(&initiator, &link_state), MCL_LINK_OK);
    CHECK_TRUE(link_state == MCL_LINK_STATE_HANDOFF);

    /* Probe the candidate path in both directions before trusting it. */
    CHECK_STATUS(mcl_contact_validation_begin(&initiator, CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(&initiator, MIG_A, SESS,
                                                 CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_begin(&responder, CHALLENGE_B), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(&responder, MIG_A, SESS,
                                                 CHALLENGE_B), MCL_LINK_OK);

    /* Still not moved: validation proves reachability, commit moves the contact. */
    CHECK_STATUS(mcl_contact_active_transport(&initiator, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_AP);

    CHECK_STATUS(mcl_contact_commit_begin(&initiator), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_confirm(&initiator, MIG_A, SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_begin(&responder), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_confirm(&responder, MIG_A, SESS), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_active_transport(&initiator, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_BLE);
    CHECK_TRUE(initiator.migration_count == 1u);
    CHECK_STATUS(mcl_contact_link_state(&initiator, &link_state), MCL_LINK_OK);
    CHECK_TRUE(link_state == MCL_LINK_STATE_ESTABLISHED);

    /* The reference learned acoustically survives the move: same contact. */
    CHECK_TRUE(initiator.peer_ref == 0xB2B2B2B2u);
}

/*
 * The defect migration_ref exists to prevent. An offer times out, the same
 * offer is retried, and the acceptance of the FIRST one arrives late. Transport
 * and profile are identical across the retry, so without a transaction
 * correlator the stale acceptance is indistinguishable from the live one and
 * the peer migrates on the strength of an abandoned transaction.
 */
static void test_stale_acceptance_refused(void)
{
    mcl_contact_t c;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00Du, 5u), MCL_LINK_OK);
    /* Caller-driven expiry: the library has no clock, and validity is retained
     * for exactly this. */
    CHECK_TRUE(c.pending_validity == 5u);
    CHECK_STATUS(mcl_contact_abandon_migration(&c), MCL_LINK_OK);

    /* Retry: same transport, same profile, new transaction. */
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_B, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00Du, 5u), MCL_LINK_OK);

    /* The late acceptance of the abandoned offer must not be honoured. */
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u, SESS),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_OFFERED);

    /* The current transaction still completes normally afterwards. */
    CHECK_STATUS(mcl_contact_agree(&c, MIG_B, MCL_CONTACT_TRANSPORT_BLE, 1u, SESS),
                 MCL_LINK_OK);

    /* A stale migration_ref must also be refused on every later message. */
    CHECK_STATUS(mcl_contact_validation_begin(&c, CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(&c, MIG_A, SESS, CHALLENGE_A),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_STATUS(mcl_contact_validation_response(&c, MIG_B, SESS, CHALLENGE_A),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_begin(&c), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_confirm(&c, MIG_A, SESS),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_STATUS(mcl_contact_commit_confirm(&c, MIG_B, SESS), MCL_LINK_OK);
}

/*
 * Path validation. A candidate transport that never answers must not become the
 * active transport, and failing to validate must leave the contact working
 * where it already was.
 */
static void test_path_validation(void)
{
    mcl_contact_t c;
    uint8_t transport = 0u;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_IP,
                                          1u, 0xE0u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u, SESS),
                 MCL_LINK_OK);

    /* Committing an unvalidated path is the defect this state exists to stop. */
    CHECK_STATUS(mcl_contact_commit_begin(&c), MCL_LINK_ERR_INVALID_STATE);

    CHECK_STATUS(mcl_contact_validation_begin(&c, CHALLENGE_A), MCL_LINK_OK);

    /* A wrong echo is not a valid path proof. CHALLENGE_B differs from
     * CHALLENGE_A in its final byte only, so a comparison that stopped early or
     * compared a prefix would wrongly accept it. */
    CHECK_STATUS(mcl_contact_validation_response(&c, MIG_A, SESS, CHALLENGE_B),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    /* Right challenge, wrong session. */
    CHECK_STATUS(mcl_contact_validation_response(&c, MIG_A, SESS + 1u, CHALLENGE_A),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);

    /* Still committing must be impossible. */
    CHECK_STATUS(mcl_contact_commit_begin(&c), MCL_LINK_ERR_INVALID_STATE);

    /* Validation never completes: abandon and stay where the contact works. */
    CHECK_STATUS(mcl_contact_abandon_migration(&c), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_AP);
    CHECK_TRUE(c.migration_count == 0u);
    CHECK_TRUE(c.session_valid == 0u);

    /* The abandoned session reference must not be usable afterwards. */
    CHECK_STATUS(mcl_contact_validation_begin(&c, CHALLENGE_A), MCL_LINK_ERR_INVALID_STATE);
}

/*
 * Simultaneous offers. Two autonomous machines will eventually offer at the
 * same instant; if each merely rejects the other, both wait forever. This is a
 * deadlock, not a refusal, and it must resolve deterministically without
 * another round trip.
 */
static void test_simultaneous_offer_glare(void)
{
    mcl_contact_t low;
    mcl_contact_t high;
    mcl_contact_collision_t outcome = 0u;

    /* Both offer at once. The keys are (source_ref, migration_ref). */
    CHECK_STATUS(mcl_contact_begin(&low, MCL_CONTACT_ROLE_INITIATOR,
                                   0x11111111u, MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_begin(&high, MCL_CONTACT_ROLE_RESPONDER,
                                   0x99999999u, MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_record_offer(&low, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0x1u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&high, MIG_B, MCL_CONTACT_TRANSPORT_IP,
                                          2u, 0x2u, 30u), MCL_LINK_OK);

    /* Each sees the other's offer while its own is outstanding. */
    CHECK_STATUS(mcl_contact_resolve_offer_collision(&low, 0x99999999u, MIG_B,
                                                     MCL_CONTACT_TRANSPORT_IP, 2u,
                                                     0x2u, 30u, &outcome), MCL_LINK_OK);
    CHECK_TRUE(outcome == MCL_CONTACT_COLLISION_PEER_WINS);
    CHECK_TRUE(low.pending_transport == MCL_CONTACT_TRANSPORT_IP);
    CHECK_TRUE(low.pending_migration_ref == MIG_B);

    CHECK_STATUS(mcl_contact_resolve_offer_collision(&high, 0x11111111u, MIG_A,
                                                     MCL_CONTACT_TRANSPORT_BLE, 1u,
                                                     0x1u, 30u, &outcome), MCL_LINK_OK);
    CHECK_TRUE(outcome == MCL_CONTACT_COLLISION_LOCAL_WINS);
    CHECK_TRUE(high.pending_transport == MCL_CONTACT_TRANSPORT_IP);
    CHECK_TRUE(high.pending_migration_ref == MIG_B);

    /* Both converged on the same transaction, so the migration can proceed. */
    CHECK_TRUE(low.pending_migration_ref == high.pending_migration_ref);
    CHECK_TRUE(low.pending_transport == high.pending_transport);
    CHECK_TRUE(low.pending_profile == high.pending_profile);

    CHECK_STATUS(mcl_contact_agree(&low, MIG_B, MCL_CONTACT_TRANSPORT_IP, 2u, SESS),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&high, MIG_B, MCL_CONTACT_TRANSPORT_IP, 2u, SESS),
                 MCL_LINK_OK);
}

/* An exact key tie cannot be resolved, so both sides must abandon rather than
 * disagree about who controls the migration. */
static void test_glare_exact_tie_aborts(void)
{
    mcl_contact_t c;
    mcl_contact_collision_t outcome = 0u;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 0x42u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 1u, 30u), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_resolve_offer_collision(&c, 0x42u, MIG_A,
                                                     MCL_CONTACT_TRANSPORT_BLE, 1u,
                                                     1u, 30u, &outcome), MCL_LINK_OK);
    CHECK_TRUE(outcome == MCL_CONTACT_COLLISION_TIE_ABORT);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(c.pending_migration_ref == MCL_CONTACT_MIGRATION_NONE);

    /* A collision can only be resolved while our own offer is outstanding. */
    CHECK_STATUS(mcl_contact_resolve_offer_collision(&c, 0x1u, MIG_B,
                                                     MCL_CONTACT_TRANSPORT_BLE, 1u,
                                                     1u, 30u, &outcome),
                 MCL_LINK_ERR_INVALID_STATE);
}

/* The migration reference decides the tie when both peers share a source_ref. */
static void test_glare_migration_ref_breaks_tie(void)
{
    mcl_contact_t c;
    mcl_contact_collision_t outcome = 0u;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 0x42u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 1u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_resolve_offer_collision(&c, 0x42u, MIG_B,
                                                     MCL_CONTACT_TRANSPORT_IP, 3u,
                                                     7u, 30u, &outcome), MCL_LINK_OK);
    CHECK_TRUE(outcome == MCL_CONTACT_COLLISION_PEER_WINS);
    CHECK_TRUE(c.pending_transport == MCL_CONTACT_TRANSPORT_IP);
    CHECK_TRUE(c.pending_profile == 3u);
    CHECK_TRUE(c.pending_endpoint_token == 7u);
}

static void test_peer_ref_is_first_write_wins(void)
{
    mcl_contact_t c;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_set_peer_ref(&c, 0xAAu), MCL_LINK_OK);
    /* Idempotent: a repeated advertisement of the same reference is normal on a
     * lossy broadcast medium and must not be an error. */
    CHECK_STATUS(mcl_contact_set_peer_ref(&c, 0xAAu), MCL_LINK_OK);
    /* Changing it would let a peer, or a third party, redirect an established
     * correlation mid-contact. */
    CHECK_STATUS(mcl_contact_set_peer_ref(&c, 0xBBu), MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_TRUE(c.peer_ref == 0xAAu);
}

static void test_refusals(void)
{
    mcl_contact_t c;
    mcl_contact_collision_t outcome = 0u;
    uint8_t transport = 0u;

    CHECK_STATUS(mcl_contact_begin(NULL, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_set_peer_ref(NULL, 1u), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_record_offer(NULL, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u, 1u, 1u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_agree(NULL, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u, 1u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_validation_begin(NULL, CHALLENGE_A), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_validation_response(NULL, MIG_A, 1u, CHALLENGE_A),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_commit_begin(NULL), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_commit_confirm(NULL, MIG_A, 1u), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_abandon_migration(NULL), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_close(NULL), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_active_transport(NULL, NULL), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_link_state(NULL, NULL), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_resolve_offer_collision(NULL, 1u, MIG_A,
                                                     MCL_CONTACT_TRANSPORT_BLE, 1u, 1u, 1u,
                                                     &outcome), MCL_LINK_ERR_INVALID_ARGUMENT);

    /* Reserved values are never valid, so a zeroed field cannot pass. */
    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_RESERVED),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_begin(&c, (mcl_contact_role_t)7u, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_ERR_INVALID_ARGUMENT);

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_record_offer(&c, MCL_CONTACT_MIGRATION_NONE,
                                          MCL_CONTACT_TRANSPORT_BLE, 1u, 1u, 1u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_RESERVED,
                                          1u, 1u, 1u), MCL_LINK_ERR_INVALID_ARGUMENT);
    /* Same transport is adaptation, not migration. */
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_AP,
                                          1u, 1u, 1u), MCL_LINK_ERR_INVALID_ARGUMENT);

    /* Out-of-order entry into every later stage. */
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u, SESS),
                 MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_validation_begin(&c, CHALLENGE_A), MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_validation_response(&c, MIG_A, SESS, CHALLENGE_A),
                 MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_commit_begin(&c), MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_commit_confirm(&c, MIG_A, SESS), MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_abandon_migration(&c), MCL_LINK_ERR_INVALID_STATE);

    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00Du, 30u), MCL_LINK_OK);
    /* A second offer must not silently replace the outstanding one. */
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_B, MCL_CONTACT_TRANSPORT_IP,
                                          1u, 1u, 30u), MCL_LINK_ERR_INVALID_STATE);
    /* Session reference zero is reserved. */
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u,
                                   MCL_CONTACT_SESSION_NONE), MCL_LINK_ERR_INVALID_ARGUMENT);
    /* An acceptance must not redirect to an unoffered transport or profile. */
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u, SESS),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 9u, SESS),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);

    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u, SESS),
                 MCL_LINK_OK);
    /* Agreeing twice would restart a transaction already in flight. */
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u, SESS),
                 MCL_LINK_ERR_INVALID_STATE);

    CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_AP);
}

static void test_closed_contact_is_inert(void)
{
    mcl_contact_t c;
    uint8_t transport = 0u;
    mcl_link_state_t link_state = 0u;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 5u,
                                   MCL_CONTACT_TRANSPORT_BLE), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_IP,
                                          1u, 9u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u, SESS),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_close(&c), MCL_LINK_OK);

    /* A closed contact cannot be revived by a peer arriving with references it
     * observed earlier. */
    CHECK_STATUS(mcl_contact_validation_begin(&c, CHALLENGE_A), MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_commit_confirm(&c, MIG_A, SESS), MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_AP, 1u, 1u, 1u),
                 MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_set_peer_ref(&c, 7u), MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_ERR_INVALID_STATE);
    CHECK_TRUE(c.session_valid == 0u);
    CHECK_STATUS(mcl_contact_link_state(&c, &link_state), MCL_LINK_OK);
    CHECK_TRUE(link_state == MCL_LINK_STATE_CLOSED);
}

/* Repeated migration must be stable rather than accumulating state. */
static void test_repeated_migration(void)
{
    mcl_contact_t c;
    unsigned i;
    uint8_t transport = 0u;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    for (i = 0u; i < 100u; ++i) {
        const uint8_t target = ((i & 1u) == 0u)
            ? MCL_CONTACT_TRANSPORT_BLE
            : MCL_CONTACT_TRANSPORT_IP;
        run_full_migration(&c, MIG_A + i, target, 1u, SESS + i);
        CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_OK);
        CHECK_TRUE(transport == target);
    }
    CHECK_TRUE(c.migration_count == 100u);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(c.pending_migration_ref == MCL_CONTACT_MIGRATION_NONE);
}

/*
 * Documents the LIMIT of this module rather than a capability of it.
 *
 * Everything above crossed an observable medium in the clear. An attacker that
 * heard the acoustic contact holds the migration reference, the session
 * reference and the endpoint token, and can answer a challenge it receives on
 * the candidate path. It therefore completes this sequence exactly as an honest
 * peer would.
 *
 * Asserted deliberately so that a green continuity suite is never mistaken for
 * a security property. The property that would defeat this observer is
 * cryptographic contact binding, which MCL does not have. See
 * mcl-link/research/contact-continuity-experiment.md.
 */
static void test_observer_is_indistinguishable(void)
{
    mcl_contact_t victim;
    uint32_t observed_migration_ref;
    uint32_t observed_session_ref;

    CHECK_STATUS(mcl_contact_begin(&victim, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&victim, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xE0u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&victim, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                   1u, SESS), MCL_LINK_OK);

    observed_migration_ref = MIG_A;
    observed_session_ref = SESS;

    CHECK_STATUS(mcl_contact_validation_begin(&victim, CHALLENGE_A), MCL_LINK_OK);
    /* The observer echoes a challenge it simply received, using references it
     * simply overheard. */
    CHECK_STATUS(mcl_contact_validation_response(&victim, observed_migration_ref,
                                                 observed_session_ref, CHALLENGE_A),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_begin(&victim), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_confirm(&victim, observed_migration_ref,
                                            observed_session_ref), MCL_LINK_OK);

    CHECK_TRUE(victim.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(victim.migration_count == 1u);
}

int main(void)
{
    test_acoustic_to_ble_migration();
    test_stale_acceptance_refused();
    test_path_validation();
    test_simultaneous_offer_glare();
    test_glare_exact_tie_aborts();
    test_glare_migration_ref_breaks_tie();
    test_peer_ref_is_first_write_wins();
    test_refusals();
    test_closed_contact_is_inert();
    test_repeated_migration();
    test_observer_is_indistinguishable();

    printf("mcl_link_contact: %d checks passed\n", g_checks);
    printf("NOTE: path validation proves reachability, not identity.\n");
    return 0;
}
