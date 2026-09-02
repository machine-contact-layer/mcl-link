#ifndef MCL_LINK_H
#define MCL_LINK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MCL_LINK_RULESET_DIGEST_MAX_SIZE 32u

/*
 * Returns a 16-bit mask with bit N set if wire_major is in 0..15.
 * Returns 0 if wire_major > 15. Single-evaluation, no narrowing.
 */
static inline uint16_t mcl_link_wire_major_mask(uint32_t wire_major)
{
    if (wire_major > 15u) {
        return 0u;
    }
    return (uint16_t)(1u << wire_major);
}

typedef int32_t mcl_link_status_t;
enum {
    MCL_LINK_OK = 0,
    MCL_LINK_ERR_INVALID_ARGUMENT = 1,
    MCL_LINK_ERR_RANGE = 2,
    MCL_LINK_ERR_INCOMPATIBLE_VERSION = 3,
    MCL_LINK_ERR_CONTEXT_MISMATCH = 4,
    MCL_LINK_ERR_NO_ACTIVE_CONTEXT = 5,
    MCL_LINK_ERR_INVALID_STATE = 6,
    MCL_LINK_ERR_INTEGRITY = 7
};

/* Contact / Link Lifecycle States */
typedef uint8_t mcl_link_state_t;
enum {
    MCL_LINK_STATE_IDLE = 0u,
    MCL_LINK_STATE_DISCOVERED = 1u,
    MCL_LINK_STATE_CAPABILITIES = 2u,
    MCL_LINK_STATE_NEGOTIATING = 3u,
    MCL_LINK_STATE_ESTABLISHED = 4u,
    MCL_LINK_STATE_ADAPTING = 5u,
    MCL_LINK_STATE_HANDOFF = 6u,
    MCL_LINK_STATE_FALLBACK = 7u,
    MCL_LINK_STATE_CLOSED = 8u
};

/*
 * Logical Context Key definition.
 * NOTE: The C storage widths below represent local implementation storage capacity only.
 * THE C STORAGE WIDTHS DO NOT DEFINE THE FUTURE LINK WIRE ENCODING.
 */
typedef struct {
    uint8_t wire_major;
    uint32_t context_id;
    uint32_t generation;
    uint8_t ruleset_digest[MCL_LINK_RULESET_DIGEST_MAX_SIZE];
    uint8_t ruleset_digest_size;
} mcl_link_context_key_t;

/* Link context & session state */
typedef struct {
    mcl_link_state_t state;
    uint8_t context_valid;
    mcl_link_context_key_t active_context;
    uint16_t supported_wire_majors_mask;
} mcl_link_t;

/* Initialize link state with supported wire majors bitmask (bit N = wire major N supported) */
mcl_link_status_t mcl_link_init(
    mcl_link_t *link,
    uint16_t supported_wire_majors_mask);

/* Reset link state and invalidate installed context */
mcl_link_status_t mcl_link_reset(mcl_link_t *link);

/*
 * Install accepted context state into the local Link session after external negotiation
 * has verified that the offer/accept exchange completed successfully.
 */
mcl_link_status_t mcl_link_install_context(
    mcl_link_t *link,
    const mcl_link_context_key_t *key);

/* Authorize decode for an incoming context key against the active installed context */
mcl_link_status_t mcl_link_authorize_context(
    const mcl_link_t *link,
    const mcl_link_context_key_t *key);

/* State transition with validation according to the Link research lifecycle */
mcl_link_status_t mcl_link_transition(
    mcl_link_t *link,
    mcl_link_state_t next_state);

/* Query whether an active installed context exists */
mcl_link_status_t mcl_link_has_active_context(
    const mcl_link_t *link,
    uint8_t *has_context);

/* Compare two logical context keys for exact equality */
uint8_t mcl_link_context_key_equals(
    const mcl_link_context_key_t *a,
    const mcl_link_context_key_t *b);

/* ============================================================
 * Link frame v0 (research draft)
 *
 * spec/link-v0.md section 3 defines the logical LinkFrame and leaves the bit
 * layout to transport-profile integration. This is that layout, and it is the
 * carriage unit every MCL transport binding maps onto its own medium.
 *
 * It was deliberately not defined until a frame had actually survived a
 * physical channel, so that the mandatory fields are the ones contact really
 * needs rather than the ones that seemed likely in advance.
 *
 * Canonical byte layout, network byte order:
 *
 *   u8   link_major (high nibble) | frame_class (low nibble)
 *   u8   flags
 *   u32  source_ref                         always present
 *   u32  destination_ref                    if MCL_LINK_FLAG_DESTINATION
 *   u32  session_ref                        if MCL_LINK_FLAG_SESSION
 *   u16  sequence                           if MCL_LINK_FLAG_SEQUENCE
 *   u16  freshness_ms                       if MCL_LINK_FLAG_FRESHNESS
 *   u16  payload_len                        always present
 *   u8   payload[payload_len]
 *   u32  integrity (CRC-32/IEEE)            if MCL_LINK_FLAG_INTEGRITY
 *
 * Minimum frame is 8 bytes plus payload (class/version, flags, source_ref,
 * payload_len). Decoding is strict: an unknown
 * link_major, an unknown frame class, any reserved flag bit set, or a
 * truncated buffer is rejected rather than interpreted. A frame carrying an
 * integrity field whose CRC does not verify is rejected; a frame without one
 * is not thereby trusted, only unverified.
 *
 * Reception of a frame is not identity, authority, or trust. source_ref and
 * session_ref are contact references for correlation only, never proof.
 * ============================================================ */

#define MCL_LINK_FRAME_MAJOR        0u
#define MCL_LINK_FRAME_MIN_SIZE     8u
#define MCL_LINK_FRAME_MAX_PAYLOAD  1024u

/* Frame classes, spec/link-v0.md section 4. */
typedef uint8_t mcl_link_frame_class_t;
enum {
    MCL_LINK_CLASS_CONTACT     = 0u,
    MCL_LINK_CLASS_CAPABILITY  = 1u,
    MCL_LINK_CLASS_NEGOTIATION = 2u,
    MCL_LINK_CLASS_DATA        = 3u,
    MCL_LINK_CLASS_ACK         = 4u,
    MCL_LINK_CLASS_NACK        = 5u,
    MCL_LINK_CLASS_KEEPALIVE   = 6u,
    MCL_LINK_CLASS_ADAPT       = 7u,
    MCL_LINK_CLASS_HANDOFF     = 8u,
    MCL_LINK_CLASS_CLOSE       = 9u,
    MCL_LINK_CLASS_COUNT       = 10u
};

/* Optional-field flags. Bits 5..7 are reserved and MUST be zero. */
#define MCL_LINK_FLAG_DESTINATION 0x01u
#define MCL_LINK_FLAG_SESSION     0x02u
#define MCL_LINK_FLAG_SEQUENCE    0x04u
#define MCL_LINK_FLAG_FRESHNESS   0x08u
#define MCL_LINK_FLAG_INTEGRITY   0x10u
#define MCL_LINK_FLAG_RESERVED    0xE0u

typedef struct {
    mcl_link_frame_class_t frame_class;
    uint8_t flags;
    uint32_t source_ref;
    uint32_t destination_ref;   /* meaningful only with MCL_LINK_FLAG_DESTINATION */
    uint32_t session_ref;       /* meaningful only with MCL_LINK_FLAG_SESSION */
    uint16_t sequence;          /* meaningful only with MCL_LINK_FLAG_SEQUENCE */
    uint16_t freshness_ms;      /* meaningful only with MCL_LINK_FLAG_FRESHNESS */
    const uint8_t *payload;     /* canonical Wire bytes; borrowed, never owned */
    uint16_t payload_len;
} mcl_link_frame_t;

/* Exact encoded size of a frame, or 0 if the frame is not encodable. */
size_t mcl_link_frame_encoded_size(const mcl_link_frame_t *frame);

/*
 * Encode a frame. The caller owns the output buffer; no allocation occurs.
 * Writes the integrity field when MCL_LINK_FLAG_INTEGRITY is set.
 */
mcl_link_status_t mcl_link_frame_encode(
    const mcl_link_frame_t *frame,
    uint8_t *out,
    size_t out_capacity,
    size_t *written);

/*
 * Decode a frame. On success, frame->payload borrows from `in`, so it stays
 * valid only as long as `in` does. `consumed` reports the exact frame length,
 * which lets a stream carriage decode successive frames without re-scanning.
 * Trailing bytes after a complete frame are not an error here; carriage
 * profiles that forbid them check `consumed` against their own boundary.
 */
mcl_link_status_t mcl_link_frame_decode(
    const uint8_t *in,
    size_t in_size,
    mcl_link_frame_t *frame,
    size_t *consumed);

/* CRC-32/IEEE over a byte range, as used by the integrity field. */
uint32_t mcl_link_crc32(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* MCL_LINK_H */

