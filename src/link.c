#include "mcl/link.h"

/*
 * Byte fill and byte copy the compiler may not rewrite into memset or memcpy.
 *
 * An optimising compiler pattern-matches a plain indexed loop over a buffer
 * into the corresponding libc call. That reintroduces a libc dependency into
 * an object required to link on a freestanding target where no C library is
 * present, and the breakage appears only at link time on the real hardware.
 * A volatile destination defeats the rewrite. mcl-wire and the transport
 * bindings solve the same problem the same way.
 */
static void mcl_link_zero_bytes(uint8_t *dst, size_t count)
{
    volatile uint8_t *out = (volatile uint8_t *)dst;
    size_t i;

    for (i = 0u; i < count; ++i) {
        out[i] = 0u;
    }
}

static void mcl_link_copy_bytes(uint8_t *dst, const uint8_t *src, size_t count)
{
    volatile uint8_t *out = (volatile uint8_t *)dst;
    size_t i;

    for (i = 0u; i < count; ++i) {
        out[i] = src[i];
    }
}

static void mcl_link_zero_context(mcl_link_context_key_t *key)
{
    if (key == NULL) {
        return;
    }
    key->wire_major = 0u;
    key->context_id = 0u;
    key->generation = 0u;
    key->ruleset_digest_size = 0u;
    mcl_link_zero_bytes(key->ruleset_digest, MCL_LINK_RULESET_DIGEST_MAX_SIZE);
}

static void mcl_link_clear_context(mcl_link_t *link)
{
    if (link != NULL) {
        link->context_valid = 0u;
        mcl_link_zero_context(&link->active_context);
    }
}

mcl_link_status_t mcl_link_init(
    mcl_link_t *link,
    uint16_t supported_wire_majors_mask)
{
    if (link == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    link->state = MCL_LINK_STATE_IDLE;
    link->context_valid = 0u;
    link->supported_wire_majors_mask = supported_wire_majors_mask;
    mcl_link_zero_context(&link->active_context);

    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_reset(mcl_link_t *link)
{
    if (link == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    link->state = MCL_LINK_STATE_IDLE;
    mcl_link_clear_context(link);

    return MCL_LINK_OK;
}

uint8_t mcl_link_context_key_equals(
    const mcl_link_context_key_t *a,
    const mcl_link_context_key_t *b)
{
    size_t i;

    if (a == NULL || b == NULL) {
        return 0u;
    }

    /* Validate bounds on digest sizes for arbitrary caller structs */
    if (a->ruleset_digest_size == 0u ||
        a->ruleset_digest_size > MCL_LINK_RULESET_DIGEST_MAX_SIZE ||
        b->ruleset_digest_size == 0u ||
        b->ruleset_digest_size > MCL_LINK_RULESET_DIGEST_MAX_SIZE) {
        return 0u;
    }

    if (a->wire_major != b->wire_major ||
        a->context_id != b->context_id ||
        a->generation != b->generation ||
        a->ruleset_digest_size != b->ruleset_digest_size) {
        return 0u;
    }

    for (i = 0u; i < (size_t)a->ruleset_digest_size; ++i) {
        if (a->ruleset_digest[i] != b->ruleset_digest[i]) {
            return 0u;
        }
    }

    return 1u;
}

mcl_link_status_t mcl_link_install_context(
    mcl_link_t *link,
    const mcl_link_context_key_t *key)
{
    if (link == NULL || key == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    if (link->state == MCL_LINK_STATE_IDLE ||
        link->state == MCL_LINK_STATE_DISCOVERED ||
        link->state == MCL_LINK_STATE_CLOSED) {
        return MCL_LINK_ERR_INVALID_STATE;
    }

    if (key->wire_major > 15u) {
        return MCL_LINK_ERR_RANGE;
    }

    if (key->ruleset_digest_size == 0u ||
        key->ruleset_digest_size > MCL_LINK_RULESET_DIGEST_MAX_SIZE) {
        return MCL_LINK_ERR_RANGE;
    }

    if ((link->supported_wire_majors_mask & (uint16_t)(1u << key->wire_major)) == 0u) {
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }

    link->active_context.wire_major = key->wire_major;
    link->active_context.context_id = key->context_id;
    link->active_context.generation = key->generation;
    link->active_context.ruleset_digest_size = key->ruleset_digest_size;

    mcl_link_copy_bytes(link->active_context.ruleset_digest,
                        key->ruleset_digest,
                        (size_t)key->ruleset_digest_size);
    /* Never leave a previous digest's tail visible behind a shorter one. */
    mcl_link_zero_bytes(link->active_context.ruleset_digest
                            + key->ruleset_digest_size,
                        (size_t)(MCL_LINK_RULESET_DIGEST_MAX_SIZE
                                 - key->ruleset_digest_size));

    link->context_valid = 1u;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_authorize_context(
    const mcl_link_t *link,
    const mcl_link_context_key_t *key)
{
    if (link == NULL || key == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    if (link->context_valid == 0u) {
        return MCL_LINK_ERR_NO_ACTIVE_CONTEXT;
    }

    if (mcl_link_context_key_equals(&link->active_context, key) == 0u) {
        return MCL_LINK_ERR_CONTEXT_MISMATCH;
    }

    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_transition(
    mcl_link_t *link,
    mcl_link_state_t next_state)
{
    if (link == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    switch (link->state) {
    case MCL_LINK_STATE_IDLE:
        if (next_state == MCL_LINK_STATE_DISCOVERED ||
            next_state == MCL_LINK_STATE_CLOSED) {
            link->state = next_state;
            if (next_state == MCL_LINK_STATE_CLOSED) {
                mcl_link_clear_context(link);
            }
            return MCL_LINK_OK;
        }
        break;

    case MCL_LINK_STATE_DISCOVERED:
        if (next_state == MCL_LINK_STATE_CAPABILITIES ||
            next_state == MCL_LINK_STATE_IDLE ||
            next_state == MCL_LINK_STATE_CLOSED) {
            link->state = next_state;
            if (next_state == MCL_LINK_STATE_IDLE || next_state == MCL_LINK_STATE_CLOSED) {
                mcl_link_clear_context(link);
            }
            return MCL_LINK_OK;
        }
        break;

    case MCL_LINK_STATE_CAPABILITIES:
        if (next_state == MCL_LINK_STATE_NEGOTIATING ||
            next_state == MCL_LINK_STATE_IDLE ||
            next_state == MCL_LINK_STATE_CLOSED) {
            link->state = next_state;
            if (next_state == MCL_LINK_STATE_IDLE || next_state == MCL_LINK_STATE_CLOSED) {
                mcl_link_clear_context(link);
            }
            return MCL_LINK_OK;
        }
        break;

    case MCL_LINK_STATE_NEGOTIATING:
        if (next_state == MCL_LINK_STATE_ESTABLISHED ||
            next_state == MCL_LINK_STATE_IDLE ||
            next_state == MCL_LINK_STATE_CLOSED) {
            link->state = next_state;
            if (next_state == MCL_LINK_STATE_IDLE || next_state == MCL_LINK_STATE_CLOSED) {
                mcl_link_clear_context(link);
            }
            return MCL_LINK_OK;
        }
        break;

    case MCL_LINK_STATE_ESTABLISHED:
        if (next_state == MCL_LINK_STATE_ADAPTING ||
            next_state == MCL_LINK_STATE_HANDOFF ||
            next_state == MCL_LINK_STATE_FALLBACK ||
            next_state == MCL_LINK_STATE_IDLE ||
            next_state == MCL_LINK_STATE_CLOSED) {
            link->state = next_state;
            if (next_state == MCL_LINK_STATE_IDLE || next_state == MCL_LINK_STATE_CLOSED) {
                mcl_link_clear_context(link);
            }
            return MCL_LINK_OK;
        }
        break;

    case MCL_LINK_STATE_ADAPTING:
        if (next_state == MCL_LINK_STATE_ESTABLISHED ||
            next_state == MCL_LINK_STATE_HANDOFF ||
            next_state == MCL_LINK_STATE_FALLBACK ||
            next_state == MCL_LINK_STATE_IDLE ||
            next_state == MCL_LINK_STATE_CLOSED) {
            link->state = next_state;
            if (next_state == MCL_LINK_STATE_IDLE || next_state == MCL_LINK_STATE_CLOSED) {
                mcl_link_clear_context(link);
            }
            return MCL_LINK_OK;
        }
        break;

    case MCL_LINK_STATE_HANDOFF:
        if (next_state == MCL_LINK_STATE_ESTABLISHED ||
            next_state == MCL_LINK_STATE_IDLE ||
            next_state == MCL_LINK_STATE_CLOSED) {
            link->state = next_state;
            if (next_state == MCL_LINK_STATE_IDLE || next_state == MCL_LINK_STATE_CLOSED) {
                mcl_link_clear_context(link);
            }
            return MCL_LINK_OK;
        }
        break;

    case MCL_LINK_STATE_FALLBACK:
        if (next_state == MCL_LINK_STATE_ESTABLISHED ||
            next_state == MCL_LINK_STATE_DISCOVERED ||
            next_state == MCL_LINK_STATE_IDLE ||
            next_state == MCL_LINK_STATE_CLOSED) {
            link->state = next_state;
            if (next_state == MCL_LINK_STATE_DISCOVERED ||
                next_state == MCL_LINK_STATE_IDLE ||
                next_state == MCL_LINK_STATE_CLOSED) {
                mcl_link_clear_context(link);
            }
            return MCL_LINK_OK;
        }
        break;

    case MCL_LINK_STATE_CLOSED:
        if (next_state == MCL_LINK_STATE_IDLE) {
            link->state = next_state;
            mcl_link_clear_context(link);
            return MCL_LINK_OK;
        }
        break;

    default:
        break;
    }

    return MCL_LINK_ERR_INVALID_STATE;
}

mcl_link_status_t mcl_link_has_active_context(
    const mcl_link_t *link,
    uint8_t *has_context)
{
    if (link == NULL || has_context == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    *has_context = link->context_valid;
    return MCL_LINK_OK;
}

/* ============================================================
 * Link frame v0
 * ============================================================ */

uint32_t mcl_link_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    unsigned bit;

    if (data == NULL) {
        return 0u;
    }

    for (i = 0u; i < size; ++i) {
        crc ^= (uint32_t)data[i];
        for (bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
}

/* Length of the optional fields selected by `flags`. */
static size_t mcl_link_optional_size(uint8_t flags)
{
    size_t n = 0u;

    if ((flags & MCL_LINK_FLAG_DESTINATION) != 0u) n += 4u;
    if ((flags & MCL_LINK_FLAG_SESSION) != 0u)     n += 4u;
    if ((flags & MCL_LINK_FLAG_SEQUENCE) != 0u)    n += 2u;
    if ((flags & MCL_LINK_FLAG_FRESHNESS) != 0u)   n += 2u;
    if ((flags & MCL_LINK_FLAG_FRAME_CHECK) != 0u)   n += 4u;

    return n;
}

static uint8_t mcl_link_frame_encodable(const mcl_link_frame_t *frame)
{
    if (frame == NULL) {
        return 0u;
    }
    if (frame->frame_class >= MCL_LINK_CLASS_COUNT) {
        return 0u;
    }
    if (frame->frame_class == MCL_LINK_CLASS_ADAPT) {
        /* Reserved. Nothing may emit one; see the decoder for why. */
        return 0u;
    }
    if ((frame->flags & MCL_LINK_FLAG_RESERVED) != 0u) {
        return 0u;
    }
    if (frame->payload_len > MCL_LINK_FRAME_MAX_PAYLOAD) {
        return 0u;
    }
    if (frame->payload_len != 0u && frame->payload == NULL) {
        return 0u;
    }
    return 1u;
}

size_t mcl_link_frame_encoded_size(const mcl_link_frame_t *frame)
{
    if (mcl_link_frame_encodable(frame) == 0u) {
        return 0u;
    }
    return (size_t)MCL_LINK_FRAME_MIN_SIZE
         + mcl_link_optional_size(frame->flags)
         + (size_t)frame->payload_len;
}

static void mcl_link_put_u16(uint8_t *out, uint16_t v)
{
    out[0] = (uint8_t)(v >> 8u);
    out[1] = (uint8_t)(v & 0xFFu);
}

static void mcl_link_put_u32(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)(v >> 24u);
    out[1] = (uint8_t)((v >> 16u) & 0xFFu);
    out[2] = (uint8_t)((v >> 8u) & 0xFFu);
    out[3] = (uint8_t)(v & 0xFFu);
}

static uint16_t mcl_link_get_u16(const uint8_t *in)
{
    return (uint16_t)(((uint16_t)in[0] << 8u) | (uint16_t)in[1]);
}

static uint32_t mcl_link_get_u32(const uint8_t *in)
{
    return ((uint32_t)in[0] << 24u)
         | ((uint32_t)in[1] << 16u)
         | ((uint32_t)in[2] << 8u)
         | (uint32_t)in[3];
}

mcl_link_status_t mcl_link_frame_encode(
    const mcl_link_frame_t *frame,
    uint8_t *out,
    size_t out_capacity,
    size_t *written)
{
    /*
     * The experimental major, for source compatibility. A caller written before
     * major 1 was cut compiles unchanged and emits exactly the bytes it emitted
     * before. A caller that wants the Stable major asks for it by name.
     */
    return mcl_link_frame_encode_at_major(MCL_LINK_FRAME_MAJOR, frame, out,
                                          out_capacity, written);
}

mcl_link_status_t mcl_link_frame_encode_at_major(
    uint8_t major,
    const mcl_link_frame_t *frame,
    uint8_t *out,
    size_t out_capacity,
    size_t *written)
{
    size_t need;
    size_t pos = 0u;

    if (frame == NULL || out == NULL || written == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (major != MCL_LINK_FRAME_MAJOR && major != MCL_LINK_STABLE_MAJOR) {
        /* Unassigned. Refused rather than emitted and left for a peer to
         * puzzle over. */
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }
    if (mcl_link_frame_encodable(frame) == 0u) {
        return MCL_LINK_ERR_RANGE;
    }

    need = mcl_link_frame_encoded_size(frame);
    if (out_capacity < need) {
        return MCL_LINK_ERR_RANGE;
    }

    out[pos++] = (uint8_t)(((uint32_t)major << 4u)
                           | (uint32_t)frame->frame_class);
    out[pos++] = frame->flags;

    mcl_link_put_u32(out + pos, frame->source_ref);
    pos += 4u;

    if ((frame->flags & MCL_LINK_FLAG_DESTINATION) != 0u) {
        mcl_link_put_u32(out + pos, frame->destination_ref);
        pos += 4u;
    }
    if ((frame->flags & MCL_LINK_FLAG_SESSION) != 0u) {
        mcl_link_put_u32(out + pos, frame->session_ref);
        pos += 4u;
    }
    if ((frame->flags & MCL_LINK_FLAG_SEQUENCE) != 0u) {
        mcl_link_put_u16(out + pos, frame->sequence);
        pos += 2u;
    }
    if ((frame->flags & MCL_LINK_FLAG_FRESHNESS) != 0u) {
        mcl_link_put_u16(out + pos, frame->freshness_ms);
        pos += 2u;
    }

    mcl_link_put_u16(out + pos, frame->payload_len);
    pos += 2u;

    mcl_link_copy_bytes(out + pos, frame->payload, (size_t)frame->payload_len);
    pos += (size_t)frame->payload_len;

    if ((frame->flags & MCL_LINK_FLAG_FRAME_CHECK) != 0u) {
        mcl_link_put_u32(out + pos, mcl_link_crc32(out, pos));
        pos += 4u;
    }

    *written = pos;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_frame_decode(
    const uint8_t *in,
    size_t in_size,
    mcl_link_frame_t *frame,
    size_t *consumed)
{
    size_t pos = 0u;
    size_t optional;
    uint8_t major, flags;
    uint16_t payload_len;

    if (in == NULL || frame == NULL || consumed == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (in_size < (size_t)MCL_LINK_FRAME_MIN_SIZE) {
        return MCL_LINK_ERR_TRUNCATED;
    }

    major = (uint8_t)((in[0] >> 4u) & 0x0Fu);
    if (major != (uint8_t)MCL_LINK_FRAME_MAJOR &&
        major != (uint8_t)MCL_LINK_STABLE_MAJOR) {
        /*
         * Major 1 is CUT. The frame layout is byte-identical to major 0 -- the
         * major bump exists because the project's own version policy reserves
         * major 0 for pre-standard work, so publishing a Stable Link on it
         * would contradict the policy, not because any byte moved.
         *
         * What differs at major 1 is the CLASS DISPOSITIONS, which
         * link-class-disposition-v1.md freezes: nine Stable, ADAPT reserved.
         * Those are enforced below for both majors, because they were already
         * true at major 0.
         */
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }
    frame->link_major = major;

    frame->frame_class = (mcl_link_frame_class_t)(in[0] & 0x0Fu);
    if (frame->frame_class >= MCL_LINK_CLASS_COUNT) {
        /* Unknown meaning is rejected, never guessed. */
        return MCL_LINK_ERR_RANGE;
    }
    if (frame->frame_class == MCL_LINK_CLASS_ADAPT) {
        /*
         * ADAPT is RESERVED at Link major 0 and is refused.
         *
         * It was assigned for transport adaptation -- changing rate, profile or
         * parameters without a full migration -- and then nothing was ever
         * built on it: no payload was designed, no mechanism uses it, and the
         * cases considered so far are served either by a transport's own
         * adaptation, below MCL entirely, or by a migration, which is
         * specified.
         *
         * Accepting a class whose payload nobody has specified is an
         * interoperability failure waiting for its first independent
         * implementation: two vendors would each invent a payload and both
         * would decode successfully. Reserving costs nothing and can be undone;
         * specifying it speculatively cannot. See
         * spec/link-frame-classes-v0.1.md section 5.
         *
         * This is a deliberate BEHAVIOUR CHANGE: a frame previously accepted
         * with an undefined payload is now refused. Nothing sends one.
         */
        return MCL_LINK_ERR_RANGE;
    }

    flags = in[1];
    if ((flags & MCL_LINK_FLAG_RESERVED) != 0u) {
        /* Reserved bits must be zero for the encoding to be canonical. */
        return MCL_LINK_ERR_RANGE;
    }
    frame->flags = flags;
    pos = 2u;

    optional = mcl_link_optional_size(flags);
    if (in_size < (size_t)MCL_LINK_FRAME_MIN_SIZE + optional) {
        return MCL_LINK_ERR_TRUNCATED;
    }

    frame->source_ref = mcl_link_get_u32(in + pos);
    pos += 4u;

    frame->destination_ref = 0u;
    frame->session_ref = 0u;
    frame->sequence = 0u;
    frame->freshness_ms = 0u;

    if ((flags & MCL_LINK_FLAG_DESTINATION) != 0u) {
        frame->destination_ref = mcl_link_get_u32(in + pos);
        pos += 4u;
    }
    if ((flags & MCL_LINK_FLAG_SESSION) != 0u) {
        frame->session_ref = mcl_link_get_u32(in + pos);
        pos += 4u;
    }
    if ((flags & MCL_LINK_FLAG_SEQUENCE) != 0u) {
        frame->sequence = mcl_link_get_u16(in + pos);
        pos += 2u;
    }
    if ((flags & MCL_LINK_FLAG_FRESHNESS) != 0u) {
        frame->freshness_ms = mcl_link_get_u16(in + pos);
        pos += 2u;
    }

    payload_len = mcl_link_get_u16(in + pos);
    pos += 2u;

    if (payload_len > MCL_LINK_FRAME_MAX_PAYLOAD) {
        return MCL_LINK_ERR_RANGE;
    }
    if (in_size - pos < (size_t)payload_len) {
        return MCL_LINK_ERR_TRUNCATED;
    }

    frame->payload_len = payload_len;
    frame->payload = (payload_len != 0u) ? (in + pos) : NULL;
    pos += (size_t)payload_len;

    if ((flags & MCL_LINK_FLAG_FRAME_CHECK) != 0u) {
        uint32_t received;
        if (in_size - pos < 4u) {
            return MCL_LINK_ERR_TRUNCATED;
        }
        received = mcl_link_get_u32(in + pos);
        if (received != mcl_link_crc32(in, pos)) {
            return MCL_LINK_ERR_FRAME_CHECK;
        }
        pos += 4u;
    }

    *consumed = pos;
    return MCL_LINK_OK;
}
