#include "mcl/negotiation.h"

/*
 * Destination pointers are volatile for the reason given at the top of
 * control.c: an optimising compiler rewrites a byte-by-byte copy into memcpy,
 * which breaks the freestanding contract at link time on a target with no libc.
 */
static void mcl_neg_put_u8(volatile uint8_t *out, uint8_t value)
{
    out[0] = value;
}

static void mcl_neg_put_u16(volatile uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)((value >> 8) & 0xFFu);
    out[1] = (uint8_t)(value & 0xFFu);
}

static uint16_t mcl_neg_get_u16(const uint8_t *in)
{
    return (uint16_t)(((uint16_t)in[0] << 8) | (uint16_t)in[1]);
}

/*
 * Short and long are reported differently, as everywhere else in Link: a short
 * buffer may become valid once more bytes arrive, a long one never will.
 */
static mcl_link_status_t mcl_neg_exact_length(size_t data_size, size_t expected)
{
    if (data_size < expected) {
        return MCL_LINK_ERR_TRUNCATED;
    }
    if (data_size > expected) {
        return MCL_LINK_ERR_RANGE;
    }
    return MCL_LINK_OK;
}

/*
 * Index of the highest set bit, or -1 for zero.
 *
 * A plain loop rather than a compiler builtin: builtins are not available on
 * every target this code cross-compiles to, and a 16-iteration loop is not
 * worth a portability exception.
 */
static int mcl_neg_highest_bit(uint16_t mask)
{
    int index;

    for (index = 15; index >= 0; --index) {
        if ((mask & (uint16_t)(1u << (unsigned)index)) != 0u) {
            return index;
        }
    }
    return -1;
}

mcl_link_status_t mcl_link_make_capability(
    mcl_link_capability_t *capability,
    uint16_t wire_majors,
    uint16_t link_majors,
    uint16_t max_frame,
    uint16_t features)
{
    if (capability == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    /*
     * A node supporting no major cannot be negotiated with. Refusing here
     * rather than at the peer saves a round trip on both sides and keeps an
     * empty advertisement from looking like a position that was taken.
     */
    if (wire_majors == 0u || link_majors == 0u) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (max_frame < MCL_LINK_NEGOTIATED_FRAME_FLOOR) {
        return MCL_LINK_ERR_RANGE;
    }

    capability->control_version = MCL_LINK_CONTROL_VERSION;
    capability->wire_majors = wire_majors;
    capability->link_majors = link_majors;
    capability->max_frame = max_frame;
    capability->features = features;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_capability_encode(
    const mcl_link_capability_t *capability,
    uint8_t *out,
    size_t out_size,
    size_t *written)
{
    volatile uint8_t *dst;

    if (capability == NULL || out == NULL || written == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (capability->control_version != MCL_LINK_CONTROL_VERSION) {
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }
    if (capability->wire_majors == 0u || capability->link_majors == 0u) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (capability->max_frame < MCL_LINK_NEGOTIATED_FRAME_FLOOR) {
        return MCL_LINK_ERR_RANGE;
    }
    if (out_size < MCL_LINK_CAPABILITY_SIZE) {
        return MCL_LINK_ERR_RANGE;
    }

    dst = (volatile uint8_t *)out;
    mcl_neg_put_u8(dst, capability->control_version);
    mcl_neg_put_u16(dst + 1, capability->wire_majors);
    mcl_neg_put_u16(dst + 3, capability->link_majors);
    mcl_neg_put_u16(dst + 5, capability->max_frame);
    mcl_neg_put_u16(dst + 7, capability->features);

    *written = MCL_LINK_CAPABILITY_SIZE;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_capability_decode(
    const uint8_t *in,
    size_t in_size,
    mcl_link_capability_t *capability)
{
    mcl_link_status_t status;
    uint16_t wire_majors;
    uint16_t link_majors;
    uint16_t max_frame;

    if (in == NULL || capability == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    status = mcl_neg_exact_length(in_size, MCL_LINK_CAPABILITY_SIZE);
    if (status != MCL_LINK_OK) {
        return status;
    }

    /* The version is checked before anything else is interpreted: reading the
     * later fields first would read them under a layout not agreed to. */
    if (in[0] != MCL_LINK_CONTROL_VERSION) {
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }

    wire_majors = mcl_neg_get_u16(in + 1);
    link_majors = mcl_neg_get_u16(in + 3);
    max_frame = mcl_neg_get_u16(in + 5);

    if (wire_majors == 0u || link_majors == 0u) {
        return MCL_LINK_ERR_RANGE;
    }
    if (max_frame < MCL_LINK_NEGOTIATED_FRAME_FLOOR) {
        return MCL_LINK_ERR_RANGE;
    }

    capability->control_version = in[0];
    capability->wire_majors = wire_majors;
    capability->link_majors = link_majors;
    capability->max_frame = max_frame;
    capability->features = mcl_neg_get_u16(in + 7);
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_negotiation_encode(
    const mcl_link_negotiation_t *negotiation,
    uint8_t *out,
    size_t out_size,
    size_t *written)
{
    volatile uint8_t *dst;

    if (negotiation == NULL || out == NULL || written == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (negotiation->control_version != MCL_LINK_CONTROL_VERSION) {
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }
    /* Majors are nibbles on the wire. A value that cannot be carried in the
     * frame header must not be presented as a negotiated outcome. */
    if (negotiation->wire_major > 15u || negotiation->link_major > 15u) {
        return MCL_LINK_ERR_RANGE;
    }
    if (negotiation->max_frame < MCL_LINK_NEGOTIATED_FRAME_FLOOR) {
        return MCL_LINK_ERR_RANGE;
    }
    if (out_size < MCL_LINK_NEGOTIATION_SIZE) {
        return MCL_LINK_ERR_RANGE;
    }

    dst = (volatile uint8_t *)out;
    mcl_neg_put_u8(dst, negotiation->control_version);
    mcl_neg_put_u8(dst + 1, negotiation->wire_major);
    mcl_neg_put_u8(dst + 2, negotiation->link_major);
    mcl_neg_put_u16(dst + 3, negotiation->max_frame);
    mcl_neg_put_u16(dst + 5, negotiation->features);

    *written = MCL_LINK_NEGOTIATION_SIZE;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_negotiation_decode(
    const uint8_t *in,
    size_t in_size,
    mcl_link_negotiation_t *negotiation)
{
    mcl_link_status_t status;
    uint8_t wire_major;
    uint8_t link_major;
    uint16_t max_frame;

    if (in == NULL || negotiation == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    status = mcl_neg_exact_length(in_size, MCL_LINK_NEGOTIATION_SIZE);
    if (status != MCL_LINK_OK) {
        return status;
    }
    if (in[0] != MCL_LINK_CONTROL_VERSION) {
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }

    wire_major = in[1];
    link_major = in[2];
    max_frame = mcl_neg_get_u16(in + 3);

    if (wire_major > 15u || link_major > 15u) {
        return MCL_LINK_ERR_RANGE;
    }
    if (max_frame < MCL_LINK_NEGOTIATED_FRAME_FLOOR) {
        return MCL_LINK_ERR_RANGE;
    }

    negotiation->control_version = in[0];
    negotiation->wire_major = wire_major;
    negotiation->link_major = link_major;
    negotiation->max_frame = max_frame;
    negotiation->features = mcl_neg_get_u16(in + 5);
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_negotiation_select(
    const mcl_link_capability_t *local,
    const mcl_link_capability_t *peer,
    mcl_link_negotiation_t *selection)
{
    uint16_t common_wire;
    uint16_t common_link;
    uint16_t frame;
    int wire_major;
    int link_major;

    if (local == NULL || peer == NULL || selection == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    /*
     * Every operation below is symmetric in local and peer -- & is commutative,
     * min is commutative, and the highest set bit of (A & B) does not depend on
     * operand order. That is what makes two simultaneous CAPABILITY frames
     * resolve themselves without a tiebreaker. Do not introduce an asymmetric
     * rule here without also introducing one.
     */
    common_wire = (uint16_t)(local->wire_majors & peer->wire_majors);
    common_link = (uint16_t)(local->link_majors & peer->link_majors);

    wire_major = mcl_neg_highest_bit(common_wire);
    link_major = mcl_neg_highest_bit(common_link);
    if (wire_major < 0 || link_major < 0) {
        /* Two honest implementations with nothing in common. Reported, not
         * papered over with a default that neither peer agreed to. */
        return MCL_LINK_ERR_RANGE;
    }

    frame = (local->max_frame < peer->max_frame) ? local->max_frame
                                                 : peer->max_frame;
    if (frame < MCL_LINK_NEGOTIATED_FRAME_FLOOR) {
        return MCL_LINK_ERR_RANGE;
    }

    selection->control_version = MCL_LINK_CONTROL_VERSION;
    selection->wire_major = (uint8_t)wire_major;
    selection->link_major = (uint8_t)link_major;
    selection->max_frame = frame;
    /*
     * An unknown feature bit is a bit this node did not set, so the AND clears
     * it. Unknown features fail closed by construction: there is no rule to get
     * wrong, and none to test an implementation against.
     */
    selection->features = (uint16_t)(local->features & peer->features);
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_negotiation_check(
    const mcl_link_negotiation_t *proposal,
    const mcl_link_capability_t *local,
    const mcl_link_capability_t *peer)
{
    if (proposal == NULL || local == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (proposal->control_version != MCL_LINK_CONTROL_VERSION) {
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }
    if (proposal->wire_major > 15u || proposal->link_major > 15u) {
        return MCL_LINK_ERR_RANGE;
    }

    if (peer != NULL) {
        /* Strong branch: both inputs are known, so the answer is known. */
        mcl_link_negotiation_t expected;
        mcl_link_status_t status;

        status = mcl_link_negotiation_select(local, peer, &expected);
        if (status != MCL_LINK_OK) {
            return status;
        }
        if (proposal->wire_major != expected.wire_major ||
            proposal->link_major != expected.link_major ||
            proposal->max_frame != expected.max_frame ||
            proposal->features != expected.features) {
            return MCL_LINK_ERR_RANGE;
        }
        return MCL_LINK_OK;
    }

    /*
     * Weak branch: without the peer's advertisement this can verify only that
     * the proposal is legal HERE, not that the peer chose the highest common
     * major. A legal-but-lower selection is suboptimal, not incorrect, and v1
     * has no mechanism that could distinguish the two. Nothing here is
     * authenticated; this is not downgrade protection.
     */
    if ((local->wire_majors &
         (uint16_t)(1u << proposal->wire_major)) == 0u) {
        return MCL_LINK_ERR_RANGE;
    }
    if ((local->link_majors &
         (uint16_t)(1u << proposal->link_major)) == 0u) {
        return MCL_LINK_ERR_RANGE;
    }
    if (proposal->max_frame > local->max_frame ||
        proposal->max_frame < MCL_LINK_NEGOTIATED_FRAME_FLOOR) {
        return MCL_LINK_ERR_RANGE;
    }
    if ((proposal->features & (uint16_t)~local->features) != 0u) {
        /* A feature this node never offered cannot be in the outcome. */
        return MCL_LINK_ERR_RANGE;
    }
    return MCL_LINK_OK;
}
