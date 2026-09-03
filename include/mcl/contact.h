#ifndef MCL_CONTACT_H
#define MCL_CONTACT_H

#include "mcl/link.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Contact migration: continuing one logical contact across a change of
 * transport.
 *
 * This is the capability that distinguishes MCL from a collection of framing
 * libraries that happen to share a byte layout. Two machines meet on whatever
 * medium exists -- typically acoustic, because it needs no prior network --
 * agree on a better transport, and continue the SAME contact there.
 *
 *      AP  ------- contact begins -------
 *                       |
 *                  TRANSPORT_OFFER
 *                  TRANSPORT_ACCEPT
 *                       |
 *      BLE / IP  ---- same contact continues ----
 *
 *
 * WHAT THIS IS NOT
 *
 * This is ordinary SESSION CONTINUITY. It is correlation, and it involves no
 * cryptography whatsoever.
 *
 * `session_ref` travels in the clear across an observable medium. Anyone within
 * range of the first contact can read it, and anyone can quote it back. It
 * lets an honest peer recognise a contact it is continuing. It establishes
 * NOTHING about who that peer is:
 *
 *      an attacker that observed the contact can present a correct
 *      session_ref on the new transport and be accepted here
 *
 * That is not a defect in this module. It is the honest limit of what
 * correlation without cryptography can do, and it is stated here rather than
 * discovered later. Establishing that the peer on the second transport is
 * cryptographically the peer from the first is a separate, optional property
 * -- CRYPTOGRAPHIC CONTACT BINDING -- which no part of MCL provides today. See
 * mcl-link/research/contact-continuity-experiment.md.
 *
 * Do not build an authorization decision on a matching session_ref. Charter
 * 2.11.2: where local policy requires a property, the absence of the mechanism
 * that establishes it must cause refusal, not acceptance.
 *
 * The vast majority of deployments need exactly what is here and nothing more:
 * a known fleet, an open hazard broadcaster, or any deployment whose peers are
 * already trusted by means outside MCL. Charter 2.10 makes those first-class.
 * ============================================================ */

/*
 * Transport identifiers.
 *
 * These mirror mcl-link/registries/transport-ids-v0.1.json, which is the
 * authoritative assignment. mcl-sdk/tests/test_transport_registry_binding.c
 * pins the registry against the binding headers, because the registry and a
 * published conformance vector have disagreed once already.
 *
 * Zero is permanently reserved so that a zeroed or uninitialised field can
 * never be mistaken for a valid transport.
 */
#define MCL_CONTACT_TRANSPORT_RESERVED 0u
#define MCL_CONTACT_TRANSPORT_AP       1u
#define MCL_CONTACT_TRANSPORT_IP       2u
#define MCL_CONTACT_TRANSPORT_BLE      3u
#define MCL_CONTACT_TRANSPORT_UWB      4u

/*
 * Reserved session reference. A contact that has not agreed a migration has no
 * session reference, and zero is never a valid agreed value -- so an
 * uninitialised field cannot accidentally match.
 */
#define MCL_CONTACT_SESSION_NONE 0u

typedef uint8_t mcl_contact_role_t;
enum {
    /* The peer that offers a transport change. Roles are for correlation and
     * ordering only; they confer no authority. */
    MCL_CONTACT_ROLE_INITIATOR = 0u,
    MCL_CONTACT_ROLE_RESPONDER = 1u
};

typedef uint8_t mcl_contact_state_t;
enum {
    MCL_CONTACT_STATE_NONE     = 0u,  /* not begun */
    MCL_CONTACT_STATE_ACTIVE   = 1u,  /* live on active_transport */
    MCL_CONTACT_STATE_OFFERED  = 2u,  /* a transport change is proposed */
    MCL_CONTACT_STATE_AGREED   = 3u,  /* agreed, awaiting resume on the new transport */
    MCL_CONTACT_STATE_CLOSED   = 4u
};

/*
 * Caller-owned. No heap, no hidden globals, no libc at runtime: an MCL node
 * embeds this wherever it likes and the library never allocates.
 */
typedef struct {
    uint32_t local_ref;
    uint32_t peer_ref;
    uint32_t session_ref;

    uint8_t  role;
    uint8_t  state;
    uint8_t  active_transport;

    uint8_t  pending_transport;
    uint8_t  pending_profile;
    uint32_t pending_endpoint_token;

    uint8_t  peer_ref_valid;
    uint8_t  session_valid;
    uint16_t migration_count;
} mcl_contact_t;

/*
 * Begin a contact on a transport. `local_ref` is this machine's contact
 * reference for correlation; it is not identity and should be rotated per
 * contact so that it cannot be used to track the machine over time.
 */
mcl_link_status_t mcl_contact_begin(
    mcl_contact_t *contact,
    mcl_contact_role_t role,
    uint32_t local_ref,
    uint8_t transport_id);

/* Record the peer's contact reference, learned during first contact. */
mcl_link_status_t mcl_contact_set_peer_ref(
    mcl_contact_t *contact,
    uint32_t peer_ref);

/*
 * Record a proposed transport change, whether this machine sent the offer or
 * received it. Refuses the reserved transport, and refuses an offer naming the
 * transport already in use: changing profile on the current transport is
 * adaptation, not migration, and conflating them would let a peer "migrate"
 * without ever proving it can be reached anywhere else.
 *
 * Whether a given transport is acceptable is deliberately NOT decided here.
 * That is deployment policy (charter 2.10.1); this module enforces only the
 * rules that two implementations must agree on.
 */
mcl_link_status_t mcl_contact_record_offer(
    mcl_contact_t *contact,
    uint8_t transport_id,
    uint8_t profile_id,
    uint32_t endpoint_token);

/*
 * Agree the proposed change, binding a session reference to it. Both peers call
 * this: the accepting peer when it chooses the reference, the offering peer
 * when it receives the acceptance. The transport must match what was offered,
 * so that an acceptance cannot silently redirect the contact to a transport
 * nobody proposed.
 */
mcl_link_status_t mcl_contact_agree(
    mcl_contact_t *contact,
    uint8_t transport_id,
    uint8_t profile_id,
    uint32_t session_ref);

/*
 * Complete the migration when the contact is first heard on the new transport.
 * Both the transport and the session reference must match what was agreed.
 *
 * A match means "this is consistent with the contact I agreed to continue". It
 * does not mean "this is the machine I was talking to" -- see the note at the
 * top of this file.
 */
mcl_link_status_t mcl_contact_resume(
    mcl_contact_t *contact,
    uint8_t transport_id,
    uint32_t session_ref);

/*
 * Abandon a proposed or agreed migration and remain on the current transport.
 *
 * A failed migration must never destroy the contact. The peer may be out of
 * range on the offered transport, may have declined, or may simply never
 * appear; in every case the machines can still talk on the medium where they
 * actually met. Returning to the current transport is the correct outcome, not
 * a degraded one.
 */
mcl_link_status_t mcl_contact_abandon_migration(mcl_contact_t *contact);

/* Close the contact. A closed contact is inert and cannot be resumed. */
mcl_link_status_t mcl_contact_close(mcl_contact_t *contact);

/* Query the transport currently carrying the contact. */
mcl_link_status_t mcl_contact_active_transport(
    const mcl_contact_t *contact,
    uint8_t *transport_id);

#ifdef __cplusplus
}
#endif

#endif /* MCL_CONTACT_H */
