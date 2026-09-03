#ifndef MCL_HANDOFF_H
#define MCL_HANDOFF_H

#include "mcl/contact.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Handoff control: the migration sequence as bytes.
 *
 * THE PROBLEM
 *
 * mcl/contact.h describes a migration sequence in which PATH_CHALLENGE,
 * PATH_RESPONSE, COMMIT and CONFIRM cross the candidate transport. Until this
 * header existed they crossed nothing: they were local function calls, and the
 * sequence diagram in contact.h named four messages that had no representation
 * on any wire.
 *
 * That gap is invisible in a single-implementation test, because both ends call
 * the same functions. It is also invisible in a hardware demonstration driven
 * by a harness that calls those functions on both machines -- such a run proves
 * the radios work, not that the protocol is specified. Two implementations
 * written from the specification could complete TRANSPORT_OFFER and
 * TRANSPORT_ACCEPT, which mcl-wire encodes canonically, and then be unable to
 * exchange a single further byte of the migration.
 *
 * This is the missing half. TRANSPORT_OFFER and TRANSPORT_ACCEPT are Wire
 * semantic objects because they are things one machine MEANS to another. The
 * four controls here are not: they carry no meaning about the world, only about
 * this Link's own transport change. They therefore belong to Link, travel in a
 * Link frame of class HANDOFF, and are specified here rather than in Core.
 *
 * WHY LINK AND NOT WIRE
 *
 * A Wire object survives being relayed, stored and re-encoded under a different
 * context; it means the same thing wherever it arrives. A handoff control means
 * nothing outside the specific Link that is migrating. Encoding it as a
 * semantic object would put a purely local negotiation into the vocabulary
 * every implementation must agree on forever, which the governing rule
 * forbids: freeze only what future implementers must agree on, at the layer
 * that owns it.
 *
 * CANONICAL BYTE LAYOUT, NETWORK BYTE ORDER
 *
 *   u8    control_version           0
 *   u8    operation                 see the operation registry below
 *   u32   migration_ref             non-zero
 *   u32   session_ref               non-zero
 *   u8[8] challenge                 PATH_CHALLENGE and PATH_RESPONSE only
 *
 * PATH_CHALLENGE and PATH_RESPONSE are 18 bytes. COMMIT and CONFIRM are 10.
 * There is no length field and no padding: the operation determines the length
 * exactly, and the Link frame that carries the control already states its
 * payload length. A control whose length disagrees with its operation is
 * malformed rather than merely unexpected, and is rejected.
 *
 * WHY BOTH REFERENCES ARE ON EVERY CONTROL
 *
 * migration_ref alone identifies the transaction, and a reader might conclude
 * session_ref is redundant. It is not. These controls arrive on the CANDIDATE
 * transport, which is a medium the contact has not been using: the receiver
 * must decide which of possibly several contacts a freshly arrived frame
 * belongs to before it can check the transaction at all. session_ref answers
 * "which contact", migration_ref answers "which transport change of that
 * contact". A design that carried only one of them would force the receiver to
 * search its contacts by transaction reference, which is exactly the kind of
 * cross-contact ambiguity that lets one contact's transaction be applied to
 * another. Both are zero-forbidden, so a zeroed buffer can never decode.
 *
 * Neither is identity. Both crossed an observable medium in the clear and can
 * be quoted back by anyone who heard them. See contact.h.
 *
 * WHY THERE IS NO ABORT OPERATION
 *
 * An explicit abort was considered and deliberately not assigned. MCL's
 * negative outcomes are already expressed by absence: an offer that is not
 * accepted simply is not accepted, and mcl_contact_record_offer carries a
 * validity the caller enforces because this library has no clock. Adding an
 * abort would create a second, faster path to a state the timeout already
 * reaches -- and one an attacker who heard the references could send, turning a
 * dropped packet into a cancelled migration. The cost is latency on a failed
 * migration, which is paid by the machine that was already failing to migrate.
 * If a deployment demonstrates that convergence latency actually matters, an
 * abort can be assigned an operation value later without a version change.
 *
 * Convergence after a LOST CONFIRM is a different problem, and absence does not
 * solve that one. It is solved by retransmission: see
 * mcl_contact_commit_repeat in contact.h.
 *
 * FORWARD EXTENSION
 *
 * control_version changes only if this fixed header changes shape. New
 * operations are added by assigning new operation values, and every handoff
 * operation is critical: an implementation that does not recognise an operation
 * MUST reject the control rather than skip it, because the operations that
 * exist are precisely the ones that move the state machine. Skipping one would
 * mean continuing a migration whose steps you did not perform.
 *
 * Operation ranges, following the registry policy MCL uses elsewhere:
 *
 *   0         reserved permanently, so a zeroed buffer is never valid
 *   1..4      assigned below
 *   5..191    unassigned; Specification Required
 *   192..255  Experimental Use
 *
 * An Experimental Use operation is still rejected by any implementation not
 * party to the experiment, which is the correct outcome and not a failure.
 * The authoritative assignment is mcl-link/registries/handoff-ops-v0.1.json.
 * ============================================================ */

#define MCL_HANDOFF_CONTROL_VERSION 0u

/* Fixed header: version, operation, migration_ref, session_ref. */
#define MCL_HANDOFF_HEADER_SIZE 10u

/* The two lengths any well-formed control can have. */
#define MCL_HANDOFF_CONTROL_MIN_SIZE MCL_HANDOFF_HEADER_SIZE
#define MCL_HANDOFF_CONTROL_MAX_SIZE \
    (MCL_HANDOFF_HEADER_SIZE + MCL_CONTACT_CHALLENGE_SIZE)

/* First and last operation value reserved for experiments. */
#define MCL_HANDOFF_OP_EXPERIMENTAL_FIRST 192u
#define MCL_HANDOFF_OP_EXPERIMENTAL_LAST  255u

typedef uint8_t mcl_handoff_op_t;
enum {
    /* Never assigned. A zeroed buffer must not decode as a valid control. */
    MCL_HANDOFF_OP_RESERVED       = 0u,

    /* Sent on the candidate transport; carries the caller's opaque challenge. */
    MCL_HANDOFF_OP_PATH_CHALLENGE = 1u,

    /* Echoes that challenge back, proving the candidate carries frames both
     * ways. Reachability only: an observer of the candidate path sees the
     * challenge and can echo it. */
    MCL_HANDOFF_OP_PATH_RESPONSE  = 2u,

    /* Requests the switch of a validated candidate path. */
    MCL_HANDOFF_OP_COMMIT         = 3u,

    /* Acknowledges the switch. Only after this does the candidate become the
     * active transport for the peer that sent COMMIT. */
    MCL_HANDOFF_OP_CONFIRM        = 4u
};

/*
 * A decoded control. `challenge` is meaningful only when `challenge_present`
 * is 1, which the decoder sets from the operation rather than from any field in
 * the bytes; there is no way to encode a COMMIT that carries a challenge.
 */
typedef struct {
    mcl_handoff_op_t operation;
    uint32_t migration_ref;
    uint32_t session_ref;
    uint8_t  challenge[MCL_CONTACT_CHALLENGE_SIZE];
    uint8_t  challenge_present;
} mcl_handoff_control_t;

/*
 * 1 if `operation` carries a challenge, 0 otherwise. Defined for every value,
 * including unassigned ones, so callers can ask without first validating.
 */
uint8_t mcl_handoff_op_carries_challenge(mcl_handoff_op_t operation);

/*
 * Exact encoded size for an operation, or 0 if the operation is not one this
 * implementation can encode.
 */
size_t mcl_handoff_control_encoded_size(mcl_handoff_op_t operation);

/*
 * Encode a control.
 *
 * Refuses a reserved or unassigned operation, a zero migration_ref, a zero
 * session_ref, and a control whose challenge_present disagrees with its
 * operation -- the last so that a caller cannot produce bytes it did not mean,
 * such as a COMMIT built from a struct still holding a stale challenge.
 */
mcl_link_status_t mcl_handoff_control_encode(
    const mcl_handoff_control_t *control,
    uint8_t *out,
    size_t out_capacity,
    size_t *written);

/*
 * Decode a control from the complete payload of one HANDOFF Link frame.
 *
 * `in_size` must be the exact payload length. Excess bytes are a malformation,
 * not a framing question: the Link frame already declared how long its payload
 * is, so a control that does not fill it is a control whose sender and receiver
 * disagree about the format.
 *
 * Status codes are distinct on purpose, because a receiver reacts differently
 * to each:
 *
 *   MCL_LINK_ERR_TRUNCATED             fewer bytes than the operation requires
 *   MCL_LINK_ERR_RANGE                 excess bytes, reserved or unassigned
 *                                      operation, zero migration_ref, zero
 *                                      session_ref
 *   MCL_LINK_ERR_INCOMPATIBLE_VERSION  a control_version this build does not
 *                                      implement
 *
 * Version is checked before operation, so a future control version reports the
 * version rather than an unknown operation. That distinction is what lets a
 * peer report "I am too old" instead of "you are malformed".
 */
mcl_link_status_t mcl_handoff_control_decode(
    const uint8_t *in,
    size_t in_size,
    mcl_handoff_control_t *control);

/*
 * Build the four controls. These exist so callers cannot assemble a struct
 * whose operation and challenge disagree, and so the challenge is copied
 * through the same volatile-destination discipline the rest of the library
 * uses.
 */
mcl_link_status_t mcl_handoff_make_path_challenge(
    mcl_handoff_control_t *control,
    uint32_t migration_ref,
    uint32_t session_ref,
    const uint8_t challenge[MCL_CONTACT_CHALLENGE_SIZE]);

mcl_link_status_t mcl_handoff_make_path_response(
    mcl_handoff_control_t *control,
    uint32_t migration_ref,
    uint32_t session_ref,
    const uint8_t echo[MCL_CONTACT_CHALLENGE_SIZE]);

mcl_link_status_t mcl_handoff_make_commit(
    mcl_handoff_control_t *control,
    uint32_t migration_ref,
    uint32_t session_ref);

mcl_link_status_t mcl_handoff_make_confirm(
    mcl_handoff_control_t *control,
    uint32_t migration_ref,
    uint32_t session_ref);

#ifdef __cplusplus
}
#endif

#endif /* MCL_HANDOFF_H */
