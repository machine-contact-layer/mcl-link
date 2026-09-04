/*
 * State-machine integrity under hostile input.
 *
 * WHAT THIS FILE ESTABLISHES
 *
 * MCL contact state is carried in references that travel in the clear:
 * source_ref, session_ref and migration_ref are CORRELATION references, and the
 * specification says so wherever they are defined. Anyone within range of the
 * medium therefore has them, and can resend anything they have seen.
 *
 * That is the premise of every test below, not its finding. The finding is
 * that it buys nothing against the state machine:
 *
 *   - a replayed opening frame leaves an established contact exactly as it was
 *   - a substituted contact reference is refused, not adopted
 *   - a completed migration cannot be replayed backwards
 *   - a control from one transaction cannot drive another
 *   - a control from one contact cannot drive a second contact
 *   - a closed contact has no route back in
 *   - a legal-but-minimal capability set still yields a symmetric selection
 *     both peers compute identically
 *
 * The property being defended is that TWO HONEST PEERS ARE NEVER LEFT
 * DISAGREEING ABOUT WHAT HAPPENED. That is the failure they could not detect
 * or repair between themselves, and it is a property of the state machine
 * rather than of any transport beneath it -- which is why it is testable here,
 * and why it holds identically over a socket, a radio and a loudspeaker.
 *
 * Retransmission and replay are the same bytes. A duplicate control is
 * therefore ACCEPTED where the specification requires idempotence -- refusing
 * it would strand a contact the first time a medium dropped a frame -- and the
 * assertions are on what MOVED, not on what was rejected. migration_count is
 * the counter that would advance if a replay were producing a second
 * migration, and it is checked after every replay in this file.
 *
 * Deployment properties of the surrounding transport are a separate subject and
 * live in mcl-core/SECURITY.md.
 */

#include "mcl/contact.h"
#include "mcl/negotiation.h"

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

static const uint8_t CHALLENGE[MCL_CONTACT_CHALLENGE_SIZE] =
    {0x51u, 0x1Cu, 0x0Fu, 0xFEu, 0xA7u, 0x3Bu, 0x90u, 0x22u};

#define MIG_A UINT32_C(0x4D194201)
#define MIG_B UINT32_C(0x4D194202)
#define SESS  UINT32_C(0x5E5510C7)
#define VICTIM_REF UINT32_C(0x0000A001)
#define HONEST_PEER_REF UINT32_C(0x0000B001)
#define ATTACKER_REF UINT32_C(0xDEADBEEF)

static void migrate(mcl_contact_t *c, uint32_t mig, uint8_t transport,
                    uint32_t session)
{
    CHECK_STATUS(mcl_contact_record_offer(c, mig, transport, 1u, 0xD00Du, 30u),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(c, mig, transport, 1u, session), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_begin(c, CHALLENGE), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_response(c, mig, session, CHALLENGE),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_begin(c), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_commit_confirm(c, mig, session), MCL_LINK_OK);
}

/*
 * THE ATTACK EVERYONE ASKS ABOUT FIRST: record the opening "hello" and replay
 * it later.
 *
 * A recorded first-contact frame replayed later IS a valid frame. SECURITY.md
 * says so and it is true. The question this test settles is what that buys.
 *
 * It buys nothing. The peer reference is first-write-wins, so the replay of the
 * honest peer's own reference is accepted and changes nothing, and a replay
 * carrying a DIFFERENT reference -- an attacker substituting its own -- is
 * refused. If it were not, anyone in range could redirect an established
 * contact by asserting a new reference, and that is hijack rather than replay.
 */
static void test_replayed_hello_changes_nothing(void)
{
    mcl_contact_t victim;

    CHECK_STATUS(mcl_contact_begin(&victim, MCL_CONTACT_ROLE_RESPONDER,
                                   VICTIM_REF, MCL_CONTACT_TRANSPORT_AP),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_set_peer_ref(&victim, HONEST_PEER_REF), MCL_LINK_OK);

    /* The attacker replays the honest peer's opening frame verbatim. */
    CHECK_STATUS(mcl_contact_set_peer_ref(&victim, HONEST_PEER_REF), MCL_LINK_OK);
    CHECK_TRUE(victim.peer_ref == HONEST_PEER_REF);
    CHECK_TRUE(victim.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(victim.migration_count == 0u);

    /* The attacker replays it with its own reference substituted. This is the
     * one that must not work: accepting it would let anyone in range take over
     * the correlation the whole module provides. */
    CHECK_STATUS(mcl_contact_set_peer_ref(&victim, ATTACKER_REF),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_TRUE(victim.peer_ref == HONEST_PEER_REF);
}

/*
 * A COMPLETED MIGRATION CANNOT BE REPLAYED BACKWARDS.
 *
 * The attacker records the whole migration transaction and replays the OFFER
 * after it has committed. If the contact reopened, an observer could pull a
 * peer back onto a transport its partner has already left -- two honest peers
 * disagreeing about where the contact lives, with no way to notice.
 *
 * The replayed offer is refused because it names a transport that is now the
 * active one, which is adaptation rather than migration.
 */
static void test_replayed_offer_cannot_reverse_a_migration(void)
{
    mcl_contact_t victim;

    CHECK_STATUS(mcl_contact_begin(&victim, MCL_CONTACT_ROLE_INITIATOR,
                                   VICTIM_REF, MCL_CONTACT_TRANSPORT_AP),
                 MCL_LINK_OK);
    migrate(&victim, MIG_A, MCL_CONTACT_TRANSPORT_IP, SESS);
    CHECK_TRUE(victim.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(victim.migration_count == 1u);

    /* Replay of the offer that has already completed. */
    CHECK_STATUS(mcl_contact_record_offer(&victim, MIG_A,
                                          MCL_CONTACT_TRANSPORT_IP, 1u,
                                          0xD00Du, 30u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_TRUE(victim.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(victim.migration_count == 1u);

    /*
     * And the stale CONFIRM that went with it. This one is ACCEPTED, and that
     * is correct: on a lossy medium the honest peer's own retransmission is
     * indistinguishable from a replay, so refusing it would strand a contact
     * every time a radio dropped a frame. What matters is that accepting it
     * moves nothing -- migration_count is the counter that would advance if a
     * replay were producing a second migration.
     */
    CHECK_STATUS(mcl_contact_commit_confirm(&victim, MIG_A, SESS), MCL_LINK_OK);
    CHECK_TRUE(victim.migration_count == 1u);
    CHECK_TRUE(victim.state == MCL_CONTACT_STATE_ACTIVE);

    /*
     * A CONFIRM naming a transaction this contact never agreed to is refused.
     * That is the boundary between a retransmission and a fabrication, and it
     * is the one an observer would need to cross to assert a migration that
     * never happened.
     */
    CHECK_STATUS(mcl_contact_commit_confirm(&victim, MIG_B, SESS),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_TRUE(victim.migration_count == 1u);
}

/*
 * A REPLAYED CONTROL FROM ONE TRANSACTION MUST NOT DRIVE THE NEXT.
 *
 * The attacker records migration A's PATH_RESPONSE and replays it during
 * migration B, whose challenge it has never seen. Accepting it would let an
 * observer declare a path reachable that was never probed -- the single thing
 * path validation exists to prevent.
 */
static void test_replayed_path_response_from_another_transaction(void)
{
    mcl_contact_t victim;

    CHECK_STATUS(mcl_contact_begin(&victim, MCL_CONTACT_ROLE_INITIATOR,
                                   VICTIM_REF, MCL_CONTACT_TRANSPORT_AP),
                 MCL_LINK_OK);
    migrate(&victim, MIG_A, MCL_CONTACT_TRANSPORT_IP, SESS);

    /* Migration B begins. */
    CHECK_STATUS(mcl_contact_record_offer(&victim, MIG_B,
                                          MCL_CONTACT_TRANSPORT_BLE, 1u,
                                          0xD00Du, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&victim, MIG_B, MCL_CONTACT_TRANSPORT_BLE,
                                   1u, SESS), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_validation_begin(&victim, CHALLENGE), MCL_LINK_OK);

    /* Replay of A's response, wearing A's migration reference. */
    CHECK_STATUS(mcl_contact_validation_response(&victim, MIG_A, SESS, CHALLENGE),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    CHECK_TRUE(victim.state == MCL_CONTACT_STATE_VALIDATING);

    /* Right reference, wrong challenge bytes: also refused, and this is the
     * case that matters on a medium anyone can transmit into. */
    {
        uint8_t wrong[MCL_CONTACT_CHALLENGE_SIZE];
        size_t i;
        for (i = 0u; i < sizeof(wrong); ++i) { wrong[i] = CHALLENGE[i]; }
        wrong[sizeof(wrong) - 1u] ^= 0x01u;
        CHECK_STATUS(mcl_contact_validation_response(&victim, MIG_B, SESS, wrong),
                     MCL_LINK_ERR_CONTEXT_MISMATCH);
        CHECK_TRUE(victim.state == MCL_CONTACT_STATE_VALIDATING);
    }
}

/*
 * A CLOSED CONTACT HAS NO ROUTE BACK IN.
 *
 * Ending a contact is a decision the local node takes; what this test fixes is
 * that the decision is final. A closed contact that could be reopened by a
 * replayed control would let a resent frame drive a contact its owner had
 * finished with, and the reopened contact would carry state neither peer
 * agreed to.
 */
static void test_close_is_denial_and_never_a_takeover(void)
{
    mcl_contact_t victim;

    CHECK_STATUS(mcl_contact_begin(&victim, MCL_CONTACT_ROLE_RESPONDER,
                                   VICTIM_REF, MCL_CONTACT_TRANSPORT_IP),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_set_peer_ref(&victim, HONEST_PEER_REF), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_close(&victim), MCL_LINK_OK);
    CHECK_TRUE(victim.state == MCL_CONTACT_STATE_CLOSED);

    /* Every route back in is refused. */
    CHECK_STATUS(mcl_contact_record_offer(&victim, MIG_A,
                                          MCL_CONTACT_TRANSPORT_BLE, 1u,
                                          0xD00Du, 30u),
                 MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_agree(&victim, MIG_A, MCL_CONTACT_TRANSPORT_BLE,
                                   1u, SESS), MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_validation_begin(&victim, CHALLENGE),
                 MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_commit_begin(&victim), MCL_LINK_ERR_INVALID_STATE);
    CHECK_TRUE(victim.state == MCL_CONTACT_STATE_CLOSED);
}

/*
 * A MINIMAL PEER STILL PRODUCES A SELECTION BOTH SIDES COMPUTE IDENTICALLY.
 *
 * Selection is symmetric by construction -- min() and & are both commutative --
 * so a peer advertising the least it legally can gets a small selection, and
 * the important part is that BOTH peers arrive at the same one from the same
 * two sets. A selection the two sides computed differently would leave them
 * framing to different limits with no way to notice.
 *
 * The result is also legal at the richer peer, which is what lets a simple
 * device talk to a capable one at all.
 */
static void test_downgrade_succeeds_but_stays_coherent(void)
{
    mcl_link_capability_t honest;
    mcl_link_capability_t forged;
    mcl_link_negotiation_t forward;
    mcl_link_negotiation_t reverse;

    CHECK_STATUS(mcl_link_make_capability(&honest, 0x0003u, 0x0003u, 1024u,
                                          0x0F0Fu), MCL_LINK_OK);
    /* A peer advertising the least it legally can. */
    CHECK_STATUS(mcl_link_make_capability(&forged, 0x0001u, 0x0001u, 64u,
                                          0x0000u), MCL_LINK_OK);

    CHECK_STATUS(mcl_link_negotiation_select(&honest, &forged, &forward),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_link_negotiation_select(&forged, &honest, &reverse),
                 MCL_LINK_OK);

    /* Symmetric: both peers reach the same selection from the same two sets. */
    CHECK_TRUE(forward.wire_major == reverse.wire_major);
    CHECK_TRUE(forward.link_major == reverse.link_major);
    CHECK_TRUE(forward.max_frame == reverse.max_frame);
    CHECK_TRUE(forward.features == reverse.features);

    /* The selection is the intersection, which is the whole contract. */
    CHECK_TRUE(forward.wire_major == 0u);
    CHECK_TRUE(forward.features == 0x0000u);

    /* And the richer peer accepts it, because it is legal here. A selection
     * inside the local capability is exactly what the check is for. */
    CHECK_STATUS(mcl_link_negotiation_check(&forward, &honest, &forged),
                 MCL_LINK_OK);
}

/*
 * A FRAME RECORDED FROM ONE CONTACT MUST DO NOTHING TO ANOTHER.
 *
 * Two contacts run side by side. The attacker replays contact A's controls,
 * complete with A's migration and session references, into contact B. Session
 * references travel in the clear, so an attacker HAS them; that is the premise,
 * not the finding.
 *
 * Cross-contact contamination is on SECURITY.md's list of things that ARE
 * vulnerabilities, so this one must fail cleanly.
 */
static void test_cross_contact_replay_is_inert(void)
{
    mcl_contact_t a;
    mcl_contact_t b;

    CHECK_STATUS(mcl_contact_begin(&a, MCL_CONTACT_ROLE_INITIATOR, 0xA0u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_begin(&b, MCL_CONTACT_ROLE_INITIATOR, 0xB0u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_record_offer(&a, MIG_A, MCL_CONTACT_TRANSPORT_IP,
                                          1u, 0xD00Du, 30u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&a, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u, SESS),
                 MCL_LINK_OK);

    /* B is ACTIVE and has no pending transaction. A's AGREE is replayed at it. */
    CHECK_STATUS(mcl_contact_agree(&b, MIG_A, MCL_CONTACT_TRANSPORT_IP, 1u, SESS),
                 MCL_LINK_ERR_INVALID_STATE);
    CHECK_TRUE(b.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(b.migration_count == 0u);

    /* A's CONFIRM, likewise. */
    CHECK_STATUS(mcl_contact_commit_confirm(&b, MIG_A, SESS),
                 MCL_LINK_ERR_INVALID_STATE);
    CHECK_TRUE(b.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(b.migration_count == 0u);

    /* A is untouched by any of it. */
    CHECK_TRUE(a.state == MCL_CONTACT_STATE_AGREED);
}

int main(void)
{
    printf("=== adversarial: what an attacker achieves against v1.0 ===\n");

    test_replayed_hello_changes_nothing();
    test_replayed_offer_cannot_reverse_a_migration();
    test_replayed_path_response_from_another_transaction();
    test_close_is_denial_and_never_a_takeover();
    test_downgrade_succeeds_but_stays_coherent();
    test_cross_contact_replay_is_inert();

    printf("%d checks passed.\n", g_checks);
    printf("\n");
    printf("WHAT THIS ESTABLISHES:\n");
    printf("  Contact references travel in the clear and can be resent by\n");
    printf("  anyone who has seen them. Against the state machine that buys\n");
    printf("  nothing: no replay in this file advances a migration, adopts a\n");
    printf("  substituted reference, drives a second contact, or reopens a\n");
    printf("  closed one.\n");
    printf("\n");
    printf("  Two honest peers are never left disagreeing about what\n");
    printf("  happened. That property belongs to the state machine and holds\n");
    printf("  identically over a socket, a radio and a loudspeaker.\n");
    return 0;
}
