/*
 * Endpoint rendezvous: resolving a 32-bit endpoint_token to a real endpoint.
 *
 * The case that motivated this: an offer names a token, and without a defined
 * way to publish and find that token on the candidate transport, a laboratory
 * harness resolves it by already knowing where the peer is. That proves the
 * harness works. Two implementations from different vendors cannot do it at
 * all.
 */

#include "mcl/endpoint_rendezvous.h"
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

#define TOKEN UINT32_C(0xA713224F)

static void test_round_trip(void)
{
    uint8_t buf[MCL_RENDEZVOUS_BEACON_SIZE];
    size_t written = 0u;
    uint8_t transport = 0u;
    uint32_t token = 0u;

    CHECK_STATUS(mcl_rendezvous_beacon_encode(MCL_CONTACT_TRANSPORT_BLE, TOKEN,
                                              buf, sizeof(buf), &written), MCL_LINK_OK);
    CHECK_TRUE(written == MCL_RENDEZVOUS_BEACON_SIZE);

    /* Pinned layout: two magic bytes, version, transport, then the token big
     * endian. A change here is a wire-breaking change to every binding. */
    CHECK_TRUE(buf[0] == 0x4Du && buf[1] == 0x43u);
    CHECK_TRUE(buf[2] == MCL_RENDEZVOUS_BEACON_VERSION);
    CHECK_TRUE(buf[3] == MCL_CONTACT_TRANSPORT_BLE);
    CHECK_TRUE(buf[4] == 0xA7u && buf[5] == 0x13u && buf[6] == 0x22u && buf[7] == 0x4Fu);

    CHECK_STATUS(mcl_rendezvous_beacon_decode(buf, written, &transport, &token),
                 MCL_LINK_OK);
    CHECK_TRUE(transport == MCL_CONTACT_TRANSPORT_BLE);
    CHECK_TRUE(token == TOKEN);

    CHECK_TRUE(mcl_rendezvous_beacon_matches(buf, written,
                                             MCL_CONTACT_TRANSPORT_BLE, TOKEN) == 1u);
}

static void test_refusals(void)
{
    uint8_t buf[MCL_RENDEZVOUS_BEACON_SIZE];
    uint8_t small[MCL_RENDEZVOUS_BEACON_SIZE - 1u];
    size_t written = 0u;
    uint8_t transport = 0u;
    uint32_t token = 0u;

    CHECK_STATUS(mcl_rendezvous_beacon_encode(MCL_CONTACT_TRANSPORT_BLE, TOKEN,
                                              NULL, sizeof(buf), &written),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    /* The reserved transport is never a valid candidate. */
    CHECK_STATUS(mcl_rendezvous_beacon_encode(MCL_CONTACT_TRANSPORT_RESERVED, TOKEN,
                                              buf, sizeof(buf), &written),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    /* Token zero is reserved, so a peer that forgot to set one cannot publish a
     * beacon that a zeroed scanner state would match. */
    CHECK_STATUS(mcl_rendezvous_beacon_encode(MCL_CONTACT_TRANSPORT_BLE, 0u,
                                              buf, sizeof(buf), &written),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_rendezvous_beacon_encode(MCL_CONTACT_TRANSPORT_BLE, TOKEN,
                                              small, sizeof(small), &written),
                 MCL_LINK_ERR_RANGE);

    CHECK_STATUS(mcl_rendezvous_beacon_encode(MCL_CONTACT_TRANSPORT_BLE, TOKEN,
                                              buf, sizeof(buf), &written), MCL_LINK_OK);

    /* Short input is truncation, not malformation: a stream carriage may yet
     * receive the rest. */
    CHECK_STATUS(mcl_rendezvous_beacon_decode(buf, sizeof(buf) - 1u, &transport, &token),
                 MCL_LINK_ERR_TRUNCATED);

    {
        uint8_t bad[MCL_RENDEZVOUS_BEACON_SIZE];
        unsigned i;
        for (i = 0u; i < MCL_RENDEZVOUS_BEACON_SIZE; ++i) { bad[i] = buf[i]; }

        bad[0] = 0x00u;
        CHECK_STATUS(mcl_rendezvous_beacon_decode(bad, sizeof(bad), &transport, &token),
                     MCL_LINK_ERR_CONTEXT_MISMATCH);
        bad[0] = buf[0];

        bad[2] = 9u;  /* a beacon version we do not understand is not guessed at */
        CHECK_STATUS(mcl_rendezvous_beacon_decode(bad, sizeof(bad), &transport, &token),
                     MCL_LINK_ERR_INCOMPATIBLE_VERSION);
        bad[2] = buf[2];

        bad[3] = MCL_CONTACT_TRANSPORT_RESERVED;
        CHECK_STATUS(mcl_rendezvous_beacon_decode(bad, sizeof(bad), &transport, &token),
                     MCL_LINK_ERR_RANGE);
        bad[3] = buf[3];

        bad[4] = 0u; bad[5] = 0u; bad[6] = 0u; bad[7] = 0u;
        CHECK_STATUS(mcl_rendezvous_beacon_decode(bad, sizeof(bad), &transport, &token),
                     MCL_LINK_ERR_RANGE);
    }

    /* An all-zero buffer must never decode as a valid beacon. */
    {
        uint8_t zeroed[MCL_RENDEZVOUS_BEACON_SIZE];
        unsigned i;
        for (i = 0u; i < MCL_RENDEZVOUS_BEACON_SIZE; ++i) { zeroed[i] = 0u; }
        CHECK_TRUE(mcl_rendezvous_beacon_matches(zeroed, sizeof(zeroed),
                                                 MCL_CONTACT_TRANSPORT_BLE, TOKEN) == 0u);
    }
}

/*
 * A scanner sees unrelated traffic continuously. Non-matches must be reported
 * as "not ours", never as an error, or the normal case looks pathological.
 */
static void test_scanner_rejects_without_erroring(void)
{
    uint8_t buf[MCL_RENDEZVOUS_BEACON_SIZE];
    size_t written = 0u;
    const uint8_t noise[5] = {0x02u, 0x01u, 0x06u, 0x11u, 0x07u};

    CHECK_STATUS(mcl_rendezvous_beacon_encode(MCL_CONTACT_TRANSPORT_BLE, TOKEN,
                                              buf, sizeof(buf), &written), MCL_LINK_OK);

    /* Right token, wrong medium: a beacon heard where it does not belong. */
    CHECK_TRUE(mcl_rendezvous_beacon_matches(buf, written,
                                             MCL_CONTACT_TRANSPORT_IP, TOKEN) == 0u);
    /* Right medium, a token from some other machine's migration. */
    CHECK_TRUE(mcl_rendezvous_beacon_matches(buf, written,
                                             MCL_CONTACT_TRANSPORT_BLE, TOKEN + 1u) == 0u);
    /* Traffic that is not a beacon at all. */
    CHECK_TRUE(mcl_rendezvous_beacon_matches(noise, sizeof(noise),
                                             MCL_CONTACT_TRANSPORT_BLE, TOKEN) == 0u);
    CHECK_TRUE(mcl_rendezvous_beacon_matches(NULL, 0u,
                                             MCL_CONTACT_TRANSPORT_BLE, TOKEN) == 0u);
}

/*
 * Documents the LIMIT. The token crossed an observable medium in the offer, so
 * any listener can publish a beacon carrying it and be found. Matching a beacon
 * establishes that a candidate endpoint claims this token, and nothing more.
 *
 * Asserted deliberately so nobody later treats "found the right beacon" as
 * "found the right peer".
 */
static void test_any_publisher_matches(void)
{
    uint8_t honest[MCL_RENDEZVOUS_BEACON_SIZE];
    uint8_t impostor[MCL_RENDEZVOUS_BEACON_SIZE];
    size_t n = 0u;

    CHECK_STATUS(mcl_rendezvous_beacon_encode(MCL_CONTACT_TRANSPORT_BLE, TOKEN,
                                              honest, sizeof(honest), &n), MCL_LINK_OK);
    /* Built by a machine that merely overheard the offer. */
    CHECK_STATUS(mcl_rendezvous_beacon_encode(MCL_CONTACT_TRANSPORT_BLE, TOKEN,
                                              impostor, sizeof(impostor), &n), MCL_LINK_OK);

    CHECK_TRUE(mcl_rendezvous_beacon_matches(impostor, n,
                                             MCL_CONTACT_TRANSPORT_BLE, TOKEN) == 1u);
    {
        unsigned i;
        for (i = 0u; i < MCL_RENDEZVOUS_BEACON_SIZE; ++i) {
            CHECK_TRUE(honest[i] == impostor[i]);
        }
    }
}

int main(void)
{
    test_round_trip();
    test_refusals();
    test_scanner_rejects_without_erroring();
    test_any_publisher_matches();

    printf("mcl_link_rendezvous: %d checks passed\n", g_checks);
    printf("NOTE: a beacon match locates a candidate endpoint, not a peer.\n");
    return 0;
}
