#include "mcl/wire.h"
#include "mcl/link.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK_WIRE_STATUS(call, expected) do { \
    const mcl_wire_status_t st__ = (call); \
    if (st__ != (expected)) { \
        fprintf(stderr, "FAIL at %s:%d: %s returned %d, expected %d\n", \
                __FILE__, __LINE__, #call, (int)st__, (int)(expected)); \
        exit(1); \
    } \
} while (0)

#define CHECK_LINK_STATUS(call, expected) do { \
    const mcl_link_status_t st__ = (call); \
    if (st__ != (expected)) { \
        fprintf(stderr, "FAIL at %s:%d: %s returned %d, expected %d\n", \
                __FILE__, __LINE__, #call, (int)st__, (int)(expected)); \
        exit(1); \
    } \
} while (0)

int main(void)
{
    /* Test that both wire and link symbols can coexist cleanly in one translation unit */
    mcl_wire_header_t wire_header;
    uint8_t wire_bytes[MCL_WIRE_COMMON_HEADER_SIZE];
    mcl_link_t link;
    mcl_link_context_key_t key;

    wire_header.major_version = MCL_WIRE_EXPERIMENTAL_MAJOR;
    wire_header.category = 0u;
    wire_header.opcode = 0u;
    wire_header.priority = 1u;
    wire_header.extension_present = 0u;
    CHECK_WIRE_STATUS(mcl_wire_header_encode(&wire_header, wire_bytes), MCL_WIRE_OK);

    CHECK_LINK_STATUS(mcl_link_init(&link, mcl_link_wire_major_mask(0u)), MCL_LINK_OK);
    CHECK_LINK_STATUS(mcl_link_transition(&link, MCL_LINK_STATE_DISCOVERED), MCL_LINK_OK);
    CHECK_LINK_STATUS(mcl_link_transition(&link, MCL_LINK_STATE_CAPABILITIES), MCL_LINK_OK);
    CHECK_LINK_STATUS(mcl_link_transition(&link, MCL_LINK_STATE_NEGOTIATING), MCL_LINK_OK);

    key.wire_major = 0u;
    key.context_id = 42u;
    key.generation = 1u;
    key.ruleset_digest[0] = 0xAAu;
    key.ruleset_digest_size = 1u;
    CHECK_LINK_STATUS(mcl_link_install_context(&link, &key), MCL_LINK_OK);
    CHECK_LINK_STATUS(mcl_link_transition(&link, MCL_LINK_STATE_ESTABLISHED), MCL_LINK_OK);
    CHECK_LINK_STATUS(mcl_link_authorize_context(&link, &key), MCL_LINK_OK);

    puts("mcl_wire and mcl_link header coexistence: PASS");
    return 0;
}
