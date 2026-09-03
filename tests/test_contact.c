/*
 * Contact migration: continuing one logical contact across a transport change.
 *
 * The positive path is the easy half. What decides whether two independent
 * implementations can migrate without corrupting each other's state is what
 * gets REFUSED, so most of this file is refusals, and each one says what would
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

/* Two peers that meet acoustically and continue over BLE, with no cryptography
 * anywhere. This is the flow the whole project exists to make possible. */
static void test_acoustic_to_ble_migration(void)
{
    mcl_contact_t initiator;
    mcl_contact_t responder;
    uint8_t transport = 0u;

    CHECK_STATUS(mcl_contact_begin(&initiator, MCL_CONTACT_ROLE_INITIATOR,
                                   0xA1A1A1A1u, MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_begin(&responder, MCL_CONTACT_ROLE_RESPONDER,
                                   0xB2B2B2B2u, MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_set_peer_ref(&initiator, 0xB2B2B2B2u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_set_peer_ref(&responder, 0xA1A1A1A1u), MCL_LINK_OK);

    /* TRANSPORT_OFFER travels acoustically; both sides record the same offer. */
    CHECK_STATUS(mcl_contact_record_offer(&initiator, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00D0001u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&responder, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xD00D0001u), MCL_LINK_OK);

    /* TRANSPORT_ACCEPT carries the responder's chosen correlation reference. */
    CHECK_STATUS(mcl_contact_agree(&responder, MCL_CONTACT_TRANSPORT_BLE,
                                   1u, 0x5E5510C7u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&initiator, MCL_CONTACT_TRANSPORT_BLE,
                                   1u, 0x5E5510C7u), MCL_LINK_OK);

    /* Neither peer has moved yet: agreement is not arrival. */
    CHECK_STATUS(mcl_contact_active_transport(&initiator, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_AP);

    /* First frame heard over BLE completes the migration. */
    CHECK_STATUS(mcl_contact_resume(&initiator, MCL_CONTACT_TRANSPORT_BLE,
                                    0x5E5510C7u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_resume(&responder, MCL_CONTACT_TRANSPORT_BLE,
                                    0x5E5510C7u), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_active_transport(&initiator, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_BLE);
    CHECK_TRUE(initiator.migration_count == 1u);
    CHECK_TRUE(initiator.state == MCL_CONTACT_STATE_ACTIVE);

    /* The contact reference learned acoustically survives the move. That is
     * the whole point: it is the same contact, not a new one. */
    CHECK_TRUE(initiator.peer_ref == 0xB2B2B2B2u);
    CHECK_TRUE(initiator.peer_ref_valid == 1u);
}

/* A contact may migrate more than once, and each hop must be agreed afresh. */
static void test_chained_migration(void)
{
    mcl_contact_t c;
    uint8_t transport = 0u;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR,
                                   0x11111111u, MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_record_offer(&c, MCL_CONTACT_TRANSPORT_BLE, 1u, 1u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MCL_CONTACT_TRANSPORT_BLE, 1u, 0x1001u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_resume(&c, MCL_CONTACT_TRANSPORT_BLE, 0x1001u), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_record_offer(&c, MCL_CONTACT_TRANSPORT_IP, 2u, 2u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MCL_CONTACT_TRANSPORT_IP, 2u, 0x2002u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_resume(&c, MCL_CONTACT_TRANSPORT_IP, 0x2002u), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_IP);
    CHECK_TRUE(c.migration_count == 2u);

    /*
     * The reference agreed for the first hop must not still be accepted after
     * the second. If it were, a stale reference observed on the acoustic
     * channel would remain usable for the life of the contact.
     */
    CHECK_STATUS(mcl_contact_record_offer(&c, MCL_CONTACT_TRANSPORT_BLE, 1u, 3u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MCL_CONTACT_TRANSPORT_BLE, 1u, 0x3003u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_resume(&c, MCL_CONTACT_TRANSPORT_BLE, 0x1001u),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
}

static void test_refusals(void)
{
    mcl_contact_t c;

    /* Null argument on every entry point. */
    CHECK_STATUS(mcl_contact_begin(NULL, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_set_peer_ref(NULL, 1u), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_record_offer(NULL, MCL_CONTACT_TRANSPORT_BLE, 1u, 1u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_agree(NULL, MCL_CONTACT_TRANSPORT_BLE, 1u, 1u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_resume(NULL, MCL_CONTACT_TRANSPORT_BLE, 1u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_abandon_migration(NULL), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_close(NULL), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_active_transport(NULL, NULL), MCL_LINK_ERR_INVALID_ARGUMENT);

    /* The reserved transport is never valid, so a zeroed field cannot pass. */
    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_RESERVED),
                 MCL_LINK_ERR_INVALID_ARGUMENT);

    /* An undefined role is refused rather than coerced. */
    CHECK_STATUS(mcl_contact_begin(&c, (mcl_contact_role_t)7u, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_ERR_INVALID_ARGUMENT);

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);

    /* Offering the transport already in use is adaptation, not migration.
     * Allowing it would let a peer complete a migration without ever showing
     * it can be reached anywhere else. */
    CHECK_STATUS(mcl_contact_record_offer(&c, MCL_CONTACT_TRANSPORT_AP, 1u, 1u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_contact_record_offer(&c, MCL_CONTACT_TRANSPORT_RESERVED, 1u, 1u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);

    /* Agreement before any offer: nothing to agree to. */
    CHECK_STATUS(mcl_contact_agree(&c, MCL_CONTACT_TRANSPORT_BLE, 1u, 0x99u),
                 MCL_LINK_ERR_INVALID_STATE);
    /* Resume before agreement: the contact has not moved. */
    CHECK_STATUS(mcl_contact_resume(&c, MCL_CONTACT_TRANSPORT_BLE, 0x99u),
                 MCL_LINK_ERR_INVALID_STATE);

    CHECK_STATUS(mcl_contact_record_offer(&c, MCL_CONTACT_TRANSPORT_BLE, 1u,
                                          0xD00Du), MCL_LINK_OK);

    /* A second offer while one is outstanding must not silently replace it,
     * or either peer could redirect the migration by re-offering. */
    CHECK_STATUS(mcl_contact_record_offer(&c, MCL_CONTACT_TRANSPORT_IP, 1u, 1u),
                 MCL_LINK_ERR_INVALID_STATE);

    /* Session reference zero is reserved. */
    CHECK_STATUS(mcl_contact_agree(&c, MCL_CONTACT_TRANSPORT_BLE, 1u,
                                   MCL_CONTACT_SESSION_NONE), MCL_LINK_ERR_INVALID_ARGUMENT);

    /* An acceptance must not redirect the contact to an unoffered transport. */
    CHECK_STATUS(mcl_contact_agree(&c, MCL_CONTACT_TRANSPORT_IP, 1u, 0x77u),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    /* Nor silently change the profile that was offered. */
    CHECK_STATUS(mcl_contact_agree(&c, MCL_CONTACT_TRANSPORT_BLE, 9u, 0x77u),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);

    CHECK_STATUS(mcl_contact_agree(&c, MCL_CONTACT_TRANSPORT_BLE, 1u, 0x77u), MCL_LINK_OK);

    /* Wrong reference on the right transport: this is the racing-peer case,
     * and it is the one refusal that carries the whole non-cryptographic
     * continuity property. */
    CHECK_STATUS(mcl_contact_resume(&c, MCL_CONTACT_TRANSPORT_BLE, 0x78u),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);
    /* Right reference on a transport that was never agreed. */
    CHECK_STATUS(mcl_contact_resume(&c, MCL_CONTACT_TRANSPORT_IP, 0x77u),
                 MCL_LINK_ERR_CONTEXT_MISMATCH);

    /* A refused resume must leave the contact intact and still resumable: a
     * wrong arrival is not grounds to destroy a live contact. */
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_AGREED);
    CHECK_STATUS(mcl_contact_resume(&c, MCL_CONTACT_TRANSPORT_BLE, 0x77u), MCL_LINK_OK);
}

/* A migration that never happens must leave the machines talking where they
 * actually met, not disconnected. */
static void test_abandon_keeps_contact(void)
{
    mcl_contact_t c;
    uint8_t transport = 0u;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_RESPONDER, 5u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&c, MCL_CONTACT_TRANSPORT_IP, 1u, 9u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_abandon_migration(&c), MCL_LINK_OK);

    CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_AP);
    CHECK_TRUE(c.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(c.migration_count == 0u);

    /* Abandoning after agreement must also discard the agreed reference, so it
     * cannot be replayed by a peer that arrives late. */
    CHECK_STATUS(mcl_contact_record_offer(&c, MCL_CONTACT_TRANSPORT_IP, 1u, 9u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MCL_CONTACT_TRANSPORT_IP, 1u, 0xABCDu), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_abandon_migration(&c), MCL_LINK_OK);
    CHECK_TRUE(c.session_valid == 0u);
    CHECK_STATUS(mcl_contact_resume(&c, MCL_CONTACT_TRANSPORT_IP, 0xABCDu),
                 MCL_LINK_ERR_INVALID_STATE);

    /* Nothing to abandon is a state error, not a silent success. */
    CHECK_STATUS(mcl_contact_abandon_migration(&c), MCL_LINK_ERR_INVALID_STATE);
}

static void test_closed_contact_is_inert(void)
{
    mcl_contact_t c;
    uint8_t transport = 0u;

    CHECK_STATUS(mcl_contact_begin(&c, MCL_CONTACT_ROLE_INITIATOR, 5u,
                                   MCL_CONTACT_TRANSPORT_BLE), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&c, MCL_CONTACT_TRANSPORT_IP, 1u, 9u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&c, MCL_CONTACT_TRANSPORT_IP, 1u, 0x4242u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_close(&c), MCL_LINK_OK);

    /* A closed contact cannot be revived by a peer arriving with a reference
     * it observed earlier. */
    CHECK_STATUS(mcl_contact_resume(&c, MCL_CONTACT_TRANSPORT_IP, 0x4242u),
                 MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_record_offer(&c, MCL_CONTACT_TRANSPORT_AP, 1u, 1u),
                 MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_set_peer_ref(&c, 7u), MCL_LINK_ERR_INVALID_STATE);
    CHECK_STATUS(mcl_contact_active_transport(&c, &transport), MCL_LINK_ERR_INVALID_STATE);
    CHECK_TRUE(c.session_valid == 0u);
}

/*
 * Documents the limit of this module rather than a capability of it.
 *
 * An observer that heard the acoustic contact learns the session reference,
 * because it crossed an open medium in the clear. It can then present that
 * reference on the new transport and be accepted, exactly as an honest peer
 * would be. Correlation cannot tell them apart, and this test exists so that
 * nobody later mistakes a passing continuity suite for a security property.
 */
static void test_observer_is_indistinguishable(void)
{
    mcl_contact_t honest;
    mcl_contact_t victim;
    uint32_t observed_session_ref;

    CHECK_STATUS(mcl_contact_begin(&victim, MCL_CONTACT_ROLE_INITIATOR, 1u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&victim, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xE0u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&victim, MCL_CONTACT_TRANSPORT_BLE,
                                   1u, 0xC0FFEEu), MCL_LINK_OK);

    /* Everything above crossed a loudspeaker. This is what a listener has. */
    observed_session_ref = 0xC0FFEEu;

    CHECK_STATUS(mcl_contact_begin(&honest, MCL_CONTACT_ROLE_RESPONDER, 2u,
                                   MCL_CONTACT_TRANSPORT_AP), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_record_offer(&honest, MCL_CONTACT_TRANSPORT_BLE,
                                          1u, 0xE0u), MCL_LINK_OK);
    CHECK_STATUS(mcl_contact_agree(&honest, MCL_CONTACT_TRANSPORT_BLE,
                                   1u, observed_session_ref), MCL_LINK_OK);

    /* The victim accepts the observed reference. It cannot do otherwise. */
    CHECK_STATUS(mcl_contact_resume(&victim, MCL_CONTACT_TRANSPORT_BLE,
                                    observed_session_ref), MCL_LINK_OK);

    /*
     * Asserted deliberately: session continuity is correlation and NOT
     * authentication. If a future change made this refusal-by-accident, that
     * would not be a security property either -- it would be an accident. The
     * property that would defeat this observer is cryptographic contact
     * binding, which MCL does not have. See
     * mcl-link/research/contact-continuity-experiment.md.
     */
    CHECK_TRUE(victim.state == MCL_CONTACT_STATE_ACTIVE);
    CHECK_TRUE(victim.migration_count == 1u);
}

int main(void)
{
    test_acoustic_to_ble_migration();
    test_chained_migration();
    test_refusals();
    test_abandon_keeps_contact();
    test_closed_contact_is_inert();
    test_observer_is_indistinguishable();

    printf("mcl_link_contact: %d checks passed\n", g_checks);
    printf("NOTE: session continuity is correlation, not authentication.\n");
    return 0;
}
