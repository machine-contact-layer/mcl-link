#define _CRT_SECURE_NO_WARNINGS
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE_SIZE 16384
#define MAX_ASSIGNMENTS 64
#define MAX_STR_LEN 64

typedef struct {
    int id;
    char name[MAX_STR_LEN];
    char status[MAX_STR_LEN];
    char owner_repo[MAX_STR_LEN];
} transport_assignment_t;

static const char *skip_ws(const char *p, const char *end)
{
    while (p < end && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static const char *parse_string(const char *p, const char *end, char *out, size_t out_size)
{
    size_t len = 0;
    p = skip_ws(p, end);
    if (p >= end || *p != '\"') {
        return NULL;
    }
    ++p;
    while (p < end && *p != '\"') {
        if (*p == '\\' || len + 1 >= out_size) {
            return NULL;
        }
        out[len++] = *p++;
    }
    if (p >= end || *p != '\"') {
        return NULL;
    }
    out[len] = '\0';
    return p + 1;
}

static const char *parse_int(const char *p, const char *end, int *out)
{
    int val = 0;
    p = skip_ws(p, end);
    if (p >= end || !isdigit((unsigned char)*p)) {
        return NULL;
    }
    while (p < end && isdigit((unsigned char)*p)) {
        val = val * 10 + (*p - '0');
        ++p;
    }
    *out = val;
    return p;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "registries/transport-ids-v0.1.json";
    FILE *f;
    char buf[MAX_FILE_SIZE];
    size_t nread;
    const char *p;
    const char *end;
    transport_assignment_t assignments[MAX_ASSIGNMENTS];
    size_t count = 0;
    size_t i, j;
    int has_ap = 0, has_ip = 0, has_ble = 0, has_uwb = 0;

    f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return 1;
    }
    nread = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[nread] = '\0';
    end = buf + nread;

    /* Verify field_width_bits */
    p = strstr(buf, "\"field_width_bits\":");
    if (!p) {
        fprintf(stderr, "Missing field_width_bits\n");
        return 1;
    }
    p += strlen("\"field_width_bits\":");
    {
        int bits = 0;
        p = parse_int(p, end, &bits);
        if (!p || bits != 8) {
            fprintf(stderr, "field_width_bits must be 8, got %d\n", bits);
            return 1;
        }
    }

    /* Parse assignments array */
    p = strstr(buf, "\"assignments\":");
    if (!p) {
        fprintf(stderr, "Missing assignments array\n");
        return 1;
    }
    p = strchr(p, '[');
    if (!p) {
        return 1;
    }
    ++p;

    while (p < end) {
        transport_assignment_t a;
        p = skip_ws(p, end);
        if (p >= end || *p == ']') {
            break;
        }
        if (*p != '{') {
            return 1;
        }
        ++p;
        memset(&a, 0, sizeof(a));

        while (p < end && *p != '}') {
            char key[MAX_STR_LEN];
            p = parse_string(p, end, key, sizeof(key));
            if (!p) return 1;
            p = skip_ws(p, end);
            if (p >= end || *p != ':') return 1;
            ++p;

            if (strcmp(key, "id") == 0) {
                p = parse_int(p, end, &a.id);
                if (!p) return 1;
            } else if (strcmp(key, "name") == 0) {
                p = parse_string(p, end, a.name, sizeof(a.name));
                if (!p) return 1;
            } else if (strcmp(key, "status") == 0) {
                p = parse_string(p, end, a.status, sizeof(a.status));
                if (!p) return 1;
            } else if (strcmp(key, "owner_repo") == 0) {
                p = parse_string(p, end, a.owner_repo, sizeof(a.owner_repo));
                if (!p) return 1;
            } else {
                return 1;
            }

            p = skip_ws(p, end);
            if (p < end && *p == ',') {
                ++p;
            }
        }
        if (p >= end || *p != '}') {
            return 1;
        }
        ++p;

        if (count >= MAX_ASSIGNMENTS) {
            fprintf(stderr, "Too many assignments\n");
            return 1;
        }
        assignments[count++] = a;

        p = skip_ws(p, end);
        if (p < end && *p == ',') {
            ++p;
        }
    }

    /* Validations */
    if (count == 0) {
        fprintf(stderr, "No assignments found\n");
        return 1;
    }

    for (i = 0; i < count; ++i) {
        /* Legal range 1..0x7F for standards action */
        if (assignments[i].id < 1 || assignments[i].id > 0x7F) {
            fprintf(stderr, "Invalid transport ID %d\n", assignments[i].id);
            return 1;
        }
        /* Check unique IDs, names, owner_repos */
        for (j = i + 1; j < count; ++j) {
            if (assignments[i].id == assignments[j].id) {
                fprintf(stderr, "Duplicate transport ID: %d\n", assignments[i].id);
                return 1;
            }
            if (strcmp(assignments[i].name, assignments[j].name) == 0) {
                fprintf(stderr, "Duplicate transport name: %s\n", assignments[i].name);
                return 1;
            }
            if (strcmp(assignments[i].owner_repo, assignments[j].owner_repo) == 0) {
                fprintf(stderr, "Duplicate transport owner: %s\n", assignments[i].owner_repo);
                return 1;
            }
        }

        /* Check specific bindings */
        if (assignments[i].id == 1 && strcmp(assignments[i].name, "MCL_AP") == 0 && strcmp(assignments[i].owner_repo, "mcl-ap") == 0) {
            has_ap = 1;
        }
        if (assignments[i].id == 2 && strcmp(assignments[i].name, "MCL_IP") == 0 && strcmp(assignments[i].owner_repo, "mcl-ip") == 0) {
            has_ip = 1;
        }
        if (assignments[i].id == 3 && strcmp(assignments[i].name, "MCL_BLE") == 0 && strcmp(assignments[i].owner_repo, "mcl-ble") == 0) {
            has_ble = 1;
        }
        if (assignments[i].id == 4 && strcmp(assignments[i].name, "MCL_UWB") == 0 && strcmp(assignments[i].owner_repo, "mcl-uwb") == 0) {
            has_uwb = 1;
        }
    }

    if (!has_ap || !has_ip || !has_ble || !has_uwb) {
        fprintf(stderr, "Missing expected standard transport bindings (AP, IP, BLE, UWB)\n");
        return 1;
    }

    printf("transport assignments: %zu\n", count);
    puts("OK");
    return 0;
}
