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
static void mcl_contact_clear(mcl_contact_t *contact)
{
    contact->local_ref = 0u;
    contact->peer_ref = 0u;
    contact->session_ref = MCL_CONTACT_SESSION_NONE;
    contact->role = MCL_CONTACT_ROLE_INITIATOR;
    contact->state = MCL_CONTACT_STATE_NONE;
    contact->active_transport = MCL_CONTACT_TRANSPORT_RESERVED;
    contact->pending_transport = MCL_CONTACT_TRANSPORT_RESERVED;
    contact->pending_profile = 0u;
    contact->pending_endpoint_token = 0u;
    contact->peer_ref_valid = 0u;
    contact->session_valid = 0u;
    contact->migration_count = 0u;
}

static void mcl_contact_clear_pending(mcl_contact_t *contact)
{
    contact->pending_transport = MCL_CONTACT_TRANSPORT_RESERVED;
    contact->pending_profile = 0u;
    contact->pending_endpoint_token = 0u;
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

    contact->peer_ref = peer_ref;
    contact->peer_ref_valid = 1u;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_record_offer(
    mcl_contact_t *contact,
    uint8_t transport_id,
    uint8_t profile_id,
    uint32_t endpoint_token)
{
    if (contact == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (transport_id == MCL_CONTACT_TRANSPORT_RESERVED) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state != MCL_CONTACT_STATE_ACTIVE) {
        /*
         * A second offer while one is outstanding is refused rather than
         * silently replacing the first. Two peers offering simultaneously must
         * resolve that explicitly, because silently keeping the newest offer
         * would let either side redirect the migration by re-offering.
         */
        return MCL_LINK_ERR_INVALID_STATE;
    }
    if (transport_id == contact->active_transport) {
        /* Changing profile on the current transport is adaptation, not
         * migration. Accepting it here would let a peer complete a "migration"
         * without ever demonstrating reachability anywhere else. */
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }

    contact->pending_transport = transport_id;
    contact->pending_profile = profile_id;
    contact->pending_endpoint_token = endpoint_token;
    contact->state = MCL_CONTACT_STATE_OFFERED;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_contact_agree(
    mcl_contact_t *contact,
    uint8_t transport_id,
    uint8_t profile_id,
    uint32_t session_ref)
{
    if (contact == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (session_ref == MCL_CONTACT_SESSION_NONE) {
        /* Zero is reserved so that an uninitialised field cannot match. */
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state != MCL_CONTACT_STATE_OFFERED) {
        return MCL_LINK_ERR_INVALID_STATE;
    }
    if (transport_id != contact->pending_transport) {
        /* An acceptance must not redirect the contact to a transport that was
         * never offered. */
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

mcl_link_status_t mcl_contact_resume(
    mcl_contact_t *contact,
    uint8_t transport_id,
    uint32_t session_ref)
{
    if (contact == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (contact->state != MCL_CONTACT_STATE_AGREED) {
        return MCL_LINK_ERR_INVALID_STATE;
    }
    if (contact->session_valid == 0u) {
        return MCL_LINK_ERR_INVALID_STATE;
    }
    if (transport_id != contact->pending_transport) {
        return MCL_LINK_ERR_CONTEXT_MISMATCH;
    }
    if (session_ref != contact->session_ref) {
        return MCL_LINK_ERR_CONTEXT_MISMATCH;
    }

    contact->active_transport = contact->pending_transport;
    mcl_contact_clear_pending(contact);
    contact->state = MCL_CONTACT_STATE_ACTIVE;

    /*
     * Saturate rather than wrap. A wrapped counter would silently report a
     * long-lived contact as a fresh one, and this value exists precisely to
     * make repeated migration visible to policy and to diagnostics.
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
    if (contact->state != MCL_CONTACT_STATE_OFFERED &&
        contact->state != MCL_CONTACT_STATE_AGREED) {
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
