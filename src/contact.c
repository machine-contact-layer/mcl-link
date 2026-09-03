#include "mcl/contact.h"

/*
 * Every field is assigned explicitly rather than through a struct-wide clear.
 *
 * A bulk clear is what an optimising compiler rewrites into a call to memset,
 * which breaks the freestanding contract at link time on a target with no libc.
 * That has already happened once in this repository, in mcl_link_zero_context,
 * and it was invisible until the objects' undefined symbols were inspected.
 * See mcl-core/governance/IMPLEMENTATION_CONTRACT.md section 2.2.
 */
static void mcl_contact_clear_challenge(mcl_contact_t *contact)
{
    volatile uint8_t *out = (volatile uint8_t *)contact->challenge;
    unsigned i;
    for (i = 0u; i < MCL_CONTACT_CHALLENGE_SIZE; ++i) {
        out[i] = 0u;
    }
    contact->challenge_valid = 0u;
}

static void mcl_contact_store_challenge(
    mcl_contact_t *contact,
    const uint8_t *challenge)
{
    volatile uint8_t *out = (volatile uint8_t *)contact->challenge;
    unsigned i;
    for (i = 0u; i < MCL_CONTACT_CHALLENGE_SIZE; ++i) {
        out[i] = challenge[i];
    }
    contact->challenge_valid = 1u;
}

/*
 * Compares the whole challenge without an early exit. The challenge is not a
 * secret, so this is not a timing defence; it simply avoids establishing the
 * habit of early-exit comparison in a module that a security profile will one
 * day sit directly on top of.
 */
static int mcl_contact_challenge_equals(
    const mcl_contact_t *contact,
    const uint8_t *echo)
{
    unsigned differing = 0u;
    unsigned i;
    for (i = 0u; i < MCL_CONTACT_CHALLENGE_SIZE; ++i) {
        differing |= (unsigned)(contact->challenge[i] ^ echo[i]);
    }
    return differing == 0u;
}

static void mcl_contact_clear_pending(mcl_contact_t *contact)
{
    contact->pending_migration_ref = MCL_CONTACT_MIGRATION_NONE;
    contact->pending_transport = MCL_CONTACT_TRANSPORT_RESERVED;
    contact->pending_profile = 0u;
    contact->pending_endpoint_token = 0u;
    contact->pending_validity = 0u;
    mcl_contact_clear_challenge(contact);
}

static void mcl_contact_clear(mcl_contact_t *contact)
{
    contact->local_ref = 0u;
    contact->peer_ref = 0u;
    contact->session_ref = MCL_CONTACT_SESSION_NONE;
    contact->role = MCL_CONTACT_ROLE_INITIATOR;
    contact->state = MCL_CONTACT_STATE_NONE;
    contact->active_transport = MCL_CONTACT_TRANSPORT_RESERVED;
    contact->peer_ref_valid = 0u;
    contact->session_valid = 0u;
    contact->migration_count = 0u;
    mcl_contact_clear_pending(contact);
}

/* True while a migration transaction is outstanding at any stage. */
static int mcl_contact_migration_in_progress(const mcl_contact_t *contact)
{
    return contact->state == MCL_CONTACT_STATE_OFFERED ||
           contact->state == MCL_CONTACT_STATE_AGREED ||
           contact->state == MCL_CONTACT_STATE_VALIDATING ||
           contact->state == MCL_CONTACT_STATE_VALIDATED ||
           contact->state == MCL_CONTACT_STATE_COMMITTING;
}

/*
 * Checks the transaction identity carried by every post-acceptance message.
 * Gathered into one place so a future message cannot be added that validates
 * only some of it.
 */
static mcl_link_status_t mcl_contact_check_transaction(
    const mcl_contact_t *contact,
    uint32_t migration_ref,
    uint32_t session_ref)
{
    if (contact->session_valid == 0u) {
        return MCL_LINK_ERR_INVALID_STATE;
    }
    if (migration_ref != contact->pending_migration_ref) {
        return MCL_LINK_ERR_CONTEXT_MISMATCH;
    }
    if (session_ref != contact->session_ref) {
        return MCL_LINK_ERR_CONTEXT_MISMATCH;
    }
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_begin(
    mcl_contact_t *contact,
    mcl_contact_role_t role,
    uint32_t local_ref,
    uint8_t transport_id)
{
    if (contact == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (role != MCL_CONTACT_ROLE_INITIATOR && role != MCL_CONTACT_ROLE_RESPONDER) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (transport_id == MCL_CONTACT_TRANSPORT_RESERVED) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    mcl_contact_clear(contact);
    contact->role = role;
    contact->local_ref = local_ref;
    contact->active_transport = transport_id;
    contact->state = MCL_CONTACT_STATE_ACTIVE;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_set_peer_ref(
    mcl_contact_t *contact,
    uint32_t peer_ref)
{
    if (contact == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state == MCL_CONTACT_STATE_NONE ||
        contact->state == MCL_CONTACT_STATE_CLOSED) {
        return MCL_LINK_ERR_INVALID_STATE;
    }

    if (contact->peer_ref_valid != 0u) {
        /* First write wins. Repeating the same value is harmless; changing it
         * would let a peer, or a third party, redirect an established
         * correlation. */
        return (peer_ref == contact->peer_ref)
            ? MCL_LINK_OK
            : MCL_LINK_ERR_CONTEXT_MISMATCH;
    }

    contact->peer_ref = peer_ref;
    contact->peer_ref_valid = 1u;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_record_offer(
    mcl_contact_t *contact,
    uint32_t migration_ref,
    uint8_t transport_id,
    uint8_t profile_id,
    uint32_t endpoint_token,
    uint8_t validity)
{
    if (contact == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (migration_ref == MCL_CONTACT_MIGRATION_NONE) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (transport_id == MCL_CONTACT_TRANSPORT_RESERVED) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state != MCL_CONTACT_STATE_ACTIVE) {
        /*
         * A simultaneous offer is not resolved here. The caller detects the
         * collision and calls mcl_contact_resolve_offer_collision, because
         * silently replacing the outstanding offer would let either side
         * redirect the migration by re-offering.
         */
        return MCL_LINK_ERR_INVALID_STATE;
    }
    if (transport_id == contact->active_transport) {
        /* Changing profile on the current transport is adaptation, not
         * migration. Accepting it here would let a peer complete a "migration"
         * without ever demonstrating reachability anywhere else. */
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    contact->pending_migration_ref = migration_ref;
    contact->pending_transport = transport_id;
    contact->pending_profile = profile_id;
    contact->pending_endpoint_token = endpoint_token;
    contact->pending_validity = validity;
    mcl_contact_clear_challenge(contact);
    contact->state = MCL_CONTACT_STATE_OFFERED;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_resolve_offer_collision(
    mcl_contact_t *contact,
    uint32_t peer_source_ref,
    uint32_t peer_migration_ref,
    uint8_t peer_transport_id,
    uint8_t peer_profile_id,
    uint32_t peer_endpoint_token,
    uint8_t peer_validity,
    mcl_contact_collision_t *outcome)
{
    int peer_wins;
    int tie;

    if (contact == NULL || outcome == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (peer_migration_ref == MCL_CONTACT_MIGRATION_NONE) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (peer_transport_id == MCL_CONTACT_TRANSPORT_RESERVED) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state != MCL_CONTACT_STATE_OFFERED) {
        /* There is no collision to resolve unless our own offer is
         * outstanding. */
        return MCL_LINK_ERR_INVALID_STATE;
    }

    /* Compare (source_ref, migration_ref) as one logical key, most significant
     * component first. Both peers compare the same two values. */
    if (peer_source_ref != contact->local_ref) {
        peer_wins = (peer_source_ref > contact->local_ref);
        tie = 0;
    } else if (peer_migration_ref != contact->pending_migration_ref) {
        peer_wins = (peer_migration_ref > contact->pending_migration_ref);
        tie = 0;
    } else {
        peer_wins = 0;
        tie = 1;
    }

    if (tie != 0) {
        /* Indistinguishable keys. Continuing would leave the peers disagreeing
         * about who controls the migration, so both transactions end and the
         * callers retry with fresh references. */
        mcl_contact_clear_pending(contact);
        contact->state = MCL_CONTACT_STATE_ACTIVE;
        *outcome = MCL_CONTACT_COLLISION_TIE_ABORT;
        return MCL_LINK_OK;
    }

    if (peer_wins != 0) {
        if (peer_transport_id == contact->active_transport) {
            /* The peer's winning offer is still subject to the same rule: a
             * migration must name a different transport. */
            return MCL_LINK_ERR_INVALID_ARGUMENT;
        }
        contact->pending_migration_ref = peer_migration_ref;
        contact->pending_transport = peer_transport_id;
        contact->pending_profile = peer_profile_id;
        contact->pending_endpoint_token = peer_endpoint_token;
        contact->pending_validity = peer_validity;
        mcl_contact_clear_challenge(contact);
        contact->state = MCL_CONTACT_STATE_OFFERED;
        *outcome = MCL_CONTACT_COLLISION_PEER_WINS;
        return MCL_LINK_OK;
    }

    *outcome = MCL_CONTACT_COLLISION_LOCAL_WINS;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_agree(
    mcl_contact_t *contact,
    uint32_t migration_ref,
    uint8_t transport_id,
    uint8_t profile_id,
    uint32_t session_ref)
{
    if (contact == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (session_ref == MCL_CONTACT_SESSION_NONE) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state != MCL_CONTACT_STATE_OFFERED) {
        return MCL_LINK_ERR_INVALID_STATE;
    }
    if (migration_ref != contact->pending_migration_ref) {
        /* A delayed acceptance from an abandoned offer. Without this check it
         * would be indistinguishable from the current one. */
        return MCL_LINK_ERR_CONTEXT_MISMATCH;
    }
    if (transport_id != contact->pending_transport) {
        return MCL_LINK_ERR_CONTEXT_MISMATCH;
    }
    if (profile_id != contact->pending_profile) {
        return MCL_LINK_ERR_CONTEXT_MISMATCH;
    }

    contact->session_ref = session_ref;
    contact->session_valid = 1u;
    contact->state = MCL_CONTACT_STATE_AGREED;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_validation_begin(
    mcl_contact_t *contact,
    const uint8_t challenge[MCL_CONTACT_CHALLENGE_SIZE])
{
    if (contact == NULL || challenge == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state != MCL_CONTACT_STATE_AGREED) {
        return MCL_LINK_ERR_INVALID_STATE;
    }
    if (contact->session_valid == 0u) {
        return MCL_LINK_ERR_INVALID_STATE;
    }

    mcl_contact_store_challenge(contact, challenge);
    contact->state = MCL_CONTACT_STATE_VALIDATING;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_validation_response(
    mcl_contact_t *contact,
    uint32_t migration_ref,
    uint32_t session_ref,
    const uint8_t echo[MCL_CONTACT_CHALLENGE_SIZE])
{
    mcl_link_status_t status;

    if (contact == NULL || echo == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state != MCL_CONTACT_STATE_VALIDATING) {
        return MCL_LINK_ERR_INVALID_STATE;
    }
    if (contact->challenge_valid == 0u) {
        return MCL_LINK_ERR_INVALID_STATE;
    }

    status = mcl_contact_check_transaction(contact, migration_ref, session_ref);
    if (status != MCL_LINK_OK) {
        return status;
    }
    if (mcl_contact_challenge_equals(contact, echo) == 0) {
        return MCL_LINK_ERR_CONTEXT_MISMATCH;
    }

    contact->state = MCL_CONTACT_STATE_VALIDATED;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_commit_begin(mcl_contact_t *contact)
{
    if (contact == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state != MCL_CONTACT_STATE_VALIDATED) {
        /* Committing an unvalidated path is exactly the defect this state
         * machine exists to prevent. */
        return MCL_LINK_ERR_INVALID_STATE;
    }

    contact->state = MCL_CONTACT_STATE_COMMITTING;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_commit_confirm(
    mcl_contact_t *contact,
    uint32_t migration_ref,
    uint32_t session_ref)
{
    mcl_link_status_t status;

    if (contact == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state != MCL_CONTACT_STATE_COMMITTING) {
        return MCL_LINK_ERR_INVALID_STATE;
    }

    status = mcl_contact_check_transaction(contact, migration_ref, session_ref);
    if (status != MCL_LINK_OK) {
        return status;
    }

    contact->active_transport = contact->pending_transport;
    mcl_contact_clear_pending(contact);
    contact->state = MCL_CONTACT_STATE_ACTIVE;

    /*
     * Saturate rather than wrap. A wrapped counter would silently report a
     * long-lived contact as a fresh one, and this value exists precisely to
     * make repeated migration visible to policy and diagnostics.
     */
    if (contact->migration_count < 0xFFFFu) {
        contact->migration_count = (uint16_t)(contact->migration_count + 1u);
    }
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_abandon_migration(mcl_contact_t *contact)
{
    if (contact == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (mcl_contact_migration_in_progress(contact) == 0) {
        return MCL_LINK_ERR_INVALID_STATE;
    }

    mcl_contact_clear_pending(contact);
    contact->session_ref = MCL_CONTACT_SESSION_NONE;
    contact->session_valid = 0u;
    contact->state = MCL_CONTACT_STATE_ACTIVE;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_close(mcl_contact_t *contact)
{
    if (contact == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state == MCL_CONTACT_STATE_NONE) {
        return MCL_LINK_ERR_INVALID_STATE;
    }

    mcl_contact_clear_pending(contact);
    contact->session_ref = MCL_CONTACT_SESSION_NONE;
    contact->session_valid = 0u;
    contact->state = MCL_CONTACT_STATE_CLOSED;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_active_transport(
    const mcl_contact_t *contact,
    uint8_t *transport_id)
{
    if (contact == NULL || transport_id == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state == MCL_CONTACT_STATE_NONE ||
        contact->state == MCL_CONTACT_STATE_CLOSED) {
        return MCL_LINK_ERR_INVALID_STATE;
    }

    *transport_id = contact->active_transport;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_link_state(
    const mcl_contact_t *contact,
    mcl_link_state_t *state)
{
    if (contact == NULL || state == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    switch (contact->state) {
    case MCL_CONTACT_STATE_ACTIVE:
        *state = MCL_LINK_STATE_ESTABLISHED;
        break;
    case MCL_CONTACT_STATE_OFFERED:
        *state = MCL_LINK_STATE_NEGOTIATING;
        break;
    case MCL_CONTACT_STATE_AGREED:
    case MCL_CONTACT_STATE_VALIDATING:
    case MCL_CONTACT_STATE_VALIDATED:
    case MCL_CONTACT_STATE_COMMITTING:
        /* The whole candidate-path sequence is handoff in Link terms: agreed
         * but not yet carrying the contact. */
        *state = MCL_LINK_STATE_HANDOFF;
        break;
    case MCL_CONTACT_STATE_CLOSED:
        *state = MCL_LINK_STATE_CLOSED;
        break;
    case MCL_CONTACT_STATE_NONE:
    default:
        *state = MCL_LINK_STATE_IDLE;
        break;
    }
    return MCL_LINK_OK;
}
