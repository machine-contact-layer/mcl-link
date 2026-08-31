#include "mcl/link.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_STATUS(call, expected) do { \
    const mcl_link_status_t mcl_st__ = (call); \
    if (mcl_st__ != (expected)) { \
        fprintf(stderr, "FAIL at %s:%d: %s returned %d, expected %d\n", \
                __FILE__, __LINE__, #call, (int)mcl_st__, (int)(expected)); \
        exit(1); \
    } \
} while (0)

#define CHECK_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL at %s:%d: expression (%s) evaluated to false\n", \
                __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static uint32_t mcl_link_rng_state = 0x5a17e001u;

static uint32_t mcl_link_random(void)
{
    uint32_t x = mcl_link_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    mcl_link_rng_state = x;
    return x;
}

static void test_initialization_and_null_checks(void)
{
    mcl_link_t link;
    uint8_t has_context = 1u;

    CHECK_STATUS(mcl_link_init(NULL, 0x0001u), MCL_LINK_ERR_INVALID_ARGUMENT);

    CHECK_STATUS(mcl_link_init(&link, 0x0003u), MCL_LINK_OK);
    CHECK_TRUE(link.state == MCL_LINK_STATE_IDLE);
    CHECK_STATUS(mcl_link_has_active_context(&link, &has_context), MCL_LINK_OK);
    CHECK_TRUE(has_context == 0u);

    CHECK_STATUS(mcl_link_reset(NULL), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_reset(&link), MCL_LINK_OK);
    CHECK_TRUE(link.state == MCL_LINK_STATE_IDLE);
}

static void test_context_storage_and_bounds(void)
{
    mcl_link_t link;
    mcl_link_context_key_t key;
    mcl_link_context_key_t candidate;
    size_t i;

    CHECK_STATUS(mcl_link_init(&link, 0x0001u), MCL_LINK_OK);

    /* 1. Digest size 0 is invalid */
    memset(&key, 0, sizeof(key));
    key.wire_major = 0u;
    key.context_id = 100u;
    key.generation = 1u;
    key.ruleset_digest_size = 0u;
    CHECK_STATUS(mcl_link_install_context(&link, &key), MCL_LINK_ERR_RANGE);

    /* 2. Digest size > 32 is invalid */
    key.ruleset_digest_size = MCL_LINK_RULESET_DIGEST_MAX_SIZE + 1u;
    CHECK_STATUS(mcl_link_install_context(&link, &key), MCL_LINK_ERR_RANGE);

    /* 3. Wire major > 15 is invalid */
    key.wire_major = 16u;
    key.ruleset_digest_size = 16u;
    CHECK_STATUS(mcl_link_install_context(&link, &key), MCL_LINK_ERR_RANGE);

    /* 4. Minimum valid digest size (1 byte) */
    key.wire_major = 0u;
    key.context_id = 1u;
    key.generation = 1u;
    key.ruleset_digest_size = 1u;
    key.ruleset_digest[0] = 0x55u;
    CHECK_STATUS(mcl_link_install_context(&link, &key), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_authorize_context(&link, &key), MCL_LINK_OK);

    /* 5. Maximum valid digest size (32 bytes) with max UINT32 fields */
    key.wire_major = 0u;
    key.context_id = UINT32_MAX;
    key.generation = UINT32_MAX;
    key.ruleset_digest_size = MCL_LINK_RULESET_DIGEST_MAX_SIZE;
    for (i = 0u; i < MCL_LINK_RULESET_DIGEST_MAX_SIZE; ++i) {
        key.ruleset_digest[i] = (uint8_t)(i ^ 0xa5u);
    }
    CHECK_STATUS(mcl_link_install_context(&link, &key), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_authorize_context(&link, &key), MCL_LINK_OK);

    /* 6. Same prefix but different digest length -> mismatch */
    candidate = key;
    candidate.ruleset_digest_size = 16u;
    CHECK_STATUS(mcl_link_authorize_context(&link, &candidate), MCL_LINK_ERR_CONTEXT_MISMATCH);

    /* 7. Same length but single byte mismatch -> mismatch */
    candidate = key;
    candidate.ruleset_digest[31] ^= 0x01u;
    CHECK_STATUS(mcl_link_authorize_context(&link, &candidate), MCL_LINK_ERR_CONTEXT_MISMATCH);

    /* 8. Wrong context ID -> mismatch */
    candidate = key;
    candidate.context_id = UINT32_MAX - 1u;
    CHECK_STATUS(mcl_link_authorize_context(&link, &candidate), MCL_LINK_ERR_CONTEXT_MISMATCH);

    /* 9. Wrong generation -> mismatch */
    candidate = key;
    candidate.generation = UINT32_MAX - 1u;
    CHECK_STATUS(mcl_link_authorize_context(&link, &candidate), MCL_LINK_ERR_CONTEXT_MISMATCH);

    /* 10. Unsupported wire major on install */
    key.wire_major = 1u; /* link was initialized with only mask 0x0001 (major 0) */
    CHECK_STATUS(mcl_link_install_context(&link, &key), MCL_LINK_ERR_INCOMPATIBLE_VERSION);

    /* 11. Reset invalidates context */
    CHECK_STATUS(mcl_link_reset(&link), MCL_LINK_OK);
    key.wire_major = 0u;
    CHECK_STATUS(mcl_link_authorize_context(&link, &key), MCL_LINK_ERR_NO_ACTIVE_CONTEXT);
}

static void test_state_machine_matrix(void)
{
    mcl_link_t link;
    unsigned s_from, s_to;

    /*
     * Exhaustively test every transition in the 9x9 state matrix against
     * the research draft lifecycle.
     */
    for (s_from = 0u; s_from <= 8u; ++s_from) {
        for (s_to = 0u; s_to <= 8u; ++s_to) {
            uint8_t allowed = 0u;
            mcl_link_status_t expected;

            CHECK_STATUS(mcl_link_init(&link, 0x0001u), MCL_LINK_OK);
            link.state = (mcl_link_state_t)s_from;

            switch (s_from) {
            case MCL_LINK_STATE_IDLE:
                if (s_to == MCL_LINK_STATE_DISCOVERED || s_to == MCL_LINK_STATE_CLOSED) allowed = 1u;
                break;
            case MCL_LINK_STATE_DISCOVERED:
                if (s_to == MCL_LINK_STATE_CAPABILITIES || s_to == MCL_LINK_STATE_IDLE || s_to == MCL_LINK_STATE_CLOSED) allowed = 1u;
                break;
            case MCL_LINK_STATE_CAPABILITIES:
                if (s_to == MCL_LINK_STATE_NEGOTIATING || s_to == MCL_LINK_STATE_IDLE || s_to == MCL_LINK_STATE_CLOSED) allowed = 1u;
                break;
            case MCL_LINK_STATE_NEGOTIATING:
                if (s_to == MCL_LINK_STATE_ESTABLISHED || s_to == MCL_LINK_STATE_IDLE || s_to == MCL_LINK_STATE_CLOSED) allowed = 1u;
                break;
            case MCL_LINK_STATE_ESTABLISHED:
                if (s_to == MCL_LINK_STATE_ADAPTING || s_to == MCL_LINK_STATE_HANDOFF || s_to == MCL_LINK_STATE_FALLBACK || s_to == MCL_LINK_STATE_IDLE || s_to == MCL_LINK_STATE_CLOSED) allowed = 1u;
                break;
            case MCL_LINK_STATE_ADAPTING:
                if (s_to == MCL_LINK_STATE_ESTABLISHED || s_to == MCL_LINK_STATE_HANDOFF || s_to == MCL_LINK_STATE_FALLBACK || s_to == MCL_LINK_STATE_IDLE || s_to == MCL_LINK_STATE_CLOSED) allowed = 1u;
                break;
            case MCL_LINK_STATE_HANDOFF:
                if (s_to == MCL_LINK_STATE_ESTABLISHED || s_to == MCL_LINK_STATE_IDLE || s_to == MCL_LINK_STATE_CLOSED) allowed = 1u;
                break;
            case MCL_LINK_STATE_FALLBACK:
                if (s_to == MCL_LINK_STATE_ESTABLISHED || s_to == MCL_LINK_STATE_DISCOVERED || s_to == MCL_LINK_STATE_IDLE || s_to == MCL_LINK_STATE_CLOSED) allowed = 1u;
                break;
            case MCL_LINK_STATE_CLOSED:
                if (s_to == MCL_LINK_STATE_IDLE) allowed = 1u;
                break;
            default:
                break;
            }

            expected = allowed ? MCL_LINK_OK : MCL_LINK_ERR_INVALID_STATE;
            CHECK_STATUS(mcl_link_transition(&link, (mcl_link_state_t)s_to), expected);
            if (allowed) {
                CHECK_TRUE(link.state == (mcl_link_state_t)s_to);
            }
        }
    }
}

static void test_randomized_context_trials(void)
{
    mcl_link_t link;
    mcl_link_context_key_t good;
    unsigned trial;
    unsigned accepted = 0u;
    unsigned rejected = 0u;
    size_t i;

    good.wire_major = 0u;
    good.context_id = 0x12345678u;
    good.generation = 42u;
    good.ruleset_digest_size = 20u;
    for (i = 0u; i < 20u; ++i) {
        good.ruleset_digest[i] = (uint8_t)(i * 7u + 3u);
    }
    for (; i < MCL_LINK_RULESET_DIGEST_MAX_SIZE; ++i) {
        good.ruleset_digest[i] = 0u;
    }

    CHECK_STATUS(mcl_link_init(&link, MCL_LINK_WIRE_MAJOR_MASK(0)), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_install_context(&link, &good), MCL_LINK_OK);

    for (trial = 0u; trial < 10000u; ++trial) {
        mcl_link_context_key_t candidate;
        if ((mcl_link_random() % 20u) == 0u) {
            candidate = good;
        } else {
            candidate.wire_major = (uint8_t)(mcl_link_random() & 1u);
            candidate.context_id = mcl_link_random();
            candidate.generation = mcl_link_random();
            candidate.ruleset_digest_size = (uint8_t)(1u + (mcl_link_random() % MCL_LINK_RULESET_DIGEST_MAX_SIZE));
            for (i = 0u; i < MCL_LINK_RULESET_DIGEST_MAX_SIZE; ++i) {
                candidate.ruleset_digest[i] = (uint8_t)mcl_link_random();
            }
        }

        if (mcl_link_authorize_context(&link, &candidate) == MCL_LINK_OK) {
            CHECK_TRUE(mcl_link_context_key_equals(&candidate, &good) == 1u);
            ++accepted;
        } else {
            CHECK_TRUE(mcl_link_context_key_equals(&candidate, &good) == 0u);
            ++rejected;
        }
    }

    CHECK_TRUE(accepted > 0u);
    CHECK_TRUE(rejected > 0u);
    CHECK_TRUE(accepted + rejected == 10000u);

    printf("randomized context trials: %u accepted, %u rejected (10000 total) PASS\n", accepted, rejected);
}

int main(void)
{
    test_initialization_and_null_checks();
    test_context_storage_and_bounds();
    test_state_machine_matrix();
    test_randomized_context_trials();

    puts("mcl_link test suite: ALL PASS");
    return 0;
}
