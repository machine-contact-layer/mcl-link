#include "mcl/handoff.h"

#include <stdio.h>
#include <stdlib.h>

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
        fprintf(stderr, "FAIL at %s:%d: (%s) is false\n", \
                __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

/*
 * The transaction references and challenge used by every vector, mirroring
 * mcl-link/conformance/vectors/handoff-v0.1.json. The published JSON and these
 * arrays must agree byte for byte; they are two renderings of one artifact.
 */
#define VEC_MIGRATION_REF 0x4D194201u
#define VEC_SESSION_REF   0x9A3C0517u

static const uint8_t vec_challenge[MCL_CONTACT_CHALLENGE_SIZE] = {
    0x8Bu, 0x41u, 0xD2u, 0x07u, 0x6Eu, 0x55u, 0x90u, 0x3Cu
};

static const uint8_t vec_path_challenge[] = {
    0x00u, 0x01u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u,
    0x8Bu, 0x41u, 0xD2u, 0x07u, 0x6Eu, 0x55u, 0x90u, 0x3Cu
};

static const uint8_t vec_path_response[] = {
    0x00u, 0x02u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u,
    0x8Bu, 0x41u, 0xD2u, 0x07u, 0x6Eu, 0x55u, 0x90u, 0x3Cu
};

static const uint8_t vec_commit[] = {
    0x00u, 0x03u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u
};

static const uint8_t vec_confirm[] = {
    0x00u, 0x04u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u
};

static void expect_bytes(
    const char *name,
    const uint8_t *actual,
    size_t actual_size,
    const uint8_t *expected,
    size_t expected_size)
{
    size_t i;

    if (actual_size != expected_size) {
        fprintf(stderr, "FAIL %s: encoded %u bytes, vector has %u\n",
                name, (unsigned)actual_size, (unsigned)expected_size);
        exit(1);
    }
    for (i = 0u; i < expected_size; ++i) {
        if (actual[i] != expected[i]) {
            fprintf(stderr, "FAIL %s: byte %u is 0x%02X, vector has 0x%02X\n",
                    name, (unsigned)i, actual[i], expected[i]);
            exit(1);
        }
    }
}

/* ---------------------------------------------------------------- positive */

static void test_encode_matches_vectors(void)
{
    mcl_handoff_control_t control;
    uint8_t out[MCL_HANDOFF_CONTROL_MAX_SIZE];
    size_t written = 0u;

    CHECK_STATUS(mcl_handoff_make_path_challenge(&control, VEC_MIGRATION_REF,
                                                 VEC_SESSION_REF, vec_challenge),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_handoff_control_encode(&control, out, sizeof(out), &written),
                 MCL_LINK_OK);
    expect_bytes("path_challenge", out, written,
                 vec_path_challenge, sizeof(vec_path_challenge));

    CHECK_STATUS(mcl_handoff_make_path_response(&control, VEC_MIGRATION_REF,
                                                VEC_SESSION_REF, vec_challenge),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_handoff_control_encode(&control, out, sizeof(out), &written),
                 MCL_LINK_OK);
    expect_bytes("path_response", out, written,
                 vec_path_response, sizeof(vec_path_response));

    CHECK_STATUS(mcl_handoff_make_commit(&control, VEC_MIGRATION_REF,
                                         VEC_SESSION_REF),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_handoff_control_encode(&control, out, sizeof(out), &written),
                 MCL_LINK_OK);
    expect_bytes("commit", out, written, vec_commit, sizeof(vec_commit));

    CHECK_STATUS(mcl_handoff_make_confirm(&control, VEC_MIGRATION_REF,
                                          VEC_SESSION_REF),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_handoff_control_encode(&control, out, sizeof(out), &written),
                 MCL_LINK_OK);
    expect_bytes("confirm", out, written, vec_confirm, sizeof(vec_confirm));
}

static void test_decode_matches_vectors(void)
{
    mcl_handoff_control_t control;
    unsigned i;

    CHECK_STATUS(mcl_handoff_control_decode(vec_path_challenge,
                                            sizeof(vec_path_challenge),
                                            &control),
                 MCL_LINK_OK);
    CHECK_TRUE(control.operation == MCL_HANDOFF_OP_PATH_CHALLENGE);
    CHECK_TRUE(control.migration_ref == VEC_MIGRATION_REF);
    CHECK_TRUE(control.session_ref == VEC_SESSION_REF);
    CHECK_TRUE(control.challenge_present == 1u);
    for (i = 0u; i < MCL_CONTACT_CHALLENGE_SIZE; ++i) {
        CHECK_TRUE(control.challenge[i] == vec_challenge[i]);
    }

    CHECK_STATUS(mcl_handoff_control_decode(vec_path_response,
                                            sizeof(vec_path_response),
                                            &control),
                 MCL_LINK_OK);
    CHECK_TRUE(control.operation == MCL_HANDOFF_OP_PATH_RESPONSE);
    CHECK_TRUE(control.challenge_present == 1u);

    CHECK_STATUS(mcl_handoff_control_decode(vec_commit, sizeof(vec_commit),
                                            &control),
                 MCL_LINK_OK);
    CHECK_TRUE(control.operation == MCL_HANDOFF_OP_COMMIT);
    CHECK_TRUE(control.migration_ref == VEC_MIGRATION_REF);
    CHECK_TRUE(control.session_ref == VEC_SESSION_REF);
    CHECK_TRUE(control.challenge_present == 0u);

    CHECK_STATUS(mcl_handoff_control_decode(vec_confirm, sizeof(vec_confirm),
                                            &control),
                 MCL_LINK_OK);
    CHECK_TRUE(control.operation == MCL_HANDOFF_OP_CONFIRM);
    CHECK_TRUE(control.challenge_present == 0u);
}

/*
 * A decoded control must not expose a challenge behind an operation that does
 * not carry one, even when the destination struct is reused and still holds the
 * previous control's bytes.
 */
static void test_decode_clears_stale_challenge(void)
{
    mcl_handoff_control_t control;
    unsigned i;
    unsigned nonzero = 0u;

    CHECK_STATUS(mcl_handoff_control_decode(vec_path_challenge,
                                            sizeof(vec_path_challenge),
                                            &control),
                 MCL_LINK_OK);
    CHECK_TRUE(control.challenge_present == 1u);

    CHECK_STATUS(mcl_handoff_control_decode(vec_commit, sizeof(vec_commit),
                                            &control),
                 MCL_LINK_OK);
    CHECK_TRUE(control.challenge_present == 0u);
    for (i = 0u; i < MCL_CONTACT_CHALLENGE_SIZE; ++i) {
        nonzero |= control.challenge[i];
    }
    CHECK_TRUE(nonzero == 0u);
}

static void test_sizes(void)
{
    CHECK_TRUE(mcl_handoff_control_encoded_size(MCL_HANDOFF_OP_PATH_CHALLENGE) == 18u);
    CHECK_TRUE(mcl_handoff_control_encoded_size(MCL_HANDOFF_OP_PATH_RESPONSE) == 18u);
    CHECK_TRUE(mcl_handoff_control_encoded_size(MCL_HANDOFF_OP_COMMIT) == 10u);
    CHECK_TRUE(mcl_handoff_control_encoded_size(MCL_HANDOFF_OP_CONFIRM) == 10u);

    /* Reserved, unassigned and Experimental Use are all unencodable here. */
    CHECK_TRUE(mcl_handoff_control_encoded_size(MCL_HANDOFF_OP_RESERVED) == 0u);
    CHECK_TRUE(mcl_handoff_control_encoded_size(5u) == 0u);
    CHECK_TRUE(mcl_handoff_control_encoded_size(MCL_HANDOFF_OP_EXPERIMENTAL_FIRST) == 0u);
    CHECK_TRUE(mcl_handoff_control_encoded_size(MCL_HANDOFF_OP_EXPERIMENTAL_LAST) == 0u);

    CHECK_TRUE(mcl_handoff_op_carries_challenge(MCL_HANDOFF_OP_PATH_CHALLENGE) == 1u);
    CHECK_TRUE(mcl_handoff_op_carries_challenge(MCL_HANDOFF_OP_COMMIT) == 0u);
    CHECK_TRUE(mcl_handoff_op_carries_challenge(200u) == 0u);
}

/* ---------------------------------------------------------------- negative */

static void decode_must_fail(
    const char *name,
    const uint8_t *bytes,
    size_t size,
    mcl_link_status_t expected)
{
    mcl_handoff_control_t control;
    mcl_link_status_t status = mcl_handoff_control_decode(bytes, size, &control);

    if (status != expected) {
        fprintf(stderr, "FAIL negative %s: decode returned %d, expected %d\n",
                name, (int)status, (int)expected);
        exit(1);
    }
}

static void test_negative_vectors(void)
{
    static const uint8_t truncated_challenge[] = {
        0x00u, 0x01u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u,
        0x8Bu, 0x41u, 0xD2u, 0x07u, 0x6Eu, 0x55u, 0x90u
    };
    static const uint8_t trailing_byte[] = {
        0x00u, 0x03u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u,
        0x00u
    };
    static const uint8_t challenge_on_commit[] = {
        0x00u, 0x03u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u,
        0x8Bu, 0x41u, 0xD2u, 0x07u, 0x6Eu, 0x55u, 0x90u, 0x3Cu
    };
    static const uint8_t challenge_absent[] = {
        0x00u, 0x01u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u
    };
    static const uint8_t reserved_op[] = {
        0x00u, 0x00u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u
    };
    static const uint8_t all_zero[] = {
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    static const uint8_t unassigned_op[] = {
        0x00u, 0x05u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u
    };
    static const uint8_t experimental_op[] = {
        0x00u, 0xC0u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u
    };
    static const uint8_t wrong_version[] = {
        0x01u, 0x03u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u
    };
    static const uint8_t future_version_unknown_op[] = {
        0x01u, 0xFFu, 0x4Du, 0x19u, 0x42u, 0x01u, 0x9Au, 0x3Cu, 0x05u, 0x17u
    };
    static const uint8_t zero_migration[] = {
        0x00u, 0x03u, 0x00u, 0x00u, 0x00u, 0x00u, 0x9Au, 0x3Cu, 0x05u, 0x17u
    };
    static const uint8_t zero_session[] = {
        0x00u, 0x03u, 0x4Du, 0x19u, 0x42u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u
    };

    /* Truncation is reported separately from malformation throughout, because
     * a carriage must be able to tell "more may arrive" from "never valid". */
    decode_must_fail("truncated_header", vec_commit, 9u, MCL_LINK_ERR_TRUNCATED);
    decode_must_fail("empty", vec_commit, 0u, MCL_LINK_ERR_TRUNCATED);
    decode_must_fail("truncated_challenge", truncated_challenge,
                     sizeof(truncated_challenge), MCL_LINK_ERR_TRUNCATED);
    decode_must_fail("challenge_absent_on_path_challenge", challenge_absent,
                     sizeof(challenge_absent), MCL_LINK_ERR_TRUNCATED);

    decode_must_fail("trailing_byte", trailing_byte, sizeof(trailing_byte),
                     MCL_LINK_ERR_RANGE);
    decode_must_fail("challenge_on_commit", challenge_on_commit,
                     sizeof(challenge_on_commit), MCL_LINK_ERR_RANGE);
    decode_must_fail("reserved_operation", reserved_op, sizeof(reserved_op),
                     MCL_LINK_ERR_RANGE);
    decode_must_fail("all_zero", all_zero, sizeof(all_zero), MCL_LINK_ERR_RANGE);
    decode_must_fail("unassigned_operation", unassigned_op,
                     sizeof(unassigned_op), MCL_LINK_ERR_RANGE);
    decode_must_fail("experimental_operation", experimental_op,
                     sizeof(experimental_op), MCL_LINK_ERR_RANGE);
    decode_must_fail("zero_migration_ref", zero_migration,
                     sizeof(zero_migration), MCL_LINK_ERR_RANGE);
    decode_must_fail("zero_session_ref", zero_session, sizeof(zero_session),
                     MCL_LINK_ERR_RANGE);

    /* Version is checked before operation, so a future version reports the
     * version even when its operation is also unknown here. */
    decode_must_fail("wrong_control_version", wrong_version,
                     sizeof(wrong_version), MCL_LINK_ERR_INCOMPATIBLE_VERSION);
    decode_must_fail("future_version_unknown_operation",
                     future_version_unknown_op,
                     sizeof(future_version_unknown_op),
                     MCL_LINK_ERR_INCOMPATIBLE_VERSION);
}

static void test_encode_refuses(void)
{
    mcl_handoff_control_t control;
    uint8_t out[MCL_HANDOFF_CONTROL_MAX_SIZE];
    size_t written = 0u;

    CHECK_STATUS(mcl_handoff_make_commit(&control, VEC_MIGRATION_REF,
                                         VEC_SESSION_REF),
                 MCL_LINK_OK);

    CHECK_STATUS(mcl_handoff_control_encode(&control, out, 9u, &written),
                 MCL_LINK_ERR_RANGE);
    CHECK_STATUS(mcl_handoff_control_encode(NULL, out, sizeof(out), &written),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_handoff_control_encode(&control, NULL, sizeof(out), &written),
                 MCL_LINK_ERR_INVALID_ARGUMENT);

    /*
     * A struct that disagrees with itself. A COMMIT still carrying a challenge
     * from a previous use must not encode: it would emit bytes the caller did
     * not mean, and the length would silently disagree with the operation.
     */
    control.challenge_present = 1u;
    CHECK_STATUS(mcl_handoff_control_encode(&control, out, sizeof(out), &written),
                 MCL_LINK_ERR_INVALID_ARGUMENT);

    CHECK_STATUS(mcl_handoff_make_path_challenge(&control, VEC_MIGRATION_REF,
                                                 VEC_SESSION_REF, vec_challenge),
                 MCL_LINK_OK);
    control.challenge_present = 0u;
    CHECK_STATUS(mcl_handoff_control_encode(&control, out, sizeof(out), &written),
                 MCL_LINK_ERR_INVALID_ARGUMENT);

    /* An operation this build cannot state the meaning of is never emitted. */
    CHECK_STATUS(mcl_handoff_make_commit(&control, VEC_MIGRATION_REF,
                                         VEC_SESSION_REF),
                 MCL_LINK_OK);
    control.operation = 200u;
    CHECK_STATUS(mcl_handoff_control_encode(&control, out, sizeof(out), &written),
                 MCL_LINK_ERR_RANGE);
}

static void test_builders_refuse_zero_refs(void)
{
    mcl_handoff_control_t control;

    CHECK_STATUS(mcl_handoff_make_commit(&control, 0u, VEC_SESSION_REF),
                 MCL_LINK_ERR_RANGE);
    CHECK_STATUS(mcl_handoff_make_commit(&control, VEC_MIGRATION_REF, 0u),
                 MCL_LINK_ERR_RANGE);
    CHECK_STATUS(mcl_handoff_make_confirm(&control, 0u, 0u),
                 MCL_LINK_ERR_RANGE);
    CHECK_STATUS(mcl_handoff_make_path_challenge(&control, VEC_MIGRATION_REF,
                                                 VEC_SESSION_REF, NULL),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_handoff_make_path_response(NULL, VEC_MIGRATION_REF,
                                                VEC_SESSION_REF, vec_challenge),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_handoff_control_decode(vec_commit, sizeof(vec_commit), NULL),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_handoff_control_decode(NULL, 10u, &control),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
}

/*
 * Every byte value in the operation field, at both lengths. Exhaustive because
 * the field is one byte, so "assigned values decode and nothing else does" can
 * be proven rather than sampled.
 */
static void test_operation_space_exhaustive(void)
{
    uint8_t buffer[MCL_HANDOFF_CONTROL_MAX_SIZE];
    mcl_handoff_control_t control;
    unsigned op;
    unsigned i;

    for (i = 0u; i < MCL_HANDOFF_CONTROL_MAX_SIZE; ++i) {
        buffer[i] = 0u;
    }
    buffer[0] = MCL_HANDOFF_CONTROL_VERSION;
    buffer[2] = 0x4Du; buffer[3] = 0x19u; buffer[4] = 0x42u; buffer[5] = 0x01u;
    buffer[6] = 0x9Au; buffer[7] = 0x3Cu; buffer[8] = 0x05u; buffer[9] = 0x17u;

    for (op = 0u; op <= 255u; ++op) {
        size_t expected_size = mcl_handoff_control_encoded_size((uint8_t)op);
        mcl_link_status_t at_10;
        mcl_link_status_t at_18;

        buffer[1] = (uint8_t)op;
        at_10 = mcl_handoff_control_decode(buffer, 10u, &control);
        at_18 = mcl_handoff_control_decode(buffer, 18u, &control);

        if (expected_size == 10u) {
            CHECK_TRUE(at_10 == MCL_LINK_OK);
            CHECK_TRUE(at_18 == MCL_LINK_ERR_RANGE);
        } else if (expected_size == 18u) {
            CHECK_TRUE(at_10 == MCL_LINK_ERR_TRUNCATED);
            CHECK_TRUE(at_18 == MCL_LINK_OK);
        } else {
            CHECK_TRUE(at_10 == MCL_LINK_ERR_RANGE);
            CHECK_TRUE(at_18 == MCL_LINK_ERR_RANGE);
        }
    }
}

/*
 * Every non-zero control version must be reported as a version mismatch,
 * whatever follows it. Pins the ordering of the decode rules against a future
 * refactor that checks the operation first.
 */
static void test_version_space_exhaustive(void)
{
    uint8_t buffer[MCL_HANDOFF_CONTROL_MAX_SIZE];
    mcl_handoff_control_t control;
    unsigned version;
    unsigned i;

    for (i = 0u; i < MCL_HANDOFF_CONTROL_MAX_SIZE; ++i) {
        buffer[i] = 0xFFu;
    }
    buffer[1] = MCL_HANDOFF_OP_COMMIT;

    for (version = 1u; version <= 255u; ++version) {
        buffer[0] = (uint8_t)version;
        CHECK_TRUE(mcl_handoff_control_decode(buffer, 10u, &control) ==
                   MCL_LINK_ERR_INCOMPATIBLE_VERSION);
        CHECK_TRUE(mcl_handoff_control_decode(buffer, 18u, &control) ==
                   MCL_LINK_ERR_INCOMPATIBLE_VERSION);
        /* Even too short to hold a complete header, truncation is reported
         * first: the version has not been read from a complete control. */
        CHECK_TRUE(mcl_handoff_control_decode(buffer, 5u, &control) ==
                   MCL_LINK_ERR_TRUNCATED);
    }
}

int main(void)
{
    test_sizes();
    test_encode_matches_vectors();
    test_decode_matches_vectors();
    test_decode_clears_stale_challenge();
    test_negative_vectors();
    test_encode_refuses();
    test_builders_refuse_zero_refs();
    test_operation_space_exhaustive();
    test_version_space_exhaustive();

    printf("mcl-link handoff control tests passed\n");
    return 0;
}
