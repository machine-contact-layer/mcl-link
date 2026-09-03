/*
 * MCL Link minimum capability and version negotiation.
 *
 * Normative specification: spec/link-negotiation-v1.md
 *
 * Two implementations cannot interoperate without agreeing which Wire major,
 * which Link major and what maximum frame size they will use, and until this
 * existed nothing in MCL let them say so. This is the smallest mechanism that
 * closes that gap, and it deliberately does not grow past it.
 *
 * WHAT IS NOT HERE, AND WHY
 *
 * Transport and profile selection is absent. TRANSPORT_OFFER/TRANSPORT_ACCEPT
 * already carry transport_id and profile_id and the migration built on them is
 * proven over real radios; a second way to select a transport would be a second
 * vocabulary for one concept, which the registry policy tells reviewers to
 * reject. The profile of the ACTIVE transport needs no negotiation either --
 * two peers exchanging frames already agree on it, or they would not be
 * exchanging frames.
 *
 * Context compression is absent because it is deferred and its codec does not
 * exist. This is not built on the CONTEXT_OFFER/CONTEXT_ACCEPT research draft:
 * no context_id, no generation, no ruleset_digest.
 *
 * THE DESIGN IN ONE PROPERTY
 *
 * The selection function is SYMMETRIC. min() is commutative, & is commutative,
 * and the highest set bit of (A & B) does not depend on operand order. Both
 * peers therefore compute the same answer from the same two inputs in either
 * order, which is why glare needs no tiebreaker here -- unlike migration, where
 * two simultaneous offers propose different transports and cannot both proceed.
 * Two simultaneous CAPABILITY frames do not propose competing outcomes; they
 * supply the two halves of one computation.
 *
 * Negotiation assigns no role and grants no authority. Reception is not
 * identity, authenticity, authority or trust, here as everywhere else.
 */

#ifndef MCL_NEGOTIATION_H
#define MCL_NEGOTIATION_H

#include <stdint.h>
#include <stddef.h>

#include "mcl/link.h"
#include "mcl/control.h"   /* MCL_LINK_CONTROL_VERSION, shared by every Link control */
#include "mcl/handoff.h"  /* MCL_HANDOFF_CONTROL_MAX_SIZE, for the frame floor */

#ifdef __cplusplus
extern "C" {
#endif

/* Exact encoded sizes. Each is an exact length, not a maximum. */
#define MCL_LINK_CAPABILITY_SIZE  9u
#define MCL_LINK_NEGOTIATION_SIZE 7u

/*
 * The smallest maximum-frame-size any implementation may advertise.
 *
 * Derived, never written as a literal: below this the link cannot carry a
 * HANDOFF control with a challenge, which is the largest payload v1 requires --
 * larger than the largest Tier-0 object at 17 bytes. A negotiation that
 * produced a link unable to carry the protocol's own migration frames would
 * succeed and then fail at the worst possible moment.
 *
 * test_negotiation.c asserts this equals its derivation, so it cannot drift
 * when any of the three contributing sizes changes.
 */
#define MCL_LINK_NEGOTIATED_FRAME_FLOOR   \
    (MCL_LINK_FRAME_MIN_SIZE            + \
     MCL_LINK_FRAME_MAX_OPTIONAL        + \
     MCL_HANDOFF_CONTROL_MAX_SIZE)

/*
 * What this node supports.
 *
 * The major fields are BITMAPS: bit N set means major N is supported. Major
 * versions are nibbles on the wire (0..15), so 16 bits covers the space exactly
 * and no escape value is needed.
 *
 * max_frame is what this node will ACCEPT, not what it intends to send.
 * Advertising a receive limit is the only direction the peer that must respect
 * it can act on.
 */
typedef struct {
    uint8_t  control_version;
    uint16_t wire_majors;
    uint16_t link_majors;
    uint16_t max_frame;
    uint16_t features;
} mcl_link_capability_t;

/*
 * What the two nodes will use. Selected values, not bitmaps.
 */
typedef struct {
    uint8_t  control_version;
    uint8_t  wire_major;
    uint8_t  link_major;
    uint16_t max_frame;
    uint16_t features;
} mcl_link_negotiation_t;

/*
 * Build a capability advertisement.
 *
 * Refuses an empty major set: a node that supports no major cannot be
 * negotiated with, and advertising that fact as though it were a position
 * wastes a round trip on both peers. Refuses a max_frame below the floor for
 * the reason given at the constant.
 */
mcl_link_status_t mcl_link_make_capability(
    mcl_link_capability_t *capability,
    uint16_t wire_majors,
    uint16_t link_majors,
    uint16_t max_frame,
    uint16_t features);

mcl_link_status_t mcl_link_capability_encode(
    const mcl_link_capability_t *capability,
    uint8_t *out,
    size_t out_size,
    size_t *written);

mcl_link_status_t mcl_link_capability_decode(
    const uint8_t *in,
    size_t in_size,
    mcl_link_capability_t *capability);

mcl_link_status_t mcl_link_negotiation_encode(
    const mcl_link_negotiation_t *negotiation,
    uint8_t *out,
    size_t out_size,
    size_t *written);

mcl_link_status_t mcl_link_negotiation_decode(
    const uint8_t *in,
    size_t in_size,
    mcl_link_negotiation_t *negotiation);

/*
 * Compute the selection from two capability sets.
 *
 * Symmetric in `local` and `peer` -- see the header comment. Returns
 * MCL_LINK_ERR_RANGE when the two sets share no Wire major, share no Link
 * major, or their common frame size falls below the floor. Those are the three
 * ways two honest implementations can simply be unable to talk, and each is
 * reported rather than papered over with a default.
 */
mcl_link_status_t mcl_link_negotiation_select(
    const mcl_link_capability_t *local,
    const mcl_link_capability_t *peer,
    mcl_link_negotiation_t *selection);

/*
 * Check a received NEGOTIATION.
 *
 * `peer` may be NULL when this node has not received the peer's CAPABILITY. The
 * two branches differ in strength and the difference is deliberate:
 *
 *   peer != NULL   the proposal MUST equal the local computation, field for
 *                  field. This is the strong check.
 *
 *   peer == NULL   the proposal need only be within the local capability. A
 *                  receiver without the peer's advertisement cannot verify the
 *                  peer chose the HIGHEST common major -- only that what it
 *                  chose is legal here. A peer selecting a legal-but-lower
 *                  major is suboptimal, not incorrect, and v1 has no mechanism
 *                  that could tell the two apart. Nothing here is
 *                  authenticated, so this is not downgrade protection and must
 *                  not be read as any.
 */
mcl_link_status_t mcl_link_negotiation_check(
    const mcl_link_negotiation_t *proposal,
    const mcl_link_capability_t *local,
    const mcl_link_capability_t *peer);

#ifdef __cplusplus
}
#endif

#endif /* MCL_NEGOTIATION_H */
