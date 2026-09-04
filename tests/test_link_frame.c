/*
 * MCL Link frame v0 tests.
 *
 * Positive round trips, and the negative cases the charter requires: unknown
 * version, unknown frame class, non-canonical reserved bits, truncation at
 * every length, and integrity failure.
 */

#include "mcl/link.h"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do {                                   \
    ++tests_run;                                                \
    if (!(cond)) {                                              \
        ++tests_failed;                                         \
        printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
    }                                                           \
} while (0)

/* The canonical Tier-0 PRESENCE payload used throughout the project. */
static const uint8_t k_presence[] = {
    0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x01u, 0x01u, 0x00u, 0x00u, 0x01u, 0x3Cu
};

static void test_minimal_round_trip(void)
{
    mcl_link_frame_t tx, rx;
    uint8_t buf[64];
    size_t written = 0u, consumed = 0u;

    printf("[TEST] minimal frame round trip\n");

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_DATA;
    tx.flags = 0u;
    tx.source_ref = 0x11223344u;
    tx.payload = k_presence;
    tx.payload_len = (uint16_t)sizeof(k_presence);

    CHECK(mcl_link_frame_encoded_size(&tx) == MCL_LINK_FRAME_MIN_SIZE + sizeof(k_presence),
          "minimal encoded size");
    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) == MCL_LINK_OK,
          "encode succeeds");
    CHECK(written == MCL_LINK_FRAME_MIN_SIZE + sizeof(k_presence),
          "written matches computed size");

    memset(&rx, 0, sizeof(rx));
    CHECK(mcl_link_frame_decode(buf, written, &rx, &consumed) == MCL_LINK_OK,
          "decode succeeds");
    CHECK(consumed == written, "consumed matches written");
    CHECK(rx.frame_class == MCL_LINK_CLASS_DATA, "class preserved");
    CHECK(rx.source_ref == 0x11223344u, "source_ref preserved");
    CHECK(rx.payload_len == sizeof(k_presence), "payload_len preserved");
    CHECK(rx.payload != NULL && memcmp(rx.payload, k_presence, sizeof(k_presence)) == 0,
          "payload bytes preserved exactly");
}

static void test_all_optionals_round_trip(void)
{
    mcl_link_frame_t tx, rx;
    uint8_t buf[128];
    size_t written = 0u, consumed = 0u;

    printf("[TEST] all optional fields round trip\n");

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_DATA;
    tx.flags = (uint8_t)(MCL_LINK_FLAG_DESTINATION | MCL_LINK_FLAG_SESSION |
                         MCL_LINK_FLAG_SEQUENCE | MCL_LINK_FLAG_FRESHNESS |
                         MCL_LINK_FLAG_FRAME_CHECK);
    tx.source_ref = 0xDEADBEEFu;
    tx.destination_ref = 0xFEEDFACEu;
    tx.session_ref = 0x01020304u;
    tx.sequence = 0xABCDu;
    tx.freshness_ms = 60000u;
    tx.payload = k_presence;
    tx.payload_len = (uint16_t)sizeof(k_presence);

    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) == MCL_LINK_OK,
          "encode with all optionals");
    CHECK(written == MCL_LINK_FRAME_MIN_SIZE + 16u + sizeof(k_presence),
          "size includes every optional field");

    memset(&rx, 0, sizeof(rx));
    CHECK(mcl_link_frame_decode(buf, written, &rx, &consumed) == MCL_LINK_OK,
          "decode with all optionals");
    CHECK(rx.destination_ref == 0xFEEDFACEu, "destination_ref preserved");
    CHECK(rx.session_ref == 0x01020304u, "session_ref preserved");
    CHECK(rx.sequence == 0xABCDu, "sequence preserved");
    CHECK(rx.freshness_ms == 60000u, "freshness preserved");
    CHECK(consumed == written, "consumed includes integrity field");
}

static void test_every_class_round_trips(void)
{
    unsigned c;

    printf("[TEST] every assigned frame class round trips\n");

    for (c = 0u; c < MCL_LINK_CLASS_COUNT; ++c) {
        mcl_link_frame_t tx, rx;
        uint8_t buf[64];
        size_t written = 0u, consumed = 0u;

        if (c == MCL_LINK_CLASS_ADAPT) {
            /* Reserved; covered by its own test below. */
            continue;
        }

        memset(&tx, 0, sizeof(tx));
        tx.frame_class = (mcl_link_frame_class_t)c;
        tx.source_ref = 0x55667788u;
        tx.payload = k_presence;
        tx.payload_len = (uint16_t)sizeof(k_presence);

        CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) == MCL_LINK_OK,
              "encode class");
        CHECK(mcl_link_frame_decode(buf, written, &rx, &consumed) == MCL_LINK_OK,
              "decode class");
        CHECK(rx.frame_class == (mcl_link_frame_class_t)c, "class value preserved");
    }
}

/*
 * ADAPT IS RESERVED, AND A RESERVED CLASS MUST BE REFUSED.
 *
 * It was assigned for transport adaptation and then nothing was built on it:
 * no payload designed, no mechanism using it, and the cases considered so far
 * are served either by a transport's own adaptation, below MCL entirely, or by
 * a migration, which is specified.
 *
 * Accepting a class whose payload nobody has specified is an interoperability
 * failure waiting for its first independent implementation -- two vendors each
 * invent a payload, and both decode successfully. This is a deliberate
 * behaviour change from earlier builds, which accepted it. Reserving costs
 * nothing and can be undone; specifying it speculatively cannot. See
 * spec/link-frame-classes-v0.1.md section 5.
 */
static void test_adapt_is_reserved(void)
{
    mcl_link_frame_t tx, rx;
    uint8_t buf[64];
    size_t written = 0u, consumed = 0u;

    printf("[TEST] the reserved ADAPT class is refused\n");

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_ADAPT;
    tx.source_ref = 0x55667788u;
    tx.payload = k_presence;
    tx.payload_len = (uint16_t)sizeof(k_presence);

    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) ==
          MCL_LINK_ERR_RANGE,
          "nothing emits a reserved class");

    /* A frame built by hand, as another implementation might, must be refused
     * on decode rather than accepted with an undefined payload. */
    tx.frame_class = MCL_LINK_CLASS_DATA;
    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) == MCL_LINK_OK,
          "a DATA frame of the same shape encodes");
    buf[0] = (uint8_t)((buf[0] & 0xF0u) | (uint8_t)MCL_LINK_CLASS_ADAPT);
    CHECK(mcl_link_frame_decode(buf, written, &rx, &consumed) ==
          MCL_LINK_ERR_RANGE,
          "and the same bytes relabelled ADAPT are refused");
}

static void test_empty_payload(void)
{
    mcl_link_frame_t tx, rx;
    uint8_t buf[32];
    size_t written = 0u, consumed = 0u;

    printf("[TEST] zero-length payload is legal\n");

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_KEEPALIVE;
    tx.source_ref = 1u;
    tx.payload = NULL;
    tx.payload_len = 0u;

    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) == MCL_LINK_OK,
          "encode empty payload");
    CHECK(written == MCL_LINK_FRAME_MIN_SIZE, "empty frame is the minimum size");
    CHECK(mcl_link_frame_decode(buf, written, &rx, &consumed) == MCL_LINK_OK,
          "decode empty payload");
    CHECK(rx.payload_len == 0u, "payload_len zero");
    CHECK(rx.payload == NULL, "payload pointer null when empty");
}

static void test_reject_unknown_version(void)
{
    mcl_link_frame_t tx, rx;
    uint8_t buf[64];
    size_t written = 0u, consumed = 0u;

    printf("[TEST] unknown link major is rejected\n");

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_DATA;
    tx.source_ref = 1u;
    tx.payload = k_presence;
    tx.payload_len = (uint16_t)sizeof(k_presence);
    (void)mcl_link_frame_encode(&tx, buf, sizeof(buf), &written);

    /*
     * Major 1 is CUT and is accepted. This assertion used to be its opposite;
     * inverted rather than deleted, because the frame is byte-identical at
     * both majors and a reader needs to see that stated somewhere.
     */
    buf[0] = (uint8_t)((MCL_LINK_STABLE_MAJOR << 4u) | MCL_LINK_CLASS_DATA);
    CHECK(mcl_link_frame_decode(buf, written, &rx, &consumed) == MCL_LINK_OK,
          "the Stable major is accepted");
    CHECK(rx.link_major == MCL_LINK_STABLE_MAJOR,
          "and the decoder reports which major it arrived under");

    /* Major 2 is unassigned and must still be refused. */
    buf[0] = (uint8_t)((2u << 4u) | MCL_LINK_CLASS_DATA);
    CHECK(mcl_link_frame_decode(buf, written, &rx, &consumed)
              == MCL_LINK_ERR_INCOMPATIBLE_VERSION,
          "an unassigned major is rejected as incompatible");

    /* Encoding at the Stable major differs from major 0 in the nibble and in
     * nothing else -- that is what "the layout did not change" means. */
    {
        uint8_t stable_buf[128];
        size_t stable_written = 0u;
        size_t i;
        CHECK(mcl_link_frame_encode_at_major(MCL_LINK_STABLE_MAJOR, &tx,
                                             stable_buf, sizeof(stable_buf),
                                             &stable_written) == MCL_LINK_OK,
              "a frame encodes at the Stable major");
        CHECK(stable_written == written, "to the same length");
        CHECK(stable_buf[0] ==
                  (uint8_t)((MCL_LINK_STABLE_MAJOR << 4u) |
                            MCL_LINK_CLASS_DATA),
              "with the Stable major in the nibble");
        (void)mcl_link_frame_encode(&tx, buf, sizeof(buf), &written);
        for (i = 1u; i < written; ++i) {
            CHECK(stable_buf[i] == buf[i],
                  "and every other byte identical to major 0");
        }
        CHECK(mcl_link_frame_encode_at_major(2u, &tx, stable_buf,
                                             sizeof(stable_buf),
                                             &stable_written)
                  == MCL_LINK_ERR_INCOMPATIBLE_VERSION,
              "an unassigned major cannot be emitted");
    }
}

static void test_reject_unknown_class(void)
{
    mcl_link_frame_t tx, rx;
    uint8_t buf[64];
    size_t written = 0u, consumed = 0u;

    printf("[TEST] unknown frame class is rejected, not guessed\n");

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_DATA;
    tx.source_ref = 1u;
    tx.payload = k_presence;
    tx.payload_len = (uint16_t)sizeof(k_presence);
    (void)mcl_link_frame_encode(&tx, buf, sizeof(buf), &written);

    buf[0] = 0x0Fu;  /* class 15, unassigned */
    CHECK(mcl_link_frame_decode(buf, written, &rx, &consumed) == MCL_LINK_ERR_RANGE,
          "unassigned class rejected");

    /* Encoding an unassigned class must fail too. */
    tx.frame_class = (mcl_link_frame_class_t)MCL_LINK_CLASS_COUNT;
    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) == MCL_LINK_ERR_RANGE,
          "unassigned class not encodable");
    CHECK(mcl_link_frame_encoded_size(&tx) == 0u, "unassigned class has no size");
}

static void test_reject_reserved_flags(void)
{
    mcl_link_frame_t tx, rx;
    uint8_t buf[64];
    size_t written = 0u, consumed = 0u;
    unsigned bit;

    printf("[TEST] reserved flag bits must be zero\n");

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_DATA;
    tx.source_ref = 1u;
    tx.payload = k_presence;
    tx.payload_len = (uint16_t)sizeof(k_presence);
    (void)mcl_link_frame_encode(&tx, buf, sizeof(buf), &written);

    for (bit = 5u; bit < 8u; ++bit) {
        uint8_t corrupt[64];
        memcpy(corrupt, buf, written);
        corrupt[1] = (uint8_t)(1u << bit);
        CHECK(mcl_link_frame_decode(corrupt, written, &rx, &consumed) == MCL_LINK_ERR_RANGE,
              "reserved bit set is rejected");
    }

    tx.flags = MCL_LINK_FLAG_RESERVED;
    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) == MCL_LINK_ERR_RANGE,
          "reserved bits not encodable");
}

static void test_reject_truncation_at_every_length(void)
{
    mcl_link_frame_t tx, rx;
    uint8_t buf[128];
    size_t written = 0u, consumed = 0u, n;

    printf("[TEST] truncation is rejected at every length\n");

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_HANDOFF;
    tx.flags = (uint8_t)(MCL_LINK_FLAG_DESTINATION | MCL_LINK_FLAG_SESSION |
                         MCL_LINK_FLAG_SEQUENCE | MCL_LINK_FLAG_FRESHNESS |
                         MCL_LINK_FLAG_FRAME_CHECK);
    tx.source_ref = 2u;
    tx.payload = k_presence;
    tx.payload_len = (uint16_t)sizeof(k_presence);
    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) == MCL_LINK_OK,
          "encode for truncation test");

    /*
     * Every short prefix must report truncation specifically, not a generic
     * range error. A stream carriage relies on that distinction to decide
     * between waiting for more bytes and resynchronising, so a regression here
     * would turn a recoverable read into a dropped connection.
     */
    for (n = 0u; n < written; ++n) {
        CHECK(mcl_link_frame_decode(buf, n, &rx, &consumed) == MCL_LINK_ERR_TRUNCATED,
              "short buffer reports truncation, not malformation");
    }
    CHECK(mcl_link_frame_decode(buf, written, &rx, &consumed) == MCL_LINK_OK,
          "full buffer still decodes");
}

/*
 * The counterpart to the truncation test: a complete buffer carrying meaning
 * this version cannot interpret must never be reported as truncated, because
 * no number of additional bytes would make it valid.
 */
static void test_malformed_is_not_truncation(void)
{
    mcl_link_frame_t tx, rx;
    uint8_t buf[128];
    size_t written = 0u, consumed = 0u;

    printf("[TEST] a complete but malformed frame is not reported as truncated\n");

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_DATA;
    tx.source_ref = 0x11223344u;
    tx.payload = k_presence;
    tx.payload_len = (uint16_t)sizeof(k_presence);
    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) == MCL_LINK_OK,
          "encode baseline");

    buf[0] = (uint8_t)((MCL_LINK_FRAME_MAJOR << 4u) | 0x0Fu);
    CHECK(mcl_link_frame_decode(buf, written, &rx, &consumed) == MCL_LINK_ERR_RANGE,
          "unknown frame class is a range error, not truncation");

    buf[0] = (uint8_t)((MCL_LINK_FRAME_MAJOR << 4u) | MCL_LINK_CLASS_DATA);
    buf[1] |= 0x80u;
    CHECK(mcl_link_frame_decode(buf, written, &rx, &consumed) == MCL_LINK_ERR_RANGE,
          "reserved flag bit is a range error, not truncation");

    buf[1] = 0u;
    /* Major 2, not 1: major 1 is cut and accepted now. */
    buf[0] = (uint8_t)((2u << 4u) | MCL_LINK_CLASS_DATA);
    CHECK(mcl_link_frame_decode(buf, written, &rx, &consumed)
              == MCL_LINK_ERR_INCOMPATIBLE_VERSION,
          "an unassigned major is reported as a version failure");
}

static void test_integrity_detects_corruption(void)
{
    mcl_link_frame_t tx, rx;
    uint8_t buf[128];
    size_t written = 0u, consumed = 0u, i;

    printf("[TEST] integrity field detects single-bit corruption\n");

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_DATA;
    tx.flags = MCL_LINK_FLAG_FRAME_CHECK;
    tx.source_ref = 0xA5A5A5A5u;
    tx.payload = k_presence;
    tx.payload_len = (uint16_t)sizeof(k_presence);
    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) == MCL_LINK_OK,
          "encode with integrity");

    for (i = 0u; i < written; ++i) {
        uint8_t corrupt[128];
        memcpy(corrupt, buf, written);
        corrupt[i] ^= 0x01u;
        /*
         * Corrupting the class or version bytes is caught earlier than the
         * CRC, which is correct; what must never happen is a clean decode.
         */
        CHECK(mcl_link_frame_decode(corrupt, written, &rx, &consumed) != MCL_LINK_OK,
              "corrupted frame never decodes cleanly");
    }
}

static void test_stream_of_frames(void)
{
    mcl_link_frame_t tx, rx;
    uint8_t buf[512];
    size_t pos = 0u, written = 0u, consumed = 0u;
    unsigned i, decoded = 0u;

    printf("[TEST] consumed allows back-to-back frames in a stream\n");

    for (i = 0u; i < 4u; ++i) {
        memset(&tx, 0, sizeof(tx));
        tx.frame_class = MCL_LINK_CLASS_DATA;
        tx.flags = MCL_LINK_FLAG_SEQUENCE;
        tx.source_ref = 0x10u + i;
        tx.sequence = (uint16_t)i;
        tx.payload = k_presence;
        tx.payload_len = (uint16_t)sizeof(k_presence);
        CHECK(mcl_link_frame_encode(&tx, buf + pos, sizeof(buf) - pos, &written)
                  == MCL_LINK_OK, "encode stream frame");
        pos += written;
    }

    written = 0u;
    while (written < pos) {
        if (mcl_link_frame_decode(buf + written, pos - written, &rx, &consumed)
                != MCL_LINK_OK) {
            break;
        }
        CHECK(rx.sequence == (uint16_t)decoded, "stream order preserved");
        written += consumed;
        ++decoded;
    }
    CHECK(decoded == 4u, "all four frames decoded from the stream");
    CHECK(written == pos, "stream fully consumed");
}

static void test_argument_validation(void)
{
    mcl_link_frame_t tx, rx;
    uint8_t buf[64];
    size_t written = 0u, consumed = 0u;

    printf("[TEST] argument validation\n");

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_DATA;
    tx.source_ref = 1u;
    tx.payload = k_presence;
    tx.payload_len = (uint16_t)sizeof(k_presence);

    CHECK(mcl_link_frame_encode(NULL, buf, sizeof(buf), &written)
              == MCL_LINK_ERR_INVALID_ARGUMENT, "null frame rejected");
    CHECK(mcl_link_frame_encode(&tx, NULL, sizeof(buf), &written)
              == MCL_LINK_ERR_INVALID_ARGUMENT, "null output rejected");
    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), NULL)
              == MCL_LINK_ERR_INVALID_ARGUMENT, "null written rejected");
    CHECK(mcl_link_frame_encode(&tx, buf, 4u, &written) == MCL_LINK_ERR_RANGE,
          "insufficient capacity rejected");
    CHECK(mcl_link_frame_decode(NULL, 16u, &rx, &consumed)
              == MCL_LINK_ERR_INVALID_ARGUMENT, "null input rejected");
    CHECK(mcl_link_frame_decode(buf, 16u, NULL, &consumed)
              == MCL_LINK_ERR_INVALID_ARGUMENT, "null frame out rejected");
    CHECK(mcl_link_crc32(NULL, 8u) == 0u, "crc of null is zero, not a crash");

    /* A declared payload with a null pointer must not encode. */
    tx.payload = NULL;
    tx.payload_len = 4u;
    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) == MCL_LINK_ERR_RANGE,
          "null payload with nonzero length rejected");
}

static void test_oversize_payload(void)
{
    mcl_link_frame_t tx;
    uint8_t buf[64];
    size_t written = 0u;

    printf("[TEST] oversize payload is rejected\n");

    memset(&tx, 0, sizeof(tx));
    tx.frame_class = MCL_LINK_CLASS_DATA;
    tx.source_ref = 1u;
    tx.payload = k_presence;
    tx.payload_len = (uint16_t)(MCL_LINK_FRAME_MAX_PAYLOAD + 1u);

    CHECK(mcl_link_frame_encoded_size(&tx) == 0u, "oversize has no encoded size");
    CHECK(mcl_link_frame_encode(&tx, buf, sizeof(buf), &written) == MCL_LINK_ERR_RANGE,
          "oversize payload not encodable");
}

int main(void)
{
    printf("MCL Link frame v0 tests\n");
    printf("=======================\n");

    test_minimal_round_trip();
    test_all_optionals_round_trip();
    test_every_class_round_trips();
    test_adapt_is_reserved();
    test_empty_payload();
    test_reject_unknown_version();
    test_reject_unknown_class();
    test_reject_reserved_flags();
    test_reject_truncation_at_every_length();
    test_malformed_is_not_truncation();
    test_integrity_detects_corruption();
    test_stream_of_frames();
    test_argument_validation();
    test_oversize_payload();

    printf("\n%d checks, %d failed\n", tests_run, tests_failed);
    return (tests_failed == 0) ? 0 : 1;
}
