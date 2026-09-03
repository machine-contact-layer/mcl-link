/*
 * Checks the published handoff conformance vectors against the decoder.
 *
 * mcl-link/tests/test_handoff.c mirrors these vectors as C byte arrays, which
 * is fast and readable but proves nothing about the JSON an independent
 * implementer would actually download. The two renderings could drift, and the
 * drift would be invisible: the C suite would stay green while the published
 * artifact described a protocol nobody implements.
 *
 * So this reads the JSON itself, decodes every `bytes_hex` in it, and asserts
 * that the decoder's verdict matches the `expect` the file declares. A vector
 * whose bytes were edited, or an expectation that no longer holds, fails here.
 *
 * A host tool: it uses stdio and the heap-free but hosted C library. It is not
 * part of the freestanding protocol path, exactly like
 * validate_transport_registry.c.
 */

#define _CRT_SECURE_NO_WARNINGS
#include "mcl/handoff.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE_SIZE 65536
#define MAX_HEX_CHARS 512

static char g_file[MAX_FILE_SIZE];
static size_t g_size;

static int g_checked;
static int g_failed;

static void fail(const char *name, const char *why, int got, int expected)
{
    ++g_failed;
    fprintf(stderr, "FAIL vector \"%s\": %s (got %d, expected %d)\n",
            name, why, got, expected);
}

/*
 * Finds the value of a "key": "..." pair starting at or after `from`, without a
 * JSON parser. The file is ours and its shape is fixed; a parser here would be
 * more code to get wrong than the thing it checks.
 */
static const char *find_string_value(
    const char *from,
    const char *end,
    const char *key,
    char *out,
    size_t out_size)
{
    char pattern[64];
    const char *p;
    size_t len = 0u;

    if (strlen(key) + 4u >= sizeof(pattern)) {
        return NULL;
    }
    sprintf(pattern, "\"%s\"", key);

    p = from;
    for (;;) {
        p = strstr(p, pattern);
        if (p == NULL || p >= end) {
            return NULL;
        }
        p += strlen(pattern);
        while (p < end && (*p == ' ' || *p == ':' || *p == '\t' ||
                           *p == '\n' || *p == '\r')) {
            ++p;
        }
        if (p < end && *p == '\"') {
            break;
        }
        /* The key appeared with a non-string value; keep looking. */
    }

    ++p;
    while (p < end && *p != '\"') {
        if (len + 1u >= out_size) {
            return NULL;
        }
        out[len++] = *p++;
    }
    if (p >= end) {
        return NULL;
    }
    out[len] = '\0';
    return p + 1;
}

/*
 * Finds the numeric value of a "key": <number> pair. Positive vectors declare
 * their operation and references in decimal beside the bytes, and those
 * declarations must agree with what the bytes actually decode to -- otherwise
 * the file can describe one control while carrying another, and a round-trip
 * check alone will not notice.
 */
static const char *find_number_value(
    const char *from,
    const char *end,
    const char *key,
    unsigned long *out)
{
    char pattern[64];
    const char *p;

    if (strlen(key) + 4u >= sizeof(pattern)) {
        return NULL;
    }
    sprintf(pattern, "\"%s\"", key);

    p = strstr(from, pattern);
    if (p == NULL || p >= end) {
        return NULL;
    }
    p += strlen(pattern);
    while (p < end && (*p == ' ' || *p == ':' || *p == '\t' ||
                       *p == '\n' || *p == '\r')) {
        ++p;
    }
    if (p >= end || !isdigit((unsigned char)*p)) {
        return NULL;
    }
    *out = strtoul(p, NULL, 10);
    while (p < end && isdigit((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return 10 + (c - 'a'); }
    if (c >= 'A' && c <= 'F') { return 10 + (c - 'A'); }
    return -1;
}

static int decode_hex(const char *hex, uint8_t *out, size_t out_capacity,
                      size_t *written)
{
    size_t len = strlen(hex);
    size_t i;

    if ((len % 2u) != 0u || (len / 2u) > out_capacity) {
        return 0;
    }
    for (i = 0u; i < len; i += 2u) {
        const int hi = hex_value(hex[i]);
        const int lo = hex_value(hex[i + 1u]);
        if (hi < 0 || lo < 0) {
            return 0;
        }
        out[i / 2u] = (uint8_t)((hi << 4) | lo);
    }
    *written = len / 2u;
    return 1;
}

static mcl_link_status_t status_from_name(const char *name)
{
    if (strcmp(name, "TRUNCATED") == 0) { return MCL_LINK_ERR_TRUNCATED; }
    if (strcmp(name, "RANGE") == 0) { return MCL_LINK_ERR_RANGE; }
    if (strcmp(name, "INCOMPATIBLE_VERSION") == 0) {
        return MCL_LINK_ERR_INCOMPATIBLE_VERSION;
    }
    return MCL_LINK_ERR_INVALID_ARGUMENT;
}

/* Walks one array of vector objects between `from` and `end`. */
static void check_section(const char *from, const char *end, int positive)
{
    const char *cursor = from;

    for (;;) {
        char name[128];
        char hex[MAX_HEX_CHARS];
        char expect[64];
        uint8_t bytes[64];
        size_t byte_count = 0u;
        mcl_handoff_control_t control;
        mcl_link_status_t status;
        const char *after_name;
        const char *after_hex;

        after_name = find_string_value(cursor, end, "name", name, sizeof(name));
        if (after_name == NULL) {
            return;
        }
        after_hex = find_string_value(after_name, end, "bytes_hex", hex,
                                      sizeof(hex));
        if (after_hex == NULL) {
            return;
        }
        cursor = after_hex;

        ++g_checked;

        if (hex[0] == '\0') {
            byte_count = 0u;
        } else if (!decode_hex(hex, bytes, sizeof(bytes), &byte_count)) {
            fail(name, "bytes_hex is not valid hex or is too long", 0, 0);
            continue;
        }

        status = mcl_handoff_control_decode(bytes, byte_count, &control);

        if (positive) {
            if (status != MCL_LINK_OK) {
                fail(name, "a positive vector did not decode",
                     (int)status, (int)MCL_LINK_OK);
                continue;
            }
            /*
             * The bytes must carry what the file says they carry. Without this
             * a vector could describe one control and encode another, and the
             * round-trip check below would still pass, because the wrong bytes
             * round-trip perfectly well.
             */
            {
                unsigned long declared = 0u;

                if (find_number_value(after_name, cursor, "operation",
                                      &declared) == NULL) {
                    fail(name, "positive vector declares no operation", 0, 0);
                    continue;
                }
                if ((unsigned long)control.operation != declared) {
                    fail(name, "bytes decode to a different operation",
                         (int)control.operation, (int)declared);
                    continue;
                }
                if (find_number_value(after_name, cursor, "migration_ref",
                                      &declared) == NULL ||
                    (unsigned long)control.migration_ref != declared) {
                    fail(name, "bytes decode to a different migration_ref",
                         (int)control.migration_ref, (int)declared);
                    continue;
                }
                if (find_number_value(after_name, cursor, "session_ref",
                                      &declared) == NULL ||
                    (unsigned long)control.session_ref != declared) {
                    fail(name, "bytes decode to a different session_ref",
                         (int)control.session_ref, (int)declared);
                    continue;
                }
                if (find_number_value(after_name, cursor, "size",
                                      &declared) == NULL ||
                    (unsigned long)byte_count != declared) {
                    fail(name, "declared size differs from the byte count",
                         (int)byte_count, (int)declared);
                    continue;
                }
            }

            /* Re-encoding must reproduce the published bytes exactly. A vector
             * that decodes but does not round-trip is not canonical. */
            {
                uint8_t out[MCL_HANDOFF_CONTROL_MAX_SIZE];
                size_t written = 0u;
                size_t i;

                if (mcl_handoff_control_encode(&control, out, sizeof(out),
                                               &written) != MCL_LINK_OK) {
                    fail(name, "decoded vector did not re-encode", 0, 0);
                    continue;
                }
                if (written != byte_count) {
                    fail(name, "re-encoded length differs",
                         (int)written, (int)byte_count);
                    continue;
                }
                for (i = 0u; i < written; ++i) {
                    if (out[i] != bytes[i]) {
                        fail(name, "re-encoded bytes differ",
                             (int)out[i], (int)bytes[i]);
                        break;
                    }
                }
            }
        } else {
            const char *after_expect =
                find_string_value(cursor, end, "expect", expect,
                                  sizeof(expect));
            mcl_link_status_t wanted;

            if (after_expect == NULL) {
                fail(name, "negative vector has no expect field", 0, 0);
                continue;
            }
            cursor = after_expect;
            wanted = status_from_name(expect);
            if (status != wanted) {
                fail(name, "negative vector produced the wrong status",
                     (int)status, (int)wanted);
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *path;
    FILE *f;
    const char *positive_start;
    const char *negative_start;
    const char *end;

    if (argc < 2) {
        fprintf(stderr, "usage: validate_handoff_vectors <handoff-v0.1.json>\n");
        return 2;
    }
    path = argv[1];

    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }
    g_size = fread(g_file, 1u, sizeof(g_file) - 1u, f);
    fclose(f);
    if (g_size == 0u || g_size >= sizeof(g_file) - 1u) {
        fprintf(stderr, "%s is empty or larger than this tool expects\n", path);
        return 2;
    }
    g_file[g_size] = '\0';
    end = g_file + g_size;

    positive_start = strstr(g_file, "\"positive\"");
    negative_start = strstr(g_file, "\"negative\"");
    if (positive_start == NULL || negative_start == NULL ||
        negative_start <= positive_start) {
        fprintf(stderr, "%s does not contain the expected positive and "
                        "negative sections\n", path);
        return 2;
    }

    check_section(positive_start, negative_start, 1);
    check_section(negative_start, end, 0);

    if (g_checked == 0) {
        fprintf(stderr, "no vectors found in %s -- the file shape changed and "
                        "this check silently stopped checking\n", path);
        return 2;
    }

    printf("handoff vectors: %d checked, %d failed (%s)\n",
           g_checked, g_failed, path);
    return g_failed == 0 ? 0 : 1;
}
