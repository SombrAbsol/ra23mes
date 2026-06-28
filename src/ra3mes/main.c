/*
 * Text converter for Pokémon Ranger: Guardian Signs.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include "utils.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif

/*
 * Validate whether a buffer matches the expected Pokémon Ranger 3 MES file
 * structure.
 */
static bool is_valid_mes(const uint8_t *buf, size_t size)
{
    if (size < 8) { // minimum header size
        return false;
    }

    uint32_t total = read_u32_le(buf); // total file size
    uint32_t count = read_u32_le(buf + 4); // number of strings

    // stored total size must match actual buffer size
    if (total != size) {
        return false;
    }

    // offset table must fit within file
    if ((uint64_t)8 + (uint64_t)count * 4 > size) {
        return false;
    }

    uint32_t header = 8 + count * 4;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = read_u32_le(buf + 8 + i * 4);

        // offset must point past the header and offset table
        if (off < header) {
            return false;
        }

        // offset must be within file bounds
        if (off >= size) {
            return false;
        }

        size_t max;
        if (i + 1 < count) {
            uint32_t next = read_u32_le(buf + 8 + (i + 1) * 4);

            // offsets must be strictly increasing
            if (next <= off) {
                return false;
            }

            max = next - off;
        } else {
            max = size - off;
        }

        // string must have a null terminator within its bounds
        if (strnlen((const char *)(buf + off), max) == max) {
            return false;
        }
    }

    return true;
}

/*
 * Convert a Pokémon Ranger 3 MES file into JSON.
 */
static bool mes_to_json(const char *input, const char *output)
{
    size_t size;
    unsigned char *buf = read_file(input, &size);
    if (!buf) {
        fprintf(stderr, "mes_to_json: failed to read '%s'\n", input);
        goto error;
    }

    if (!is_valid_mes(buf, size)) {
        fprintf(stderr, "mes_to_json: invalid MES file '%s'\n", input);
        goto error;
    }

    uint32_t count = read_u32_le(buf + 4);

    char **strings = calloc(count, sizeof(char *));
    if (!strings) {
        fprintf(stderr, "mes_to_json: memory allocation failed\n");
        goto error;
    }

    // read each string via offset table
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = read_u32_le(buf + 8 + i * 4);

        strings[i] = xstrdup((char *)(buf + off));
        if (!strings[i]) {
            fprintf(stderr,
                "mes_to_json: memory allocation failed for string at index "
                "%u\n",
                i);
            goto error_strings;
        }
    }

    bool ok = write_json_strings(output, strings, count);

    free_string_array(strings, count);
    free(buf);
    return ok;

error_strings:
    free_string_array(strings, count);
error:
    free(buf);
    return false;
}

/*
 * Convert a JSON file into Pokémon Ranger 3 MES format.
 */
static bool json_to_mes(const char *input, const char *output)
{
    uint32_t count;
    char **strings = read_json_strings(input, &count);
    if (!strings) {
        fprintf(stderr, "json_to_mes: failed to parse JSON '%s'\n", input);
        return false;
    }

    uint32_t *offsets = malloc(count * sizeof(uint32_t));
    if (!offsets) {
        fprintf(stderr, "json_to_mes: memory allocation failed\n");
        goto error;
    }

    /*
     * Layout: 8-byte header + offset table containing count * 4 bytes,
     * followed by string data.
     */
    uint32_t header = 8 + count * 4;
    uint32_t cur = header;

    // compute absolute offsets for each string
    for (uint32_t i = 0; i < count; i++) {
        offsets[i] = cur;
        uint32_t len = (uint32_t)strlen(strings[i]) + 1;
        cur += len + pad4(len);
    }

    FILE *f = xfopen(output, "wb");
    if (!f) {
        fprintf(stderr, "json_to_mes: cannot open '%s'\n", output);
        goto error;
    }

    unsigned char b[4];

    // write header
    write_u32_le(b, cur);
    if (fwrite(b, 1, 4, f) != 4) {
        fprintf(stderr,
            "json_to_mes: failed to write total size to '%s'\n",
            output);
        goto error_io;
    }
    write_u32_le(b, count);
    if (fwrite(b, 1, 4, f) != 4) {
        fprintf(stderr,
            "json_to_mes: failed to write string count to '%s'\n",
            output);
        goto error_io;
    }

    // write offset table
    for (uint32_t i = 0; i < count; i++) {
        write_u32_le(b, offsets[i]);
        if (fwrite(b, 1, 4, f) != 4) {
            fprintf(stderr, "json_to_mes: failed to write offset table\n");
            goto error_io;
        }
    }

    // write string data
    for (uint32_t i = 0; i < count; i++) {
        uint32_t len = (uint32_t)strlen(strings[i]) + 1;
        if (fwrite(strings[i], 1, len, f) != len) {
            fprintf(stderr,
                "json_to_mes: failed to write string %u to '%s'\n",
                i,
                output);
            goto error_io;
        }

        // pad to 4-byte boundary
        static const unsigned char zero_pad[4] = { 0 };
        uint32_t pad = pad4(len);
        if (pad && fwrite(zero_pad, 1, pad, f) != pad) {
            fprintf(stderr,
                "json_to_mes: failed to write padding for string %u to '%s'\n",
                i,
                output);
            goto error_io;
        }
    }

    fclose(f);
    free(offsets);
    free_string_array(strings, count);
    return true;

error_io:
    fclose(f);
error:
    free(offsets);
    free_string_array(strings, count);
    return false;
}

/*
 * Command-line interface.
 */
int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); // ensure UTF-8 output on Windows
#endif

    if (argc >= 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        printf("ra3mes - MES text file converter for Pokémon Ranger: Guardian "
               "Signs\n");
        printf("Copyright (c) 2026 SombrAbsol\n\n");
        printf("Usage:\n");
        printf("  %s --to-json <in.mes>  [out.json]\n", argv[0]);
        printf("  %s --to-mes  <in.json> [out.mes]\n", argv[0]);
        printf("  %s -h|--help\n", argv[0]);
        return EXIT_SUCCESS;
    }

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Invalid arguments\n");
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (!file_exists(argv[2])) {
        fprintf(stderr, "Invalid path: '%s'\n", argv[2]);
        return EXIT_FAILURE;
    }

    char *output = NULL;
    bool ok = false;

    const char *input = argv[2];
    const char *outarg = (argc == 4) ? argv[3] : NULL;

    if (strcmp(argv[1], "--to-json") == 0) {
        output = outarg ? xstrdup(outarg) : make_output_path(input, ".json");

        if (!output) {
            return EXIT_FAILURE;
        }

        ok = mes_to_json(input, output);

    } else if (strcmp(argv[1], "--to-mes") == 0) {
        output = outarg ? xstrdup(outarg) : make_output_path(input, ".mes");

        if (!output) {
            return EXIT_FAILURE;
        }

        ok = json_to_mes(input, output);

    } else {
        fprintf(stderr, "Unknown option: '%s'\n", argv[1]);
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
    }

    free(output);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
