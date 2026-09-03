/*
 * Minimum capability and version negotiation.
 *
 * Most of this file is refusals and the symmetry property. What decides whether
 * two independent implementations agree is what gets rejected, and -- uniquely
 * for this mechanism -- whether both peers compute the same answer from the
 * same two inputs regardless of which of them does the computing.
 *
 * Covers every obligation in spec/link-negotiation-v1.md section 8.
 */

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

/* A capability that is valid in every respect, for tests that damage one field
 * at a time. */
static mcl_link_capability_t good_capability(void)
{
    mcl_link_capability_t cap;
    CHECK_STATUS(mcl_link_make_capability(&cap, 0x0003u, 0x0001u, 1048u, 0u),
                 MCL_LINK_OK);
    return cap;
}

/*
 * The floor is derived, never written as a literal. If any of the three
 * contributing sizes changes and the derivation is not revisited, this fails
 * rather than silently negotiating a link that cannot carry a HANDOFF.
 */
static void test_floor_is_derived(void)
{
    printf("[TEST] the frame floor equals its derivation\n");

    CHECK_TRUE(MCL_LINK_NEGOTIATED_FRAME_FLOOR ==
               (MCL_LINK_FRAME_MIN_SIZE +
                MCL_LINK_FRAME_MAX_OPTIONAL +
                MCL_HANDOFF_CONTROL_MAX_SIZE));

    /* And it really is large enough for the two things it must carry. */
    CHECK_TRUE(MCL_LINK_NEGOTIATED_FRAME_FLOOR >=
               MCL_LINK_FRAME_MIN_SIZE + MCL_HANDOFF_CONTROL_MAX_SIZE);
    CHECK_TRUE(MCL_HANDOFF_CONTROL_MAX_SIZE >= 17u); /* largest Tier-0 object */
}

static void test_capability_round_trip(void)
{
    mcl_link_capability_t cap;
    mcl_link_capability_t back;
    uint8_t buffer[MCL_LINK_CAPABILITY_SIZE];
    size_t written = 0u;

    printf("[TEST] CAPABILITY round trip is exact\n");

    CHECK_STATUS(mcl_link_make_capability(&cap, 0x0005u, 0x0003u, 512u, 0u),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_link_capability_encode(&cap, buffer, sizeof(buffer),
                                            &written), MCL_LINK_OK);
    CHECK_TRUE(written == MCL_LINK_CAPABILITY_SIZE);

    CHECK_STATUS(mcl_link_capability_decode(buffer, written, &back),
                 MCL_LINK_OK);
    CHECK_TRUE(back.control_version == cap.control_version);
    CHECK_TRUE(back.wire_majors == cap.wire_majors);
    CHECK_TRUE(back.link_majors == cap.link_majors);
    CHECK_TRUE(back.max_frame == cap.max_frame);
    CHECK_TRUE(back.features == cap.features);
}

static void test_negotiation_round_trip(void)
{
    mcl_link_negotiation_t sel;
    mcl_link_negotiation_t back;
    uint8_t buffer[MCL_LINK_NEGOTIATION_SIZE];
    size_t written = 0u;
    mcl_link_capability_t a = good_capability();
    mcl_link_capability_t b = good_capability();

    printf("[TEST] NEGOTIATION round trip is exact\n");

    CHECK_STATUS(mcl_link_negotiation_select(&a, &b, &sel), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_negotiation_encode(&sel, buffer, sizeof(buffer),
                                             &written), MCL_LINK_OK);
    CHECK_TRUE(written == MCL_LINK_NEGOTIATION_SIZE);

    CHECK_STATUS(mcl_link_negotiation_decode(buffer, written, &back),
                 MCL_LINK_OK);
    CHECK_TRUE(back.wire_major == sel.wire_major);
    CHECK_TRUE(back.link_major == sel.link_major);
    CHECK_TRUE(back.max_frame == sel.max_frame);
    CHECK_TRUE(back.features == sel.features);
}

static void test_lengths_are_exact(void)
{
    mcl_link_capability_t cap = good_capability();
    mcl_link_capability_t back;
    mcl_link_negotiation_t neg;
    uint8_t buffer[MCL_LINK_CAPABILITY_SIZE + 1u];
    size_t written = 0u;

    printf("[TEST] one byte short is TRUNCATED, one byte long is RANGE\n");

    CHECK_STATUS(mcl_link_capability_encode(&cap, buffer, sizeof(buffer),
                                            &written), MCL_LINK_OK);

    CHECK_STATUS(mcl_link_capability_decode(buffer,
                                            MCL_LINK_CAPABILITY_SIZE - 1u,
                                            &back),
                 MCL_LINK_ERR_TRUNCATED);
    CHECK_STATUS(mcl_link_capability_decode(buffer,
                                            MCL_LINK_CAPABILITY_SIZE + 1u,
                                            &back),
                 MCL_LINK_ERR_RANGE);

    /* A CAPABILITY is not a NEGOTIATION even though both start with a version
     * byte. The lengths differ and that is what refuses the confusion. */
    CHECK_STATUS(mcl_link_negotiation_decode(buffer,
                                             MCL_LINK_CAPABILITY_SIZE,
                                             &neg),
                 MCL_LINK_ERR_RANGE);
}

static void test_wrong_control_version_refused(void)
{
    mcl_link_capability_t cap = good_capability();
    mcl_link_capability_t back;
    mcl_link_negotiation_t neg;
    mcl_link_negotiation_t neg_back;
    uint8_t buffer[MCL_LINK_CAPABILITY_SIZE];
    uint8_t nbuffer[MCL_LINK_NEGOTIATION_SIZE];
    size_t written = 0u;
    mcl_link_capability_t peer = good_capability();

    printf("[TEST] a control version this build has not agreed to is refused\n");

    CHECK_STATUS(mcl_link_capability_encode(&cap, buffer, sizeof(buffer),
                                            &written), MCL_LINK_OK);
    buffer[0] = 1u;
    CHECK_STATUS(mcl_link_capability_decode(buffer, written, &back),
                 MCL_LINK_ERR_INCOMPATIBLE_VERSION);

    CHECK_STATUS(mcl_link_negotiation_select(&cap, &peer, &neg), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_negotiation_encode(&neg, nbuffer, sizeof(nbuffer),
                                             &written), MCL_LINK_OK);
    nbuffer[0] = 9u;
    CHECK_STATUS(mcl_link_negotiation_decode(nbuffer, written, &neg_back),
                 MCL_LINK_ERR_INCOMPATIBLE_VERSION);

    /* And the encoder refuses to produce one. */
    cap.control_version = 3u;
    CHECK_STATUS(mcl_link_capability_encode(&cap, buffer, sizeof(buffer),
                                            &written),
                 MCL_LINK_ERR_INCOMPATIBLE_VERSION);
}

static void test_empty_major_set_refused(void)
{
    mcl_link_capability_t cap;
    mcl_link_capability_t back;
    uint8_t buffer[MCL_LINK_CAPABILITY_SIZE];
    size_t written = 0u;
    mcl_link_capability_t good = good_capability();

    printf("[TEST] a node supporting no major cannot be negotiated with\n");

    CHECK_STATUS(mcl_link_make_capability(&cap, 0u, 0x0001u, 1048u, 0u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_make_capability(&cap, 0x0001u, 0u, 1048u, 0u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);

    /* A peer that sends one anyway is refused at decode, not accepted and then
     * mishandled somewhere later. */
    CHECK_STATUS(mcl_link_capability_encode(&good, buffer, sizeof(buffer),
                                            &written), MCL_LINK_OK);
    buffer[1] = 0u;
    buffer[2] = 0u;
    CHECK_STATUS(mcl_link_capability_decode(buffer, written, &back),
                 MCL_LINK_ERR_RANGE);
}

static void test_below_floor_refused(void)
{
    mcl_link_capability_t cap;
    mcl_link_capability_t back;
    mcl_link_negotiation_t neg;
    uint8_t buffer[MCL_LINK_CAPABILITY_SIZE];
    size_t written = 0u;
    mcl_link_capability_t good = good_capability();

    printf("[TEST] a frame maximum below the floor is refused everywhere\n");

    CHECK_STATUS(mcl_link_make_capability(
                     &cap, 0x0001u, 0x0001u,
                     (uint16_t)(MCL_LINK_NEGOTIATED_FRAME_FLOOR - 1u), 0u),
                 MCL_LINK_ERR_RANGE);

    /* Exactly at the floor is legal: the boundary is inclusive. */
    CHECK_STATUS(mcl_link_make_capability(
                     &cap, 0x0001u, 0x0001u,
                     (uint16_t)MCL_LINK_NEGOTIATED_FRAME_FLOOR, 0u),
                 MCL_LINK_OK);

    /* On the wire too. */
    CHECK_STATUS(mcl_link_capability_encode(&good, buffer, sizeof(buffer),
                                            &written), MCL_LINK_OK);
    buffer[5] = 0u;
    buffer[6] = 1u;
    CHECK_STATUS(mcl_link_capability_decode(buffer, written, &back),
                 MCL_LINK_ERR_RANGE);

    /* And a selection that would land below it is refused rather than
     * returning a link that cannot carry the protocol's own frames. */
    neg.control_version = MCL_LINK_CONTROL_VERSION;
    neg.wire_major = 0u;
    neg.link_major = 0u;
    neg.max_frame = 1u;
    neg.features = 0u;
    CHECK_STATUS(mcl_link_negotiation_check(&neg, &good, NULL),
                 MCL_LINK_ERR_RANGE);
}

static void test_disjoint_majors_refused(void)
{
    mcl_link_capability_t a;
    mcl_link_capability_t b;
    mcl_link_negotiation_t sel;

    printf("[TEST] two implementations with nothing in common are told so\n");

    CHECK_STATUS(mcl_link_make_capability(&a, 0x0001u, 0x0001u, 1048u, 0u),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_link_make_capability(&b, 0x0002u, 0x0001u, 1048u, 0u),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_link_negotiation_select(&a, &b, &sel),
                 MCL_LINK_ERR_RANGE);

    /* Disjoint LINK majors fail the same way. */
    CHECK_STATUS(mcl_link_make_capability(&b, 0x0001u, 0x0004u, 1048u, 0u),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_link_negotiation_select(&a, &b, &sel),
                 MCL_LINK_ERR_RANGE);
}

/*
 * THE PROPERTY THE WHOLE DESIGN RESTS ON.
 *
 * Exhaustive over a reduced but complete space: every combination of 4-bit
 * major masks for both peers, crossed with several frame sizes and feature
 * sets. If this ever fails, glare stops being self-resolving and the mechanism
 * needs a tiebreaker it does not have.
 */
static void test_selection_is_symmetric(void)
{
    uint16_t aw;
    uint16_t bw;
    unsigned cases = 0u;
    unsigned agreed = 0u;

    printf("[TEST] the selection function is symmetric (exhaustive)\n");

    for (aw = 1u; aw < 16u; ++aw) {
        for (bw = 1u; bw < 16u; ++bw) {
            unsigned k;
            static const uint16_t frames[3] = { 42u, 512u, 1048u };
            static const uint16_t feats[3] = { 0x0000u, 0x00FFu, 0xFF0Fu };

            for (k = 0u; k < 3u; ++k) {
                mcl_link_capability_t a;
                mcl_link_capability_t b;
                mcl_link_negotiation_t ab;
                mcl_link_negotiation_t ba;
                mcl_link_status_t sab;
                mcl_link_status_t sba;

                CHECK_STATUS(mcl_link_make_capability(
                                 &a, aw, aw, frames[k], feats[k]),
                             MCL_LINK_OK);
                CHECK_STATUS(mcl_link_make_capability(
                                 &b, bw, bw, frames[(k + 1u) % 3u],
                                 feats[(k + 2u) % 3u]),
                             MCL_LINK_OK);

                sab = mcl_link_negotiation_select(&a, &b, &ab);
                sba = mcl_link_negotiation_select(&b, &a, &ba);
                ++cases;

                /* Both directions must agree even about failing. */
                CHECK_TRUE(sab == sba);
                if (sab == MCL_LINK_OK) {
                    CHECK_TRUE(ab.wire_major == ba.wire_major);
                    CHECK_TRUE(ab.link_major == ba.link_major);
                    CHECK_TRUE(ab.max_frame == ba.max_frame);
                    CHECK_TRUE(ab.features == ba.features);
                    ++agreed;
                }
            }
        }
    }

    printf("       %u ordered pairs, %u produced a selection, 0 disagreed\n",
           cases, agreed);
    CHECK_TRUE(cases == 675u);
    CHECK_TRUE(agreed > 0u);
}

/*
 * Glare: both peers send CAPABILITY, both then hold both sets, both compute.
 * Because the function is symmetric they emit identical BYTES, which is the
 * property that removes the need for a tiebreaker. Compared at the byte level
 * rather than field by field, because bytes are what a peer actually sees.
 */
static void test_glare_produces_identical_bytes(void)
{
    mcl_link_capability_t a;
    mcl_link_capability_t b;
    mcl_link_negotiation_t from_a;
    mcl_link_negotiation_t from_b;
    uint8_t bytes_a[MCL_LINK_NEGOTIATION_SIZE];
    uint8_t bytes_b[MCL_LINK_NEGOTIATION_SIZE];
    size_t wa = 0u;
    size_t wb = 0u;
    size_t i;

    printf("[TEST] simultaneous CAPABILITY yields identical NEGOTIATION bytes\n");

    CHECK_STATUS(mcl_link_make_capability(&a, 0x000Bu, 0x0003u, 900u, 0x0F0Fu),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_link_make_capability(&b, 0x0007u, 0x0001u, 512u, 0x00FFu),
                 MCL_LINK_OK);

    /* A computes with itself local; B computes with itself local. */
    CHECK_STATUS(mcl_link_negotiation_select(&a, &b, &from_a), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_negotiation_select(&b, &a, &from_b), MCL_LINK_OK);

    CHECK_STATUS(mcl_link_negotiation_encode(&from_a, bytes_a, sizeof(bytes_a),
                                             &wa), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_negotiation_encode(&from_b, bytes_b, sizeof(bytes_b),
                                             &wb), MCL_LINK_OK);
    CHECK_TRUE(wa == wb);
    for (i = 0u; i < wa; ++i) {
        CHECK_TRUE(bytes_a[i] == bytes_b[i]);
    }

    /* And each accepts the other's frame under the strong check. */
    CHECK_STATUS(mcl_link_negotiation_check(&from_b, &a, &b), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_negotiation_check(&from_a, &b, &a), MCL_LINK_OK);

    /* Sanity: the selection really is the highest common major and the
     * smaller frame, not merely something both sides agree on. */
    /* 0x0B = 1011, 0x07 = 0111, AND = 0011 -> highest common bit is 1. */
    CHECK_TRUE(from_a.wire_major == 1u);
    CHECK_TRUE(from_a.link_major == 0u);  /* 0x0003 & 0x0001 = 0x0001 */
    CHECK_TRUE(from_a.max_frame == 512u);
    CHECK_TRUE(from_a.features == (uint16_t)(0x0F0Fu & 0x00FFu));
}

static void test_check_rejects_disagreement(void)
{
    mcl_link_capability_t a;
    mcl_link_capability_t b;
    mcl_link_negotiation_t sel;

    printf("[TEST] a NEGOTIATION disagreeing with the computation is refused\n");

    CHECK_STATUS(mcl_link_make_capability(&a, 0x0007u, 0x0003u, 1048u, 0u),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_link_make_capability(&b, 0x0003u, 0x0003u, 1048u, 0u),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_link_negotiation_select(&a, &b, &sel), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_negotiation_check(&sel, &a, &b), MCL_LINK_OK);

    /* Each field, damaged one at a time, under the strong branch. */
    {
        mcl_link_negotiation_t bad = sel;
        bad.wire_major = (uint8_t)(sel.wire_major - 1u);
        CHECK_STATUS(mcl_link_negotiation_check(&bad, &a, &b),
                     MCL_LINK_ERR_RANGE);
    }
    {
        mcl_link_negotiation_t bad = sel;
        bad.link_major = (uint8_t)(sel.link_major - 1u);
        CHECK_STATUS(mcl_link_negotiation_check(&bad, &a, &b),
                     MCL_LINK_ERR_RANGE);
    }
    {
        mcl_link_negotiation_t bad = sel;
        bad.max_frame = (uint16_t)(sel.max_frame - 1u);
        CHECK_STATUS(mcl_link_negotiation_check(&bad, &a, &b),
                     MCL_LINK_ERR_RANGE);
    }
    {
        mcl_link_negotiation_t bad = sel;
        bad.features = (uint16_t)(sel.features ^ 0x0001u);
        CHECK_STATUS(mcl_link_negotiation_check(&bad, &a, &b),
                     MCL_LINK_ERR_RANGE);
    }
}

static void test_check_without_peer_is_weaker_and_says_so(void)
{
    mcl_link_capability_t local;
    mcl_link_negotiation_t proposal;

    printf("[TEST] without the peer set, only local legality is checkable\n");

    CHECK_STATUS(mcl_link_make_capability(&local, 0x0007u, 0x0003u, 1048u,
                                          0x00F0u), MCL_LINK_OK);

    /* The highest this node could do is Wire major 2. A peer proposing the
     * LOWER major 0 is accepted: it is legal here, and without the peer's
     * advertisement nothing can tell a downgrade from an honest limitation.
     * This is the documented weakness, asserted so it stays deliberate. */
    proposal.control_version = MCL_LINK_CONTROL_VERSION;
    proposal.wire_major = 0u;
    proposal.link_major = 0u;
    proposal.max_frame = 1048u;
    proposal.features = 0x00F0u;
    CHECK_STATUS(mcl_link_negotiation_check(&proposal, &local, NULL),
                 MCL_LINK_OK);

    /* But anything outside the local capability is still refused. */
    proposal.wire_major = 3u; /* bit 3 not set in 0x0007 */
    CHECK_STATUS(mcl_link_negotiation_check(&proposal, &local, NULL),
                 MCL_LINK_ERR_RANGE);

    proposal.wire_major = 0u;
    proposal.max_frame = 2000u; /* larger than this node will accept */
    CHECK_STATUS(mcl_link_negotiation_check(&proposal, &local, NULL),
                 MCL_LINK_ERR_RANGE);

    proposal.max_frame = 1048u;
    proposal.features = 0x00F1u; /* bit 0 never offered */
    CHECK_STATUS(mcl_link_negotiation_check(&proposal, &local, NULL),
                 MCL_LINK_ERR_RANGE);
}

/*
 * Unknown feature bits fail closed by construction. There is no rule to get
 * wrong here -- the AND is the rule -- and this test exists to prove the
 * property holds rather than to exercise a branch.
 */
static void test_unknown_features_fail_closed(void)
{
    mcl_link_capability_t known;
    mcl_link_capability_t futuristic;
    mcl_link_negotiation_t sel;

    printf("[TEST] a feature bit this build never heard of is cleared\n");

    CHECK_STATUS(mcl_link_make_capability(&known, 0x0001u, 0x0001u, 1048u,
                                          0x0000u), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_make_capability(&futuristic, 0x0001u, 0x0001u, 1048u,
                                          0xFFFFu), MCL_LINK_OK);

    CHECK_STATUS(mcl_link_negotiation_select(&known, &futuristic, &sel),
                 MCL_LINK_OK);
    CHECK_TRUE(sel.features == 0x0000u);

    /* Symmetric here too: the peer reaches the same empty set. */
    CHECK_STATUS(mcl_link_negotiation_select(&futuristic, &known, &sel),
                 MCL_LINK_OK);
    CHECK_TRUE(sel.features == 0x0000u);

    /* v1 assigns no feature bits at all, so this is the ordinary case. */
    CHECK_STATUS(mcl_link_negotiation_select(&known, &known, &sel),
                 MCL_LINK_OK);
    CHECK_TRUE(sel.features == 0x0000u);
}

/*
 * A second CAPABILITY replaces the first rather than being suppressed as a
 * duplicate. A peer whose configuration changed must be able to say so, and
 * because the selection is a pure function of the current inputs, recomputing
 * is the whole of the handling.
 */
static void test_repeated_capability_replaces(void)
{
    mcl_link_capability_t local;
    mcl_link_capability_t peer_first;
    mcl_link_capability_t peer_second;
    mcl_link_negotiation_t first;
    mcl_link_negotiation_t second;

    printf("[TEST] a repeated CAPABILITY replaces and re-selects\n");

    CHECK_STATUS(mcl_link_make_capability(&local, 0x0007u, 0x0001u, 1048u, 0u),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_link_make_capability(&peer_first, 0x0001u, 0x0001u, 1048u,
                                          0u), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_make_capability(&peer_second, 0x0007u, 0x0001u, 600u,
                                          0u), MCL_LINK_OK);

    CHECK_STATUS(mcl_link_negotiation_select(&local, &peer_first, &first),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_link_negotiation_select(&local, &peer_second, &second),
                 MCL_LINK_OK);

    /* The outcome genuinely changed, so a receiver that suppressed the second
     * advertisement as a duplicate would hold a stale selection. */
    CHECK_TRUE(first.wire_major == 0u);
    CHECK_TRUE(second.wire_major == 2u);
    CHECK_TRUE(first.max_frame == 1048u);
    CHECK_TRUE(second.max_frame == 600u);

    /* And the first selection is no longer acceptable against the new set. */
    CHECK_STATUS(mcl_link_negotiation_check(&first, &local, &peer_second),
                 MCL_LINK_ERR_RANGE);
}

static void test_null_arguments_refused(void)
{
    mcl_link_capability_t cap = good_capability();
    mcl_link_negotiation_t neg;
    uint8_t buffer[MCL_LINK_CAPABILITY_SIZE];
    size_t written = 0u;

    printf("[TEST] null arguments are refused, not dereferenced\n");

    CHECK_STATUS(mcl_link_make_capability(NULL, 1u, 1u, 1048u, 0u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_capability_encode(NULL, buffer, sizeof(buffer),
                                            &written),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_capability_encode(&cap, NULL, sizeof(buffer),
                                            &written),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_capability_decode(NULL, MCL_LINK_CAPABILITY_SIZE,
                                            &cap),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_negotiation_select(&cap, NULL, &neg),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_negotiation_check(NULL, &cap, NULL),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    /* peer == NULL is legal; local == NULL is not. */
    CHECK_STATUS(mcl_link_negotiation_check(&neg, NULL, NULL),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
}

static void test_short_output_buffer_refused(void)
{
    mcl_link_capability_t cap = good_capability();
    mcl_link_negotiation_t sel;
    uint8_t small[4];
    size_t written = 0u;

    printf("[TEST] a buffer too small to hold the control is refused\n");

    CHECK_STATUS(mcl_link_capability_encode(&cap, small, sizeof(small),
                                            &written), MCL_LINK_ERR_RANGE);

    CHECK_STATUS(mcl_link_negotiation_select(&cap, &cap, &sel), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_negotiation_encode(&sel, small, sizeof(small),
                                             &written), MCL_LINK_ERR_RANGE);
}

int main(void)
{
    printf("=== MCL Link capability/version negotiation ===\n");

    test_floor_is_derived();
    test_capability_round_trip();
    test_negotiation_round_trip();
    test_lengths_are_exact();
    test_wrong_control_version_refused();
    test_empty_major_set_refused();
    test_below_floor_refused();
    test_disjoint_majors_refused();
    test_selection_is_symmetric();
    test_glare_produces_identical_bytes();
    test_check_rejects_disagreement();
    test_check_without_peer_is_weaker_and_says_so();
    test_unknown_features_fail_closed();
    test_repeated_capability_replaces();
    test_null_arguments_refused();
    test_short_output_buffer_refused();

    printf("\n%d checks, 0 failed.\n", g_checks);
    return 0;
}
