#include "mcl/rendezvous.h"
#include "mcl/contact.h"

/*
 * Written through a volatile destination so the compiler cannot rewrite the
 * byte loop into a memcpy call, which would break the freestanding contract at
 * link time on a target with no libc. This has already happened once in this
 * repository. See mcl-core/governance/IMPLEMENTATION_CONTRACT.md section 2.2.
 */
static void mcl_rendezvous_put_u32(uint8_t *dst, uint32_t value)
{
    volatile uint8_t *out = (volatile uint8_t *)dst;
    out[0] = (uint8_t)((value >> 24) & 0xFFu);
    out[1] = (uint8_t)((value >> 16) & 0xFFu);
    out[2] = (uint8_t)((value >> 8) & 0xFFu);
    out[3] = (uint8_t)(value & 0xFFu);
}

static uint32_t mcl_rendezvous_get_u32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

mcl_link_status_t mcl_rendezvous_beacon_encode(
    uint8_t transport_id,
    uint32_t endpoint_token,
    uint8_t *out,
    size_t out_capacity,
    size_t *written)
{
    volatile uint8_t *dst;

    if (out == NULL || written == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (transport_id == MCL_CONTACT_TRANSPORT_RESERVED) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (endpoint_token == 0u) {
        /*
         * Reserved so a zeroed buffer cannot be a valid beacon, and so a peer
         * that forgot to set the token does not silently publish one that
         * everything matches.
         */
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (out_capacity < MCL_RENDEZVOUS_BEACON_SIZE) {
        return MCL_LINK_ERR_RANGE;
    }

    dst = (volatile uint8_t *)out;
    dst[0] = MCL_RENDEZVOUS_MAGIC_0;
    dst[1] = MCL_RENDEZVOUS_MAGIC_1;
    dst[2] = MCL_RENDEZVOUS_BEACON_VERSION;
    dst[3] = transport_id;
    mcl_rendezvous_put_u32(out + 4, endpoint_token);

    *written = MCL_RENDEZVOUS_BEACON_SIZE;
    return MCL_LINK_OK;
}

mcl_link_status_t mcl_rendezvous_beacon_decode(
    const uint8_t *in,
    size_t in_size,
    uint8_t *transport_id,
    uint32_t *endpoint_token)
{
    uint32_t token;

    if (in == NULL || transport_id == NULL || endpoint_token == NULL) {
        return MCL_LINK_ERR_INVALID_ARGUMENT;
    }
    if (in_size < MCL_RENDEZVOUS_BEACON_SIZE) {
        /* Distinct from malformation: a stream carriage may yet receive more. */
        return MCL_LINK_ERR_TRUNCATED;
    }
    if (in[0] != MCL_RENDEZVOUS_MAGIC_0 || in[1] != MCL_RENDEZVOUS_MAGIC_1) {
        return MCL_LINK_ERR_CONTEXT_MISMATCH;
    }
    if (in[2] != MCL_RENDEZVOUS_BEACON_VERSION) {
        /* A future beacon version is not guessed at. */
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }
    if (in[3] == MCL_CONTACT_TRANSPORT_RESERVED) {
        return MCL_LINK_ERR_RANGE;
    }

    token = mcl_rendezvous_get_u32(in + 4);
    if (token == 0u) {
        return MCL_LINK_ERR_RANGE;
    }

    *transport_id = in[3];
    *endpoint_token = token;
    return MCL_LINK_OK;
}

uint8_t mcl_rendezvous_beacon_matches(
    const uint8_t *data,
    size_t size,
    uint8_t expected_transport_id,
    uint32_t expected_token)
{
    uint8_t transport_id = 0u;
    uint32_t token = 0u;

    if (mcl_rendezvous_beacon_decode(data, size, &transport_id, &token) != MCL_LINK_OK) {
        /*
         * Not an error. A scanner on a shared medium sees unrelated traffic
         * continuously, and reporting each non-beacon as a failure would make
         * the normal case look pathological.
         */
        return 0u;
    }
    if (transport_id != expected_transport_id) {
        return 0u;
    }
    if (token != expected_token) {
        return 0u;
    }
    return 1u;
}
