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

/*
 * ONCE COMMIT IS SENT, ROLLING BACK IS A CLAIM THIS MACHINE CANNOT MAKE.
 *
 * From COMMITTING, "I did not hear CONFIRM" does not mean "the peer did not
 * commit". If the peer received the commit it is already on the new transport,
 * and returning to the old one produces exactly the split that
 * test_lost_confirm_recovers_by_retransmission exists to repair. An earlier
 * revision permitted this while documenting the divergence it caused.
 */
static void test_commit_is_irrevocable_once_sent(void)
{
    mcl_contact_t c;
    uint8_t transport = 0u;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);

    /* Abandonment is safe at every stage before the commit is sent, because
     * the peer cannot have switched without having received one. */
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_abandon_migration(&c), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u,
                                   SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_abandon_migration(&c), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u,
                                   SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_begin(&c, CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_abandon_migration(&c), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u,
                                   SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_begin(&c, CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(&c, MIG_A, SESS, CHALLENGE_A),
                 MCL_LINK_OK);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_VALIDATED);
    CHECK_STATUS(mcl_contact_abandon_migration(&c), MCL_LINK_OK);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_ACTIVE);

    /* Now go all the way to COMMITTING and try again. */
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_B, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00D0002u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MIG_B, MCL_CONTACT_TRANSPORT_BLE, 1u,
                                   SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_begin(&c, CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(&c, MIG_B, SESS, CHALLENGE_A),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_begin(&c), MCL_LINK_OK);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_COMMITTING);

    CHECK_STATUS(mcl_contact_abandon_migration(&c), MCL_LINK_ERR_INVALID_STATE);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_COMMITTING);
    CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_AP);

    /* The two honest outcomes. Retransmitting until CONFIRM arrives: */
    CHECK_STATUS(mcl_contact_commit_confirm(&c, MIG_B, SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_BLE);

    /* ...or giving up on the CONTACT rather than on the migration. */
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_IP,
                                          1u, 0xD00D0003u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u,
                                   SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_begin(&c, CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(&c, MIG_A, SESS, CHALLENGE_A),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_begin(&c), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_abandon_migration(&c), MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_close(&c), MCL_LINK_OK);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_CLOSED);
}

/*
 * The receiving peer goes from VALIDATED straight to ACTIVE and never occupies
 * COMMITTING, which is what makes the rollback rule above enforceable: the
 * state then means exactly one thing.
 */
static void test_commit_accept_never_enters_committing(void)
{
    mcl_contact_t c;
    uint8_t transport = 0u;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_RESPONDER, 2u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u,
                                   SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_begin(&c, CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(&c, MIG_A, SESS, CHALLENGE_A),
                 MCL_LINK_OK);

    /* A commit naming another transaction leaves the contact in VALIDATED with
     * the old transport intact, not stranded partway through a switch. */
    CHECK_STATUS(mcl_contact_commit_accept(&c, MIG_B, SESS),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_VALIDATED);
    CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_AP);

    CHECK_STATUS(mcl_contact_commit_accept(&c, MIG_A, SESS + 1u),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_VALIDATED);

    CHECK_STATUS(mcl_contact_commit_accept(&c, MIG_A, SESS), MCL_LINK_OK);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_BLE);
    CHECK_TRUE(c.migration_count == 1u);

    /* Accepting a commit for a path that was never validated is refused. */
    CHECK_STATUS(mcl_contact_commit_accept(&c, MIG_A, SESS),
                 MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_commit_accept(NULL, MIG_A, SESS),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
}

/* Two peers meet acoustically and continue over BLE, with no cryptography
 * anywhere. This is the flow the whole project exists to make possible. */
static void test_acoustic_to_ble_migration(void)
{
    mcl_contact_t initiator;
    mcl_contact_t responder;
    uint8_t transport = 0u;
    uint8_t control_transport = 0u;
    uint8_t quiesced = 0u;
    uint8_t migrating = 0u;

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

    /*
     * The contact now spans TWO transports, and which one a frame goes on
     * depends on what the frame is. Ordinary traffic still belongs on the
     * acoustic path; the handoff controls belong on the candidate, because
     * their whole purpose is to establish it.
     */
    CHECK_STATUS(mcl_contact_migration_active(&initiator, &migrating), MCL_LINK_OK);
    CHECK_TRUE(migrating == 1u);
    CHECK_STATUS(mcl_contact_control_transport(&initiator, &control_transport),
                 MCL_LINK_OK);
    CHECK_TRUE(control_transport == MCL_CONTACT_TRANSPORT_BLE);
    CHECK_STATUS(mcl_contact_data_transport(&initiator, &transport, &quiesced),
                 MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_AP);
    CHECK_TRUE(quiesced == 0u);

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

    /*
     * Between COMMIT and CONFIRM the peers genuinely disagree about which
     * transport carries the contact, and the sender cannot find out. Ordinary
     * traffic is quiesced for that window rather than being sent onto a medium
     * the peer may already have left.
     */
    CHECK_STATUS(mcl_contact_data_transport(&initiator, &transport, &quiesced),
                 MCL_LINK_OK);
    CHECK_TRUE(quiesced == 1u);

    CHECK_STATUS(mcl_contact_commit_confirm(&initiator, MIG_A, SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_begin(&responder), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_confirm(&responder, MIG_A, SESS), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_active_transport(&initiator, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_BLE);
    CHECK_TRUE(initiator.migration_count == 1u);
    CHECK_STATUS(mcl_contact_migration_active(&initiator, &migrating), MCL_LINK_OK);
    CHECK_TRUE(migrating == 0u);
    CHECK_STATUS(mcl_contact_data_transport(&initiator, &transport, &quiesced),
                 MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_BLE);
    CHECK_TRUE(quiesced == 0u);
    /* With nothing outstanding, a retransmitted COMMIT arrives where the
     * contact now lives. */
    CHECK_STATUS(mcl_contact_control_transport(&initiator, &control_transport),
                 MCL_LINK_OK);
    CHECK_TRUE(control_transport == MCL_CONTACT_TRANSPORT_BLE);

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

    /*
     * The session reference SURVIVES the failed migration. It names the
     * continuing logical contact, and the contact did not fail -- only the
     * attempt to move it did. A peer that missed the abandonment must still be
     * able to tell this contact from a new one.
     */
    CHECK_TRUE(c.session_valid == 1u);
    CHECK_TRUE(c.session_ref == SESS);

    /* The abandoned TRANSACTION is gone, so the sequence cannot resume. */
    CHECK_STATUS(mcl_contact_validation_begin(&c, CHALLENGE_A), MCL_LINK_ERR_INVALID_STATE);
    CHECK_TRUE(c.pending_migration_ref == MCL_CONTACT_MIGRATION_NONE);

    /* A later migration must reuse the same session reference, not choose a
     * new one. */
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_B, MCL_CONTACT_TRANSPORT_IP,
                                          1u, 0xE0u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MIG_B, MCL_CONTACT_TRANSPORT_IP, 1u,
                                   SESS + 1u), MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_STATUS(mcl_contact_agree(&c, MIG_B, MCL_CONTACT_TRANSPORT_IP, 1u,
                                   SESS), MCL_LINK_OK);
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
    CHECK_STATUS(mcl_contact_migration_active(NULL, NULL), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_control_transport(NULL, NULL), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_data_transport(NULL, NULL, NULL), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_challenge_repeat(NULL, MIG_A, SESS, CHALLENGE_A, NULL),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
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
    /*
     * Agreeing twice with the SAME transaction is a retransmitted acceptance,
     * which is ordinary on a lossy medium: accepted, and it changes nothing.
     * An earlier revision refused it, which turned a dropped frame into a
     * failed migration.
     */
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u, SESS),
                 MCL_LINK_OK);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_AGREED);
    /* Agreeing again to something DIFFERENT is still refused. */
    CHECK_STATUS(mcl_contact_agree(&c, MIG_B, MCL_CONTACT_TRANSPORT_BLE, 1u, SESS),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_STATUS(mcl_contact_agree(&c, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u, SESS),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);

    CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_AP);
}

static void test_closed_contact_is_inert(void)
{
    mcl_contact_t c;
    uint8_t transport = 0u;
    uint8_t quiesced = 0u;

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
    /* A closed contact has no transport for anything. */
    CHECK_STATUS(mcl_contact_control_transport(&c, &transport), MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_data_transport(&c, &transport, &quiesced),
                 MCL_LINK_ERR_INVALID_STATE);
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
        /*
         * The SAME session reference every hop. It names the continuing logical
         * contact, so it must survive each move rather than rotating with it.
         * An earlier revision passed SESS + i here, which quietly asserted the
         * opposite model.
         */
        run_full_migration(&c, MIG_A + i, target, 1u, SESS);
        CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_OK);
        CHECK_TRUE(transport == target);
    }
    CHECK_TRUE(c.migration_count == 100u);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(c.pending_migration_ref == MCL_CONTACT_MIGRATION_NONE);
}

/*
 * A LOST CONFIRM MUST NOT LEAVE THE PEERS ON DIFFERENT TRANSPORTS.
 *
 * A sends COMMIT. B accepts it, completes the migration and replies CONFIRM.
 * The CONFIRM is lost. B is now on the new transport; A is still COMMITTING,
 * eventually gives up, and correctly returns to the old one. They now disagree
 * about which transport carries the contact, and no adversary was involved --
 * one dropped frame is enough.
 *
 * An abort message cannot repair this, because the peers are no longer on a
 * common transport to abort over. Retransmission can: A resends COMMIT, and B
 * must be able to answer it a second time. This test drives both peers through
 * that, and asserts they finish on the same transport.
 */
static void test_lost_confirm_recovers_by_retransmission(void)
{
    mcl_contact_t a;
    mcl_contact_t b;
    uint8_t a_transport = 0u;
    uint8_t b_transport = 0u;
    uint8_t reconfirm = 0u;

    CHECK_STATUS(mcl_contact_begin(&a, MCL_CONTACT_ROLE_INITIATOR, 0xA0u,
                                   MCL_CONTACT_TRANSPORT_BLE), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_begin(&b, MCL_CONTACT_ROLE_RESPONDER, 0xB0u,
                                   MCL_CONTACT_TRANSPORT_BLE), MCL_LINK_OK);

    /* Both reach VALIDATED on the candidate. */
    CHECK_STATUS(mcl_contact_record_offer(&a, MIG_A, MCL_CONTACT_TRANSPORT_IP,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&b, MIG_A, MCL_CONTACT_TRANSPORT_IP,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&a, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u, SESS),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&b, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u, SESS),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_begin(&a, CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_begin(&b, CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(&a, MIG_A, SESS, CHALLENGE_A),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(&b, MIG_A, SESS, CHALLENGE_A),
                 MCL_LINK_OK);

    /* A sends COMMIT. B receives it and completes, replying CONFIRM. */
    CHECK_STATUS(mcl_contact_commit_begin(&a), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_accept(&b, MIG_A, SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_active_transport(&b, &b_transport), MCL_LINK_OK);
    CHECK_TRUE(b_transport == MCL_CONTACT_TRANSPORT_IP);

    /* The CONFIRM never arrives. A is still committing on the old transport. */
    CHECK_TRUE(a.state == MCL_CONTACT_STATE_COMMITTING);
    CHECK_STATUS(mcl_contact_active_transport(&a, &a_transport), MCL_LINK_OK);
    CHECK_TRUE(a_transport == MCL_CONTACT_TRANSPORT_BLE);

    /*
     * A cannot escape by abandoning: the peer may already have committed, and
     * that is precisely the case here. Rolling back would make this divergence
     * permanent instead of temporary.
     */
    CHECK_STATUS(mcl_contact_abandon_migration(&a), MCL_LINK_ERR_INVALID_STATE);
    CHECK_TRUE(a.state == MCL_CONTACT_STATE_COMMITTING);

    /* A retransmits COMMIT. B has cleared its pending transaction, so the
     * ordinary path refuses it -- which is correct, and is exactly why the
     * retransmission case has to be asked separately. */
    CHECK_STATUS(mcl_contact_commit_accept(&b, MIG_A, SESS),
                 MCL_LINK_ERR_INVALID_STATE);

    CHECK_STATUS(mcl_contact_commit_repeat(&b, MIG_A, SESS, &reconfirm),
                 MCL_LINK_OK);
    CHECK_TRUE(reconfirm == 1u);

    /* Re-confirming changed nothing on B. */
    CHECK_TRUE(b.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(b.migration_count == 1u);
    CHECK_STATUS(mcl_contact_active_transport(&b, &b_transport), MCL_LINK_OK);
    CHECK_TRUE(b_transport == MCL_CONTACT_TRANSPORT_IP);

    /* The repeated CONFIRM reaches A, which completes. */
    CHECK_STATUS(mcl_contact_commit_confirm(&a, MIG_A, SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_active_transport(&a, &a_transport), MCL_LINK_OK);
    CHECK_TRUE(a_transport == b_transport);
}

/*
 * The retransmission answer must be narrow. It exists to repair one dropped
 * frame, not to become a way of making a settled contact move again, and not
 * to answer a transaction this contact never completed.
 */
static void test_commit_repeat_is_narrow(void)
{
    mcl_contact_t c;
    uint8_t reconfirm = 0xFFu;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_RESPONDER, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);

    /* Nothing has completed yet. */
    CHECK_STATUS(mcl_contact_commit_repeat(&c, MIG_A, SESS, &reconfirm),
                 MCL_LINK_OK);
    CHECK_TRUE(reconfirm == 0u);

    run_full_migration(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE, 1u, SESS);

    /* The transaction that completed is answered. */
    CHECK_STATUS(mcl_contact_commit_repeat(&c, MIG_A, SESS, &reconfirm),
                 MCL_LINK_OK);
    CHECK_TRUE(reconfirm == 1u);

    /* A different transaction is not, even with the right session. Otherwise a
     * stale COMMIT from an abandoned attempt would be confirmed. */
    CHECK_STATUS(mcl_contact_commit_repeat(&c, MIG_B, SESS, &reconfirm),
                 MCL_LINK_OK);
    CHECK_TRUE(reconfirm == 0u);

    /* Nor a different contact's session carrying the right transaction. */
    CHECK_STATUS(mcl_contact_commit_repeat(&c, MIG_A, SESS + 1u, &reconfirm),
                 MCL_LINK_OK);
    CHECK_TRUE(reconfirm == 0u);

    /* A newer migration replaces the memory: only the most recent completed
     * transaction is answerable, so an old COMMIT cannot be revived. */
    run_full_migration(&c, MIG_B, MCL_CONTACT_TRANSPORT_IP, 1u, SESS);
    CHECK_STATUS(mcl_contact_commit_repeat(&c, MIG_A, SESS, &reconfirm),
                 MCL_LINK_OK);
    CHECK_TRUE(reconfirm == 0u);
    CHECK_STATUS(mcl_contact_commit_repeat(&c, MIG_B, SESS, &reconfirm),
                 MCL_LINK_OK);
    CHECK_TRUE(reconfirm == 1u);

    /* While a migration is in progress the ordinary state machine owns the
     * COMMIT, so the retransmission path must stay silent. */
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A + 9u,
                                          MCL_CONTACT_TRANSPORT_BLE, 1u,
                                          0xD00D0009u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_repeat(&c, MIG_B, SESS, &reconfirm),
                 MCL_LINK_OK);
    CHECK_TRUE(reconfirm == 0u);

    /* A closed contact answers nothing. */
    CHECK_STATUS(mcl_contact_close(&c), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_repeat(&c, MIG_B, SESS, &reconfirm),
                 MCL_LINK_OK);
    CHECK_TRUE(reconfirm == 0u);

    CHECK_STATUS(mcl_contact_commit_repeat(NULL, MIG_B, SESS, &reconfirm),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_commit_repeat(&c, MIG_B, SESS, NULL),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
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

/*
 * EVERY CONTROL MUST SURVIVE BEING SENT TWICE.
 *
 * On a lossy medium the only repair is retransmission, so a peer that handles
 * only the first copy of each control works in a harness and strands a contact
 * the first time a radio drops a frame. The lost CONFIRM was fixed earlier;
 * this is the same defect at every other step.
 *
 * The one that actually bites is the lost PATH_RESPONSE. B echoes a challenge,
 * reaching VALIDATED, and the echo is lost. A retransmits PATH_CHALLENGE --
 * correctly. B has left AGREED, so a state machine that accepts a challenge
 * only in AGREED refuses it, and the migration dies with both peers behaving
 * correctly and no adversary present.
 */
static void test_every_control_survives_duplication(void)
{
    mcl_contact_t a;
    mcl_contact_t b;
    uint8_t reecho = 0u;
    uint8_t reconfirm = 0u;

    CHECK_STATUS(mcl_contact_begin(&a, MCL_CONTACT_ROLE_INITIATOR, 0xA0u,
                                   MCL_CONTACT_TRANSPORT_BLE), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_begin(&b, MCL_CONTACT_ROLE_RESPONDER, 0xB0u,
                                   MCL_CONTACT_TRANSPORT_BLE), MCL_LINK_OK);

    /* Duplicate OFFER: identical is accepted and changes nothing. */
    CHECK_STATUS(mcl_contact_record_offer(&a, MIG_A, MCL_CONTACT_TRANSPORT_IP,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&a, MIG_A, MCL_CONTACT_TRANSPORT_IP,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);
    CHECK_TRUE(a.state == MCL_CONTACT_STATE_OFFERED);
    /* Same reference, different content: a second offer wearing the first
     * one's name, and there is no correct way to merge the two. */
    CHECK_STATUS(mcl_contact_record_offer(&a, MIG_A, MCL_CONTACT_TRANSPORT_IP,
                                          1u, 0xD00D0002u, 30u),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_STATUS(mcl_contact_record_offer(&b, MIG_A, MCL_CONTACT_TRANSPORT_IP,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);

    /* Duplicate ACCEPT: accepted, and it must not move anything backwards. */
    CHECK_STATUS(mcl_contact_agree(&a, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u, SESS),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&a, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u, SESS),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&b, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u, SESS),
                 MCL_LINK_OK);

    /* B is challenged and echoes; its PATH_RESPONSE is then lost. */
    CHECK_STATUS(mcl_contact_validation_begin(&b, CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(&b, MIG_A, SESS, CHALLENGE_A),
                 MCL_LINK_OK);
    CHECK_TRUE(b.state == MCL_CONTACT_STATE_VALIDATED);

    /*
     * A retransmits the SAME challenge. B must answer it again. Before
     * mcl_contact_challenge_repeat existed this was refused and the migration
     * ended here.
     */
    CHECK_STATUS(mcl_contact_challenge_repeat(&b, MIG_A, SESS, CHALLENGE_A,
                                              &reecho), MCL_LINK_OK);
    CHECK_TRUE(reecho == 1u);
    CHECK_TRUE(b.state == MCL_CONTACT_STATE_VALIDATED);

    /*
     * A DIFFERENT challenge under the same transaction is answered with
     * silence. An honest retransmission repeats itself; this is either a bug
     * or someone asking to have bytes of their choosing echoed back on a path
     * already validated.
     */
    CHECK_STATUS(mcl_contact_challenge_repeat(&b, MIG_A, SESS, CHALLENGE_B,
                                              &reecho), MCL_LINK_OK);
    CHECK_TRUE(reecho == 0u);
    /* Wrong transaction: also silence. */
    CHECK_STATUS(mcl_contact_challenge_repeat(&b, MIG_B, SESS, CHALLENGE_A,
                                              &reecho), MCL_LINK_OK);
    CHECK_TRUE(reecho == 0u);

    /* A completes its own half, then receives a duplicate PATH_RESPONSE. */
    CHECK_STATUS(mcl_contact_validation_begin(&a, CHALLENGE_A), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(&a, MIG_A, SESS, CHALLENGE_A),
                 MCL_LINK_OK);
    CHECK_TRUE(a.state == MCL_CONTACT_STATE_VALIDATED);
    CHECK_STATUS(mcl_contact_validation_response(&a, MIG_A, SESS, CHALLENGE_A),
                 MCL_LINK_OK);
    CHECK_TRUE(a.state == MCL_CONTACT_STATE_VALIDATED);
    /* A duplicate echoing something else is not a duplicate. */
    CHECK_STATUS(mcl_contact_validation_response(&a, MIG_A, SESS, CHALLENGE_B),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);

    /* A late duplicate ACCEPT must not drag either peer back to AGREED. */
    CHECK_STATUS(mcl_contact_agree(&a, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u, SESS),
                 MCL_LINK_OK);
    CHECK_TRUE(a.state == MCL_CONTACT_STATE_VALIDATED);

    /* COMMIT and CONFIRM, then duplicates of both. */
    CHECK_STATUS(mcl_contact_commit_accept(&b, MIG_A, SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_repeat(&b, MIG_A, SESS, &reconfirm),
                 MCL_LINK_OK);
    CHECK_TRUE(reconfirm == 1u);

    CHECK_STATUS(mcl_contact_commit_begin(&a), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_confirm(&a, MIG_A, SESS), MCL_LINK_OK);
    CHECK_TRUE(a.state == MCL_CONTACT_STATE_ACTIVE);
    /* Duplicate CONFIRM of the migration that just completed. */
    CHECK_STATUS(mcl_contact_commit_confirm(&a, MIG_A, SESS), MCL_LINK_OK);
    CHECK_TRUE(a.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(a.migration_count == 1u);
    /* A CONFIRM from ACTIVE naming anything else is confirming a migration
     * this machine never agreed to. */
    CHECK_STATUS(mcl_contact_commit_confirm(&a, MIG_B, SESS),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_TRUE(a.migration_count == 1u);
}

/*
 * Profile zero is reserved in every transport's profile registry so that an
 * uninitialised field names no profile. All four registries said so and nothing
 * enforced it, which meant a zeroed offer was a legal migration proposal.
 *
 * Which profiles are ACCEPTABLE stays deployment policy. That zero is not a
 * profile at all does not.
 */
static void test_reserved_profile_is_refused(void)
{
    mcl_contact_t c;
    mcl_contact_collision_t outcome;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 0x11111111u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          MCL_CONTACT_PROFILE_RESERVED,
                                          0xD00D0001u, 30u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(c.pending_migration_ref == MCL_CONTACT_MIGRATION_NONE);

    /* A well-formed offer still works, so the check refuses the reserved value
     * rather than the field. */
    CHECK_STATUS(mcl_contact_record_offer(&c, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00D0001u, 30u), MCL_LINK_OK);

    /* And a colliding peer offer naming it is not a contender either: adopting
     * it on PEER_WINS would install a profile that names nothing. */
    CHECK_STATUS(mcl_contact_resolve_offer_collision(&c, 0x99999999u, MIG_B,
                                                     MCL_CONTACT_TRANSPORT_IP,
                                                     MCL_CONTACT_PROFILE_RESERVED,
                                                     0xD00D0002u, 30u, &outcome),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_OFFERED);
    CHECK_TRUE(c.pending_migration_ref == MIG_A);
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
    test_lost_confirm_recovers_by_retransmission();
    test_commit_repeat_is_narrow();
    test_commit_is_irrevocable_once_sent();
    test_commit_accept_never_enters_committing();
    test_every_control_survives_duplication();
    test_observer_is_indistinguishable();
    test_reserved_profile_is_refused();

    printf("mcl_link_contact: %d checks passed\n", g_checks);
    printf("NOTE: path validation proves reachability, not identity.\n");
    return 0;
}
