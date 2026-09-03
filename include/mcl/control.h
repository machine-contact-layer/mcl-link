#ifndef MCL_CONTROL_H
#define MCL_CONTROL_H

#include "mcl/link.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Link controls: ACK, NACK, KEEPALIVE and CLOSE.
 *
 * These are the payloads of the frame classes that carry Link's own business
 * rather than a semantic object. They are specified in
 * spec/link-frame-classes-v0.1.md, which decided the contract for all ten
 * classes; this is that decision in code.
 *
 * None of them is a Wire object. The frame class selects which contract the
 * payload follows, and a payload interpreted under two contracts is a payload
 * with two meanings.
 *
 * NONE OF THEM CHANGES STATE, AND THAT IS DELIBERATE
 *
 * An ACK is a statement about carriage, never about meaning or acceptance. A
 * CLOSE is a notification, not an instruction. A NACK reports a refusal that
 * already happened. Nothing here is authenticated and every medium MCL uses is
 * observable, so a control that could force a peer to act would be a primitive
 * available to anyone in earshot.
 * ============================================================ */

/*
 * Control payload version, carried in the first byte of every control below.
 *
 * Separate from the Link frame's own major version on purpose. The frame header
 * and the class payloads can need to change independently, and a shared version
 * number would force a frame-format revision to reissue every payload format
 * that had not changed.
 */
#define MCL_LINK_CONTROL_VERSION 0u

/* Exact encoded sizes. Each is an exact length, not a maximum: a control that
 * does not fill its frame's payload_len exactly is refused. */
#define MCL_LINK_ACK_SIZE       4u
#define MCL_LINK_NACK_SIZE      4u
#define MCL_LINK_CLOSE_SIZE     2u
#define MCL_LINK_KEEPALIVE_SIZE 0u

/*
 * NACK reasons.
 *
 * EVERY REASON HERE DESCRIBES A FRAME THAT WAS VALID AND WAS REFUSED.
 *
 * An earlier draft also listed MALFORMED, TRUNCATED, UNSUPPORTED_VERSION,
 * UNSUPPORTED_CLASS and FRAME_CHECK_FAILED. All five were removed, because a
 * receiver that rejects a frame at those points cannot quote a trustworthy
 * acked_sequence out of it -- an unsupported major is refused BEFORE the
 * receiver is entitled to read any later field, so "NACK sequence 42,
 * unsupported version" contradicts itself. A truncated frame may not contain
 * the field at all, and a frame whose CRC failed has no field a receiver may
 * rely on.
 *
 * The second reason is sufficient on its own: nothing here is authenticated and
 * the media are open, so a rule that answers malformed bytes with a frame turns
 * any transmitter in range into a source of replies from every MCL node that
 * hears it. Frames that fail before correlation are dropped locally and the
 * carriage resynchronises.
 */
typedef uint8_t mcl_link_nack_reason_t;
enum {
    MCL_LINK_NACK_RESERVED             = 0u, /* never sent */
    /* Well formed; its payload violated its class contract. */
    MCL_LINK_NACK_PAYLOAD_REFUSED      = 1u,
    /*
     * The deployment declined. NOT an error: two correctly implemented peers
     * with different configurations refuse each other at different points, and
     * that is a policy outcome (charter 2.10.1), not an interoperability
     * failure. A peer must be able to say so without claiming the other sent
     * something wrong.
     */
    MCL_LINK_NACK_POLICY_REFUSED       = 2u,
    /*
     * No capacity to accept it now. The only reason for which retrying the
     * IDENTICAL frame is sensible, which is why it is not folded into
     * PAYLOAD_REFUSED: a sender would otherwise treat a full buffer as a
     * permanent protocol error.
     */
    MCL_LINK_NACK_RESOURCE_EXHAUSTED   = 3u,
    /* Not admissible in the receiver's current state. */
    MCL_LINK_NACK_STATE_REFUSED        = 4u,
    /* Decoded, and named something this receiver does not implement. */
    MCL_LINK_NACK_UNSUPPORTED_SEMANTIC = 5u,
    MCL_LINK_NACK_REASON_COUNT         = 6u
};

/* CLOSE reasons. */
typedef uint8_t mcl_link_close_reason_t;
enum {
    MCL_LINK_CLOSE_RESERVED       = 0u, /* never sent */
    MCL_LINK_CLOSE_NORMAL         = 1u, /* the contact is finished */
    MCL_LINK_CLOSE_GOING_AWAY     = 2u, /* shutting down or leaving */
    MCL_LINK_CLOSE_POLICY         = 3u, /* local policy ended it */
    MCL_LINK_CLOSE_TRANSPORT_LOST = 4u, /* the carrier is no longer usable */
    MCL_LINK_CLOSE_REASON_COUNT   = 5u
};

/*
 * ACK and NACK share one structure because they share one layout. The FRAME
 * CLASS says which it is; the payload does not repeat it.
 *
 * `acked_sequence` is NOT this frame's own sequence. They are two different
 * numbers:
 *
 *     frame.sequence     the ordinal of THIS acknowledgement
 *     acked_sequence     the ordinal of the frame BEING acknowledged
 *
 * Overloading one field for both is the obvious byte saving and it is wrong: it
 * makes an acknowledgement indistinguishable from an ordinary frame with that
 * ordinal, and an acknowledgement stream unorderable. The mcl-ip over-air
 * harness sent replies with no correlation at all, and one lost datagram
 * shifted every later reply by one -- presenting as a string of protocol
 * failures that never happened.
 */
typedef struct {
    uint8_t  control_version;
    uint8_t  reason;          /* MUST be 0 in an ACK */
    uint16_t acked_sequence;
} mcl_link_ack_t;

typedef struct {
    uint8_t control_version;
    uint8_t reason;
} mcl_link_close_t;

/*
 * Build an ACK for a frame that was received with a valid SEQUENCE.
 *
 * The caller must have taken `acked_sequence` from a frame it actually decoded.
 * There is no way for this function to check that, which is why the rule is
 * stated in the class audit: only a frame that framed successfully and carried
 * SEQUENCE may be acknowledged at all.
 */
mcl_link_status_t mcl_link_make_ack(
    mcl_link_ack_t *ack,
    uint16_t acked_sequence);

/* Build a NACK. `reason` must be an assigned, non-reserved value. */
mcl_link_status_t mcl_link_make_nack(
    mcl_link_ack_t *nack,
    uint16_t acked_sequence,
    mcl_link_nack_reason_t reason);

/* Build a CLOSE. `reason` must be an assigned, non-reserved value. */
mcl_link_status_t mcl_link_make_close(
    mcl_link_close_t *close_control,
    mcl_link_close_reason_t reason);

/*
 * Encode and decode. `is_ack` selects which validation applies: an ACK's reason
 * MUST be 0, a NACK's MUST NOT be. Passing the wrong one is how a NACK would
 * become an ACK carrying a refusal nobody reads.
 *
 * Decoding enforces the EXACT length. A short buffer reports TRUNCATED and a
 * long one reports RANGE, because those demand opposite responses: more bytes
 * may complete a short buffer, and no number of bytes fixes a control that
 * declares more than its class defines.
 */
mcl_link_status_t mcl_link_ack_encode(
    const mcl_link_ack_t *ack,
    uint8_t is_ack,
    uint8_t *out,
    size_t capacity,
    size_t *written);

mcl_link_status_t mcl_link_ack_decode(
    const uint8_t *data,
    size_t data_size,
    uint8_t is_ack,
    mcl_link_ack_t *ack);

mcl_link_status_t mcl_link_close_encode(
    const mcl_link_close_t *close_control,
    uint8_t *out,
    size_t capacity,
    size_t *written);

mcl_link_status_t mcl_link_close_decode(
    const uint8_t *data,
    size_t data_size,
    mcl_link_close_t *close_control);

/*
 * KEEPALIVE carries nothing, and "nothing" is a contract rather than an
 * absence of one.
 *
 * A non-empty KEEPALIVE is REFUSED rather than ignored. Accepting bytes nobody
 * has defined is how an undocumented sub-protocol appears between two vendors:
 * one starts putting something there, the other starts reading it, and the
 * specification has quietly acquired a field it never described.
 */
mcl_link_status_t mcl_link_keepalive_check(size_t payload_len);

/* ============================================================
 * SEQUENCE WRAP AND THE ACKNOWLEDGEMENT WINDOW
 *
 * `sequence` is 16 bits and wraps at 65536. Once acknowledgements name a
 * sequence, wrap stops being cosmetic: a delayed acknowledgement of sequence 7
 * is indistinguishable from an acknowledgement of the sequence 7 that arrives
 * 65536 frames later, and a sender that matches the wrong one believes a frame
 * arrived that never did.
 *
 * The rule is RFC 1982 serial-number arithmetic, bounded well inside half the
 * space: an implementation MUST NOT have more than MCL_LINK_SEQUENCE_WINDOW_MAX
 * frames outstanding and unacknowledged on one contact, and an acked_sequence
 * outside that window, counting backwards from the highest sequence sent, MUST
 * be discarded rather than matched.
 *
 * A quarter of the space rather than the half RFC 1982 permits, because the
 * boundary case at exactly half is ambiguous by construction, and a limit that
 * is ambiguous at its own edge is one an implementer will get wrong. Nothing in
 * MCL needs 16384 frames in flight; a contact that does has a queueing problem
 * this field cannot fix.
 * ============================================================ */
#define MCL_LINK_SEQUENCE_WINDOW_MAX 16384u

/*
 * Does `acked_sequence` name a frame that could still be outstanding?
 *
 * Sets *live to 1 when it lies within the window ending at `highest_sent`,
 * inclusive of `highest_sent` itself, counting backwards with wrap. The
 * arithmetic is modulo 65536 throughout, so a window that spans the wrap point
 * is handled with no special case -- which is the point of doing it this way
 * rather than with comparisons.
 */
mcl_link_status_t mcl_link_sequence_is_live(
    uint16_t highest_sent,
    uint16_t acked_sequence,
    uint8_t *live);

#ifdef __cplusplus
}
#endif

#endif /* MCL_CONTROL_H */
