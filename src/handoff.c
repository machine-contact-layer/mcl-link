#include "mcl/handoff.h"

/*
 * Written through a volatile destination so the compiler cannot rewrite the
 * byte loop into a memcpy call, which would break the freestanding contract at
 * link time on a target with no libc. This has already happened once in this
 * repository. See mcl-core/governance/IMPLEMENTATION_CONTRACT.md section 2.2.
 */
static void mcl_handoff_put_u32(uint8_t *dst, uint32_t value)
{
    volatile uint8_t *out = (volatile uint8_t *)dst;
    out[0] = (uint8_t)((value >> 24) & 0xFFu);
    out[1] = (uint8_t)((value >> 16) & 0xFFu);
    out[2] = (uint8_t)((value >> 8) & 0xFFu);
    out[3] = (uint8_t)(value & 0xFFu);
}

static uint32_t mcl_handoff_get_u32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static void mcl_handoff_copy_challenge(uint8_t *dst, const uint8_t *src)
{
    volatile uint8_t *out = (volatile uint8_t *)dst;
    unsigned i;
    for (i = 0u; i < MCL_CONTACT_CHALLENGE_SIZE; ++i) {
        out[i] = src[i];
    }
}

static void mcl_handoff_clear_challenge(uint8_t *dst)
{
    volatile uint8_t *out = (volatile uint8_t *)dst;
    unsigned i;
    for (i = 0u; i < MCL_CONTACT_CHALLENGE_SIZE; ++i) {
        out[i] = 0u;
    }
}

/* An operation this build implements. Experimental Use values are not
 * implemented here, and are rejected exactly like any unassigned value. */
static int mcl_handoff_op_assigned(mcl_handoff_op_t operation)
{
    return operation == MCL_HANDOFF_OP_PATH_CHALLENGE ||
           operation == MCL_HANDOFF_OP_PATH_RESPONSE ||
           operation == MCL_HANDOFF_OP_COMMIT ||
           operation == MCL_HANDOFF_OP_CONFIRM;
}

uint8_t mcl_handoff_op_carries_challenge(mcl_handoff_op_t operation)
{
    if (operation == MCL_HANDOFF_OP_PATH_CHALLENGE ||
        operation == MCL_HANDOFF_OP_PATH_RESPONSE) {
        return 1u;
    }
    return 0u;
}

size_t mcl_handoff_control_encoded_size(mcl_handoff_op_t operation)
{
    if (mcl_handoff_op_assigned(operation) == 0) {
        return 0u;
    }
    if (mcl_handoff_op_carries_challenge(operation) != 0u) {
        return (size_t)MCL_HANDOFF_CONTROL_MAX_SIZE;
    }
    return (size_t)MCL_HANDOFF_HEADER_SIZE;
}

mcl_link_status_t mcl_handoff_control_encode(
    const mcl_handoff_control_t *control,
    uint8_t *out,
    size_t out_capacity,
    size_t *written)
{
    volatile uint8_t *dst;
    size_t size;
    uint8_t carries;

    if (control == NULL || out == NULL || written == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    size = mcl_handoff_control_encoded_size(control->operation);
    if (size == 0u) {
        /* Reserved, unassigned or Experimental Use: this build will not emit
         * bytes whose meaning it cannot state. */
        return MCL_LINK_ERR_RANGE;
    }

    if (control->migration_ref == MCL_CONTACT_MIGRATION_NONE) {
        return MCL_LINK_ERR_RANGE;
    }
    if (control->session_ref == MCL_CONTACT_SESSION_NONE) {
        return MCL_LINK_ERR_RANGE;
    }

    carries = mcl_handoff_op_carries_challenge(control->operation);
    if (control->challenge_present != carries) {
        /*
         * The struct disagrees with itself. Encoding it anyway would silently
         * drop a challenge the caller believed it was sending, or emit a
         * commit built from a struct still holding a stale one.
         */
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    if (out_capacity < size) {
        return MCL_LINK_ERR_RANGE;
    }

    dst = (volatile uint8_t *)out;
    dst[0] = MCL_HANDOFF_CONTROL_VERSION;
    dst[1] = control->operation;
    mcl_handoff_put_u32(out + 2, control->migration_ref);
    mcl_handoff_put_u32(out + 6, control->session_ref);
    if (carries != 0u) {
        mcl_handoff_copy_challenge(out + MCL_HANDOFF_HEADER_SIZE,
                                   control->challenge);
    }

    *written = size;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_handoff_control_decode(
    const uint8_t *in,
    size_t in_size,
    mcl_handoff_control_t *control)
{
    mcl_handoff_op_t operation;
    uint32_t migration_ref;
    uint32_t session_ref;
    size_t expected;

    if (in == NULL || control == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    if (in_size < (size_t)MCL_HANDOFF_HEADER_SIZE) {
        /*
         * Not enough bytes to read even the operation, so the length this
         * control should have is not yet knowable. Distinct from malformation:
         * a carriage that delivers partial payloads may still complete it.
         */
        return MCL_LINK_ERR_TRUNCATED;
    }

    /*
     * Version first. A future control version must be reported as a version
     * mismatch, not as an unknown operation, or a peer cannot tell "I am too
     * old to speak to you" from "you sent me nonsense".
     */
    if (in[0] != MCL_HANDOFF_CONTROL_VERSION) {
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }

    operation = in[1];
    if (mcl_handoff_op_assigned(operation) == 0) {
        /*
         * Every handoff operation is critical: the operations that exist are
         * the ones that move the state machine, so skipping an unknown one
         * would mean continuing a migration whose steps were not performed.
         */
        return MCL_LINK_ERR_RANGE;
    }

    expected = mcl_handoff_control_encoded_size(operation);
    if (in_size < expected) {
        return MCL_LINK_ERR_TRUNCATED;
    }
    if (in_size > expected) {
        /*
         * The Link frame already declared this payload's length, so trailing
         * bytes mean the two ends disagree about the format rather than that a
         * boundary is unclear.
         */
        return MCL_LINK_ERR_RANGE;
    }

    migration_ref = mcl_handoff_get_u32(in + 2);
    session_ref = mcl_handoff_get_u32(in + 6);
    if (migration_ref == MCL_CONTACT_MIGRATION_NONE) {
        return MCL_LINK_ERR_RANGE;
    }
    if (session_ref == MCL_CONTACT_SESSION_NONE) {
        return MCL_LINK_ERR_RANGE;
    }

    control->operation = operation;
    control->migration_ref = migration_ref;
    control->session_ref = session_ref;
    control->challenge_present = mcl_handoff_op_carries_challenge(operation);
    if (control->challenge_present != 0u) {
        mcl_handoff_copy_challenge(control->challenge,
                                   in + MCL_HANDOFF_HEADER_SIZE);
    } else {
        /* Never leave a stale challenge visible behind an operation that does
         * not carry one. */
        mcl_handoff_clear_challenge(control->challenge);
    }

    return MCL_LINK_OK;
}

static mcl_link_status_t mcl_handoff_make_common(
    mcl_handoff_control_t *control,
    mcl_handoff_op_t operation,
    uint32_t migration_ref,
    uint32_t session_ref)
{
    if (control == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (migration_ref == MCL_CONTACT_MIGRATION_NONE) {
        return MCL_LINK_ERR_RANGE;
    }
    if (session_ref == MCL_CONTACT_SESSION_NONE) {
        return MCL_LINK_ERR_RANGE;
    }

    control->operation = operation;
    control->migration_ref = migration_ref;
    control->session_ref = session_ref;
    control->challenge_present = 0u;
    mcl_handoff_clear_challenge(control->challenge);
    return MCL_LINK_OK;
}

static mcl_link_status_t mcl_handoff_make_with_challenge(
    mcl_handoff_control_t *control,
    mcl_handoff_op_t operation,
    uint32_t migration_ref,
    uint32_t session_ref,
    const uint8_t *challenge)
{
    mcl_link_status_t status;

    if (challenge == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    status = mcl_handoff_make_common(control, operation, migration_ref,
                                     session_ref);
    if (status != MCL_LINK_OK) {
        return status;
    }

    mcl_handoff_copy_challenge(control->challenge, challenge);
    control->challenge_present = 1u;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_handoff_make_path_challenge(
    mcl_handoff_control_t *control,
    uint32_t migration_ref,
    uint32_t session_ref,
    const uint8_t challenge[MCL_CONTACT_CHALLENGE_SIZE])
{
    return mcl_handoff_make_with_challenge(control,
                                           MCL_HANDOFF_OP_PATH_CHALLENGE,
                                           migration_ref, session_ref,
                                           challenge);
}

mcl_link_status_t mcl_handoff_make_path_response(
    mcl_handoff_control_t *control,
    uint32_t migration_ref,
    uint32_t session_ref,
    const uint8_t echo[MCL_CONTACT_CHALLENGE_SIZE])
{
    return mcl_handoff_make_with_challenge(control,
                                           MCL_HANDOFF_OP_PATH_RESPONSE,
                                           migration_ref, session_ref, echo);
}

mcl_link_status_t mcl_handoff_make_commit(
    mcl_handoff_control_t *control,
    uint32_t migration_ref,
    uint32_t session_ref)
{
    return mcl_handoff_make_common(control, MCL_HANDOFF_OP_COMMIT,
                                   migration_ref, session_ref);
}

mcl_link_status_t mcl_handoff_make_confirm(
    mcl_handoff_control_t *control,
    uint32_t migration_ref,
    uint32_t session_ref)
{
    return mcl_handoff_make_common(control, MCL_HANDOFF_OP_CONFIRM,
                                   migration_ref, session_ref);
}
