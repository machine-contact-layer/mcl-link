#include "mcl/link.h"

static void mcl_link_zero_context(mcl_link_context_key_t *key)
{
    size_t i;
    if (key == NULL) {
        return;
    }
    key->wire_major = 0u;
    key->context_id = 0u;
    key->generation = 0u;
    key->ruleset_digest_size = 0u;
    for (i = 0u; i < MCL_LINK_RULESET_DIGEST_MAX_SIZE; ++i) {
        key->ruleset_digest[i] = 0u;
    }
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
    size_t i;

    if (link == NULL || key == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
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

    for (i = 0u; i < (size_t)key->ruleset_digest_size; ++i) {
        link->active_context.ruleset_digest[i] = key->ruleset_digest[i];
    }
    for (; i < MCL_LINK_RULESET_DIGEST_MAX_SIZE; ++i) {
        link->active_context.ruleset_digest[i] = 0u;
    }

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
