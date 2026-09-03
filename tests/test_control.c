/*
 * Link controls: ACK, NACK, KEEPALIVE, CLOSE, and the sequence window.
 *
 * These four classes were specified in spec/link-frame-classes-v0.1.md and then
 * had no code for a while, which is a specific kind of hazard: a decoder that
 * ACCEPTS a class whose payload nobody has implemented invites two vendors to
 * invent one each, and both will decode successfully.
 *
 * Most of this file is refusals. What decides whether two independent
 * implementations agree is what gets rejected.
 */

#include "mcl/control.h"

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

static void test_ack_round_trip(void)
{
    mcl_link_ack_t ack;
    mcl_link_ack_t decoded;
    uint8_t buffer[8];
    size_t written = 0u;

    CHECK_STATUS(mcl_link_make_ack(&ack, 0x1234u), MCL_LINK_OK);
    CHECK_TRUE(ack.reason == 0u);
    CHECK_STATUS(mcl_link_ack_encode(&ack, 1u, buffer, sizeof(buffer), &written),
                 MCL_LINK_OK);
    CHECK_TRUE(written == MCL_LINK_ACK_SIZE);

    /* The exact bytes, so a second implementation has something to compare
     * against rather than a round trip that agrees with itself. */
    CHECK_TRUE(buffer[0] == 0x00u);
    CHECK_TRUE(buffer[1] == 0x00u);
    CHECK_TRUE(buffer[2] == 0x12u);
    CHECK_TRUE(buffer[3] == 0x34u);

    CHECK_STATUS(mcl_link_ack_decode(buffer, written, 1u, &decoded), MCL_LINK_OK);
    CHECK_TRUE(decoded.control_version == MCL_LINK_CONTROL_VERSION);
    CHECK_TRUE(decoded.reason == 0u);
    CHECK_TRUE(decoded.acked_sequence == 0x1234u);
}

static void test_nack_round_trip(void)
{
    mcl_link_ack_t nack;
    mcl_link_ack_t decoded;
    uint8_t buffer[8];
    size_t written = 0u;

    CHECK_STATUS(mcl_link_make_nack(&nack, 0xFFFEu,
                                    MCL_LINK_NACK_POLICY_REFUSED), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_ack_encode(&nack, 0u, buffer, sizeof(buffer), &written),
                 MCL_LINK_OK);
    CHECK_TRUE(written == MCL_LINK_NACK_SIZE);
    CHECK_TRUE(buffer[0] == 0x00u);
    CHECK_TRUE(buffer[1] == 0x02u);
    CHECK_TRUE(buffer[2] == 0xFFu);
    CHECK_TRUE(buffer[3] == 0xFEu);

    CHECK_STATUS(mcl_link_ack_decode(buffer, written, 0u, &decoded), MCL_LINK_OK);
    CHECK_TRUE(decoded.reason == MCL_LINK_NACK_POLICY_REFUSED);
    CHECK_TRUE(decoded.acked_sequence == 0xFFFEu);

    /* Every assigned reason survives the round trip. */
    {
        uint8_t r;
        for (r = 1u; r < MCL_LINK_NACK_REASON_COUNT; ++r) {
            CHECK_STATUS(mcl_link_make_nack(&nack, 1u, r), MCL_LINK_OK);
            CHECK_STATUS(mcl_link_ack_encode(&nack, 0u, buffer, sizeof(buffer),
                                             &written), MCL_LINK_OK);
            CHECK_STATUS(mcl_link_ack_decode(buffer, written, 0u, &decoded),
                         MCL_LINK_OK);
            CHECK_TRUE(decoded.reason == r);
        }
    }
}

/*
 * AN ACK AND A NACK ARE NOT INTERCHANGEABLE, EVEN THOUGH THEY SHARE A LAYOUT.
 *
 * The frame CLASS says which one a payload is. If the decoder did not enforce
 * that, a NACK decoded as an ACK would report a refusal nobody reads -- the
 * sender would see an acknowledgement where the peer had actually declined.
 */
static void test_ack_and_nack_are_not_interchangeable(void)
{
    mcl_link_ack_t control;
    mcl_link_ack_t decoded;
    uint8_t buffer[8];
    size_t written = 0u;

    /* A NACK's bytes must not decode as an ACK. */
    CHECK_STATUS(mcl_link_make_nack(&control, 7u, MCL_LINK_NACK_STATE_REFUSED),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_link_ack_encode(&control, 0u, buffer, sizeof(buffer),
                                     &written), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_ack_decode(buffer, written, 1u, &decoded),
                 MCL_LINK_ERR_RANGE);

    /* An ACK's bytes must not decode as a NACK: reason 0 is reserved, so an
     * ACK read as a NACK would be a refusal with no stated reason. */
    CHECK_STATUS(mcl_link_make_ack(&control, 7u), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_ack_encode(&control, 1u, buffer, sizeof(buffer),
                                     &written), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_ack_decode(buffer, written, 0u, &decoded),
                 MCL_LINK_ERR_RANGE);

    /* An ACK carrying a reason is never emitted. */
    control.reason = 3u;
    CHECK_STATUS(mcl_link_ack_encode(&control, 1u, buffer, sizeof(buffer),
                                     &written), MCL_LINK_ERR_INVALID_ARGUMENT);
}

static void test_close_round_trip(void)
{
    mcl_link_close_t close_control;
    mcl_link_close_t decoded;
    uint8_t buffer[4];
    size_t written = 0u;
    uint8_t r;

    CHECK_STATUS(mcl_link_make_close(&close_control,
                                     MCL_LINK_CLOSE_TRANSPORT_LOST),
                 MCL_LINK_OK);
    CHECK_STATUS(mcl_link_close_encode(&close_control, buffer, sizeof(buffer),
                                       &written), MCL_LINK_OK);
    CHECK_TRUE(written == MCL_LINK_CLOSE_SIZE);
    CHECK_TRUE(buffer[0] == 0x00u);
    CHECK_TRUE(buffer[1] == 0x04u);
    CHECK_STATUS(mcl_link_close_decode(buffer, written, &decoded), MCL_LINK_OK);
    CHECK_TRUE(decoded.reason == MCL_LINK_CLOSE_TRANSPORT_LOST);

    for (r = 1u; r < MCL_LINK_CLOSE_REASON_COUNT; ++r) {
        CHECK_STATUS(mcl_link_make_close(&close_control, r), MCL_LINK_OK);
        CHECK_STATUS(mcl_link_close_encode(&close_control, buffer,
                                           sizeof(buffer), &written),
                     MCL_LINK_OK);
        CHECK_STATUS(mcl_link_close_decode(buffer, written, &decoded),
                     MCL_LINK_OK);
        CHECK_TRUE(decoded.reason == r);
    }
}

/*
 * "Empty" is a contract, not the absence of one.
 *
 * A non-empty KEEPALIVE is refused rather than ignored. Accepting bytes nobody
 * has defined is how an undocumented sub-protocol appears between two vendors:
 * one starts putting something there, the other starts reading it, and the
 * specification has acquired a field it never described.
 */
static void test_keepalive_is_explicitly_empty(void)
{
    CHECK_STATUS(mcl_link_keepalive_check(0u), MCL_LINK_OK);
    CHECK_STATUS(mcl_link_keepalive_check(1u), MCL_LINK_ERR_RANGE);
    CHECK_STATUS(mcl_link_keepalive_check(1024u), MCL_LINK_ERR_RANGE);
}

/* Exact lengths, reserved values, unassigned reasons, unknown versions. */
static void test_refusals(void)
{
    mcl_link_ack_t ack;
    mcl_link_close_t close_control;
    uint8_t buffer[8];
    size_t written = 0u;

    CHECK_STATUS(mcl_link_make_ack(NULL, 0u), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_make_nack(NULL, 0u, 1u), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_make_close(NULL, 1u), MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_ack_encode(NULL, 1u, buffer, sizeof(buffer), &written),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_ack_decode(NULL, 4u, 1u, &ack),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_close_decode(buffer, 2u, NULL),
                 MCL_LINK_ERR_INVALID_ARGUMENT);

    /* Reserved and unassigned values are never built. */
    CHECK_STATUS(mcl_link_make_nack(&ack, 0u, MCL_LINK_NACK_RESERVED),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_make_nack(&ack, 0u, 200u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_make_close(&close_control, MCL_LINK_CLOSE_RESERVED),
                 MCL_LINK_ERR_INVALID_ARGUMENT);
    CHECK_STATUS(mcl_link_make_close(&close_control, 200u),
                 MCL_LINK_ERR_INVALID_ARGUMENT);

    /*
     * Length is EXACT, and short is reported differently from long. A short
     * buffer may become a valid control once more bytes arrive, so a stream
     * carriage waits; a long one never becomes valid, so the carriage must
     * resynchronise. Reporting both identically forces a carriage to choose
     * between stalling on corruption and discarding recoverable reads.
     */
    (void)mcl_link_make_ack(&ack, 5u);
    (void)mcl_link_ack_encode(&ack, 1u, buffer, sizeof(buffer), &written);
    CHECK_STATUS(mcl_link_ack_decode(buffer, 3u, 1u, &ack),
                 MCL_LINK_ERR_TRUNCATED);
    CHECK_STATUS(mcl_link_ack_decode(buffer, 5u, 1u, &ack), MCL_LINK_ERR_RANGE);
    CHECK_STATUS(mcl_link_close_decode(buffer, 1u, &close_control),
                 MCL_LINK_ERR_TRUNCATED);
    CHECK_STATUS(mcl_link_close_decode(buffer, 3u, &close_control),
                 MCL_LINK_ERR_RANGE);

    /* A control version this build does not implement is refused before any
     * later field is interpreted -- reading them would be reading them under a
     * layout this build has not agreed to. */
    buffer[0] = 0x01u;
    buffer[1] = 0x00u;
    buffer[2] = 0x00u;
    buffer[3] = 0x00u;
    CHECK_STATUS(mcl_link_ack_decode(buffer, 4u, 1u, &ack),
                 MCL_LINK_ERR_INCOMPATIBLE_VERSION);
    CHECK_STATUS(mcl_link_close_decode(buffer, 2u, &close_control),
                 MCL_LINK_ERR_INCOMPATIBLE_VERSION);

    /* An unassigned NACK reason on the wire is refused rather than passed
     * through with a number the caller cannot look up. */
    buffer[0] = 0x00u;
    buffer[1] = (uint8_t)MCL_LINK_NACK_REASON_COUNT;
    CHECK_STATUS(mcl_link_ack_decode(buffer, 4u, 0u, &ack), MCL_LINK_ERR_RANGE);

    /* Capacity is checked before anything is written. */
    (void)mcl_link_make_ack(&ack, 5u);
    CHECK_STATUS(mcl_link_ack_encode(&ack, 1u, buffer, 3u, &written),
                 MCL_LINK_ERR_RANGE);
    (void)mcl_link_make_close(&close_control, MCL_LINK_CLOSE_NORMAL);
    CHECK_STATUS(mcl_link_close_encode(&close_control, buffer, 1u, &written),
                 MCL_LINK_ERR_RANGE);
}

/*
 * SEQUENCE WRAP.
 *
 * A delayed acknowledgement of sequence 7 is indistinguishable from an
 * acknowledgement of the sequence 7 that arrives 65536 frames later, and a
 * sender that matches the wrong one believes a frame arrived that never did.
 * The window is what makes the two distinguishable.
 */
static void test_sequence_window(void)
{
    uint8_t live = 0u;

    CHECK_STATUS(mcl_link_sequence_is_live(0u, 0u, NULL),
                 MCL_LINK_ERR_INVALID_ARGUMENT);

    /* The frame just sent is live. */
    CHECK_STATUS(mcl_link_sequence_is_live(100u, 100u, &live), MCL_LINK_OK);
    CHECK_TRUE(live == 1u);

    /* Anything inside the window, counting backwards, is live. */
    CHECK_STATUS(mcl_link_sequence_is_live(100u, 99u, &live), MCL_LINK_OK);
    CHECK_TRUE(live == 1u);

    /* The last live value, and the first dead one. */
    CHECK_STATUS(mcl_link_sequence_is_live(
        20000u, (uint16_t)(20000u - (MCL_LINK_SEQUENCE_WINDOW_MAX - 1u)),
        &live), MCL_LINK_OK);
    CHECK_TRUE(live == 1u);
    CHECK_STATUS(mcl_link_sequence_is_live(
        20000u, (uint16_t)(20000u - MCL_LINK_SEQUENCE_WINDOW_MAX), &live),
        MCL_LINK_OK);
    CHECK_TRUE(live == 0u);

    /* A sequence AHEAD of what was sent has not been sent, so nothing can
     * acknowledge it. */
    CHECK_STATUS(mcl_link_sequence_is_live(100u, 101u, &live), MCL_LINK_OK);
    CHECK_TRUE(live == 0u);

    /*
     * ACROSS THE WRAP POINT, which is the case the whole rule exists for.
     * Sequence 0xFFFD is three before 0x0000, and must still be live; the
     * arithmetic is modulo 65536, so the wrap needs no special case.
     */
    CHECK_STATUS(mcl_link_sequence_is_live(0x0000u, 0xFFFDu, &live), MCL_LINK_OK);
    CHECK_TRUE(live == 1u);
    CHECK_STATUS(mcl_link_sequence_is_live(0x0002u, 0xFFFFu, &live), MCL_LINK_OK);
    CHECK_TRUE(live == 1u);

    /*
     * And the collision the window prevents: half a space away is NOT live,
     * so an acknowledgement delayed by 32768 frames cannot be mistaken for a
     * current one.
     */
    CHECK_STATUS(mcl_link_sequence_is_live(0u, 0x8000u, &live), MCL_LINK_OK);
    CHECK_TRUE(live == 0u);
    CHECK_STATUS(mcl_link_sequence_is_live(0u, 0x4000u, &live), MCL_LINK_OK);
    CHECK_TRUE(live == 0u);
}

int main(void)
{
    test_ack_round_trip();
    test_nack_round_trip();
    test_ack_and_nack_are_not_interchangeable();
    test_close_round_trip();
    test_keepalive_is_explicitly_empty();
    test_refusals();
    test_sequence_window();

    printf("mcl_link_control: %d checks passed\n", g_checks);
    printf("NOTE: an ACK is a statement about carriage, never about meaning.\n");
    return 0;
}
