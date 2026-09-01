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
    MCL_LINK_ERR_INVALID_STATE = 6
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

#ifdef __cplusplus
}
#endif

#endif /* MCL_LINK_H */
