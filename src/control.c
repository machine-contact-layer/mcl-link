#include "mcl/control.h"

/*
 * Destination pointers are volatile throughout.
 *
 * An optimising compiler rewrites a byte-by-byte copy into a call to memcpy,
 * which breaks the freestanding contract at link time on a target with no libc.
 * That has already happened once in this repository and was invisible until the
 * objects' undefined symbols were inspected. See
 * mcl-core/governance/IMPLEMENTATION_CONTRACT.md section 2.2.
 */
static void mcl_link_put_u8(volatile uint8_t *out, uint8_t value)
{
    out[0] = value;
}

static void mcl_link_put_u16(volatile uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)((value >> 8) & 0xFFu);
    out[1] = (uint8_t)(value & 0xFFu);
}

static uint16_t mcl_link_get_u16(const uint8_t *in)
{
    return (uint16_t)(((uint16_t)in[0] << 8) | (uint16_t)in[1]);
}

/*
 * Exact-length enforcement, shared so that a control cannot be added later that
 * checks only one side of it.
 *
 * Short and long are reported differently on purpose. A short buffer may become
 * a valid control once more bytes arrive, so a stream carriage waits; a buffer
 * longer than the control declares never becomes valid, so the carriage must
 * resynchronise instead. A decoder that reported both identically would force
 * the carriage either to stall on corruption or to discard recoverable reads.
 */
static mcl_link_status_t mcl_link_exact_length(size_t data_size, size_t expected)
{
    if (data_size < expected) {
        return MCL_LINK_ERR_TRUNCATED;
    }
    if (data_size > expected) {
        return MCL_LINK_ERR_RANGE;
    }
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_make_ack(
    mcl_link_ack_t *ack,
    uint16_t acked_sequence)
{
    if (ack == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    ack->control_version = MCL_LINK_CONTROL_VERSION;
    ack->reason = 0u;
    ack->acked_sequence = acked_sequence;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_make_nack(
    mcl_link_ack_t *nack,
    uint16_t acked_sequence,
    mcl_link_nack_reason_t reason)
{
    if (nack == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (reason == MCL_LINK_NACK_RESERVED ||
        reason >= MCL_LINK_NACK_REASON_COUNT) {
        /* A zeroed payload is not a valid NACK, and an unassigned reason is a
         * refusal whose meaning the peer cannot look up. */
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    nack->control_version = MCL_LINK_CONTROL_VERSION;
    nack->reason = reason;
    nack->acked_sequence = acked_sequence;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_make_close(
    mcl_link_close_t *close_control,
    mcl_link_close_reason_t reason)
{
    if (close_control == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (reason == MCL_LINK_CLOSE_RESERVED ||
        reason >= MCL_LINK_CLOSE_REASON_COUNT) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    close_control->control_version = MCL_LINK_CONTROL_VERSION;
    close_control->reason = reason;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_ack_encode(
    const mcl_link_ack_t *ack,
    uint8_t is_ack,
    uint8_t *out,
    size_t capacity,
    size_t *written)
{
    volatile uint8_t *dst;

    if (ack == NULL || out == NULL || written == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (ack->control_version != MCL_LINK_CONTROL_VERSION) {
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }
    if (is_ack != 0u) {
        if (ack->reason != 0u) {
            /*
             * An ACK carries no reason. Emitting one would put a value in a
             * field with no defined meaning, which is where an undocumented
             * sub-protocol between two vendors starts.
             */
            return MCL_LINK_ERR_INVALID_ARGUMENT;
        }
    } else {
        if (ack->reason == MCL_LINK_NACK_RESERVED ||
            ack->reason >= MCL_LINK_NACK_REASON_COUNT) {
            return MCL_LINK_ERR_INVALID_ARGUMENT;
        }
    }
    if (capacity < MCL_LINK_ACK_SIZE) {
        return MCL_LINK_ERR_RANGE;
    }

    dst = (volatile uint8_t *)out;
    mcl_link_put_u8(dst, ack->control_version);
    mcl_link_put_u8(dst + 1, ack->reason);
    mcl_link_put_u16(dst + 2, ack->acked_sequence);

    *written = MCL_LINK_ACK_SIZE;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_ack_decode(
    const uint8_t *data,
    size_t data_size,
    uint8_t is_ack,
    mcl_link_ack_t *ack)
{
    mcl_link_status_t status;
    uint8_t reason;

    if (data == NULL || ack == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    status = mcl_link_exact_length(data_size, MCL_LINK_ACK_SIZE);
    if (status != MCL_LINK_OK) {
        return status;
    }

    /*
     * The version is checked before anything else is interpreted. Reading the
     * later fields first would be reading them under a layout this build has
     * not agreed to.
     */
    if (data[0] != MCL_LINK_CONTROL_VERSION) {
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }

    reason = data[1];
    if (is_ack != 0u) {
        if (reason != 0u) {
            return MCL_LINK_ERR_RANGE;
        }
    } else {
        if (reason == MCL_LINK_NACK_RESERVED ||
            reason >= MCL_LINK_NACK_REASON_COUNT) {
            /* An unassigned refusal is refused rather than passed through with
             * a number the caller cannot look up. */
            return MCL_LINK_ERR_RANGE;
        }
    }

    ack->control_version = data[0];
    ack->reason = reason;
    ack->acked_sequence = mcl_link_get_u16(data + 2);
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_close_encode(
    const mcl_link_close_t *close_control,
    uint8_t *out,
    size_t capacity,
    size_t *written)
{
    volatile uint8_t *dst;

    if (close_control == NULL || out == NULL || written == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (close_control->control_version != MCL_LINK_CONTROL_VERSION) {
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }
    if (close_control->reason == MCL_LINK_CLOSE_RESERVED ||
        close_control->reason >= MCL_LINK_CLOSE_REASON_COUNT) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (capacity < MCL_LINK_CLOSE_SIZE) {
        return MCL_LINK_ERR_RANGE;
    }

    dst = (volatile uint8_t *)out;
    mcl_link_put_u8(dst, close_control->control_version);
    mcl_link_put_u8(dst + 1, close_control->reason);

    *written = MCL_LINK_CLOSE_SIZE;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_close_decode(
    const uint8_t *data,
    size_t data_size,
    mcl_link_close_t *close_control)
{
    mcl_link_status_t status;

    if (data == NULL || close_control == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    status = mcl_link_exact_length(data_size, MCL_LINK_CLOSE_SIZE);
    if (status != MCL_LINK_OK) {
        return status;
    }
    if (data[0] != MCL_LINK_CONTROL_VERSION) {
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }
    if (data[1] == MCL_LINK_CLOSE_RESERVED ||
        data[1] >= MCL_LINK_CLOSE_REASON_COUNT) {
        return MCL_LINK_ERR_RANGE;
    }

    close_control->control_version = data[0];
    close_control->reason = data[1];
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_keepalive_check(size_t payload_len)
{
    if (payload_len != MCL_LINK_KEEPALIVE_SIZE) {
        /* Refused, not ignored. See the note in control.h. */
        return MCL_LINK_ERR_RANGE;
    }
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_link_sequence_is_live(
    uint16_t highest_sent,
    uint16_t acked_sequence,
    uint8_t *live)
{
    uint16_t distance;

    if (live == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    /*
     * Modulo-65536 subtraction. Because both operands are uint16_t and the
     * result is taken back to uint16_t, a window that spans the wrap point
     * needs no special case: the distance from 0xFFFF to 0x0002 is 3 whichever
     * side of the wrap the two values fall.
     *
     * The promotion to int that C performs on the operands is exactly why the
     * cast back is explicit rather than implied.
     */
    distance = (uint16_t)((uint32_t)highest_sent - (uint32_t)acked_sequence);

    *live = (uint8_t)((distance < MCL_LINK_SEQUENCE_WINDOW_MAX) ? 1u : 0u);
    return MCL_LINK_OK;
}
