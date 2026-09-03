#ifndef MCL_RENDEZVOUS_H
#define MCL_RENDEZVOUS_H

#include "mcl/link.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Endpoint rendezvous: resolving an endpoint_token to a real endpoint.
 *
 * THE PROBLEM
 *
 * TRANSPORT_OFFER carries a 32-bit `endpoint_token`. The bindings define full
 * endpoint descriptors -- address family, mode, port, address for IP; role,
 * address type, MTU, service reference for BLE -- and nothing connected the
 * two. A laboratory harness can paper over this by knowing in advance that
 * token 0x1234 means "the board at 192.168.4.1", but then the experiment proves
 * that the harness knows where the board is, not that MCL negotiated anything.
 * Two implementations from different vendors cannot do that at all.
 *
 * THE APPROACH
 *
 * The token is a RENDEZVOUS REFERENCE, not an address.
 *
 *   1. The offering peer picks a fresh token and puts it in TRANSPORT_OFFER,
 *      which crosses the medium where the machines already are.
 *   2. The offering peer publishes a beacon carrying that token ON THE
 *      CANDIDATE TRANSPORT.
 *   3. The accepting peer searches the candidate transport for that token, and
 *      learns the concrete address from the medium itself.
 *
 * Addresses therefore never have to cross the first-contact medium. That keeps
 * the offer small, which matters most on the transport where bytes are
 * scarcest, and it avoids broadcasting a machine's network topology to
 * everyone in earshot. Wi-Fi Easy Connect uses the same shape: bootstrap
 * information out of band, real network detail over the medium being joined.
 *
 * WHAT A BEACON MATCH MEANS
 *
 * It means a candidate endpoint exists that is publishing this token. It does
 * NOT mean it is the peer the contact began with. The token crossed an
 * observable medium, so any listener can republish a matching beacon and be
 * found first.
 *
 * A match is therefore only the beginning: path validation
 * (mcl_contact_validation_begin / _response) then proves the candidate carries
 * frames in both directions, and even that proves reachability rather than
 * identity. Establishing that the peer is the one from the original contact
 * requires cryptographic contact binding, which MCL does not have.
 *
 * TOKEN HYGIENE
 *
 * A token MUST be fresh per migration transaction. Reusing one lets a stale
 * beacon from an abandoned migration attract a peer to an endpoint nobody is
 * offering any more. It should also be unpredictable, so that a third party
 * cannot publish a matching beacon BEFORE the legitimate one -- which is a
 * race-hardening measure, not a security property, since the token becomes
 * public the moment the offer is sent.
 * ============================================================ */

/*
 * Canonical beacon payload. Byte layout, network byte order:
 *
 *   u8   magic 0x4D          'M'
 *   u8   magic 0x43          'C'
 *   u8   beacon_version      0
 *   u8   transport_id        the candidate transport, from the registry
 *   u32  endpoint_token      the token from TRANSPORT_OFFER
 *
 * The magic exists so that arbitrary traffic on a shared medium -- another
 * protocol's multicast, an unrelated BLE advertisement -- is not mistaken for a
 * rendezvous beacon. It is not a checksum and not authentication.
 *
 * transport_id is included so a beacon observed on the wrong medium is
 * detectably wrong rather than silently accepted, which matters for a scanner
 * watching several media at once.
 *
 * This structure is transport-neutral. WHERE it appears is defined by each
 * binding: BLE puts it in advertising service data, IP in a discovery datagram.
 */
#define MCL_RENDEZVOUS_BEACON_SIZE   8u
#define MCL_RENDEZVOUS_MAGIC_0       0x4Du
#define MCL_RENDEZVOUS_MAGIC_1       0x43u
#define MCL_RENDEZVOUS_BEACON_VERSION 0u

mcl_link_status_t mcl_rendezvous_beacon_encode(
    uint8_t transport_id,
    uint32_t endpoint_token,
    uint8_t *out,
    size_t out_capacity,
    size_t *written);

/*
 * Decode a beacon. Rejects a wrong magic, an unknown beacon version, the
 * reserved transport id and the reserved token, so that a zeroed buffer can
 * never decode as a valid beacon.
 */
mcl_link_status_t mcl_rendezvous_beacon_decode(
    const uint8_t *in,
    size_t in_size,
    uint8_t *transport_id,
    uint32_t *endpoint_token);

/*
 * Convenience for a scanner: does this observed payload carry exactly the
 * beacon we are looking for?
 *
 * Returns 1 only when the payload decodes AND both the transport and the token
 * match. Anything malformed returns 0 rather than an error, because a scanner
 * sees unrelated traffic constantly and that is not an error condition.
 */
uint8_t mcl_rendezvous_beacon_matches(
    const uint8_t *data,
    size_t size,
    uint8_t expected_transport_id,
    uint32_t expected_token);

#ifdef __cplusplus
}
#endif

#endif /* MCL_RENDEZVOUS_H */
