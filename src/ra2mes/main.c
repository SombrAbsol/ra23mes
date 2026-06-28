/*
 * Text converter for Pokémon Ranger: Shadows of Almia.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include "lz10.h"
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
 * Build a MES-format binary buffer from a JSON file.
 */
static unsigned char *build_mes_buffer(const char *input, size_t *outSize)
{
    uint32_t count;
    char **strings = read_json_strings(input, &count);
    if (!strings) {
        fprintf(stderr, "build_mes_buffer: failed to parse JSON '%s'\n", input);
        return NULL;
    }

    // initial buffer capacity, grows dynamically
    size_t cap = 1024;
    size_t size = 0;

    unsigned char *mem = malloc(cap);
    if (!mem) {
        fprintf(stderr, "build_mes_buffer: memory allocation failed\n");
        goto error;
    }

// append data to the buffer, growing it if necessary
#define EMIT(ptr, len)                                                         \
    do {                                                                       \
        if (size + len > cap) {                                                \
            size_t newcap = cap * 2;                                           \
            if (newcap < size + len)                                           \
                newcap = size + len;                                           \
            unsigned char *tmp = realloc(mem, newcap);                         \
            if (!tmp) {                                                        \
                fprintf(stderr, "build_mes_buffer: buffer growth failed\n");   \
                goto error;                                                    \
            }                                                                  \
            mem = tmp;                                                         \
            cap = newcap;                                                      \
        }                                                                      \
        memcpy(mem + size, (ptr), (len));                                      \
        size += (len);                                                         \
    } while (0)

    unsigned char b[4];

    // placeholder for total file size, patched later
    write_u32_le(b, 0);
    EMIT(b, 4);

    // number of strings
    write_u32_le(b, count);
    EMIT(b, 4);

    size_t total = 8;
    if (total != size) {
        goto error;
    }

    // write each string block: [block size][string][padding]
    for (uint32_t i = 0; i < count; i++) {

        // include null terminator
        uint32_t len = (uint32_t)strlen(strings[i]) + 1;

        // padded block size
        uint32_t blk = len + pad4(len);

        // block size
        write_u32_le(b, blk);
        EMIT(b, 4);

        // string data
        EMIT(strings[i], len);

        // zero padding for 4-byte alignment
        static const unsigned char zero_pad[4] = { 0 };
        uint32_t pad = pad4(len);
        if (pad) {
            EMIT(zero_pad, pad);
        }

        total += 4 + blk;
    }

    // patch total file size at start of buffer
    write_u32_le(mem, total);

    free_string_array(strings, count);
    *outSize = size;
    return mem;

error:
    free(mem);
    free_string_array(strings, count);
    return NULL;
}

/*
 * Validate whether a buffer matches the expected Pokémon Ranger 2 MES file
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

    uint32_t off = 8;

    // iterate over all string blocks and read them
    for (uint32_t i = 0; i < count; i++) {
        if ((uint64_t)off + 4 > size) {
            return false;
        }

        uint32_t blk = read_u32_le(buf + off);
        off += 4;

        if (blk == 0 || (uint64_t)off + blk > size) {
            return false;
        }

        off += blk;
    }

    // final offset must match file size
    return off == size;
}

/*
 * Convert a (raw or LZ10-compressed) Pokémon Ranger 2 MES file into JSON.
 */
static bool mes_to_json(const char *input, const char *output)
{
    size_t size;
    unsigned char *buf = NULL;
    unsigned char *work = NULL;

    buf = read_file(input, &size);
    if (!buf) {
        fprintf(stderr, "mes_to_json: failed to read '%s'\n", input);
        goto error;
    }

    work = buf;
    size_t workSize = size;
    unsigned char *dec = NULL;

    // attempt LZ10 decompression
    if (!is_valid_mes(buf, size) && looks_like_lz10(buf, size)) {
        size_t decSize;
        dec = lz10_decompress(buf, size, &decSize);

        if (dec && is_valid_mes(dec, decSize)) {
            free(buf);
            work = dec;
            workSize = decSize;
        } else {
            free(dec);
        }
    }

    if (!is_valid_mes(work, workSize)) {
        fprintf(stderr, "mes_to_json: invalid MES file '%s'\n", input);
        goto error;
    }

    uint32_t count = read_u32_le(work + 4);

    char **strings = calloc(count, sizeof(char *));
    if (!strings) {
        fprintf(stderr, "mes_to_json: memory allocation failed\n");
        goto error;
    }

    uint32_t off = 8;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t blk = read_u32_le(work + off);
        off += 4;

        strings[i] = xstrdup((char *)(work + off));
        if (!strings[i]) {
            fprintf(stderr,
                "mes_to_json: memory allocation failed for string at index "
                "%u\n",
                i);
            goto error_strings;
        }

        off += blk;
    }

    bool ok = write_json_strings(output, strings, count);

    free_string_array(strings, count);
    free(work);
    return ok;

error_strings:
    free_string_array(strings, count);
error:
    free(work);
    return false;
}

/*
 * Convert a JSON file into Pokémon Ranger 2 MES format.
 */
static bool json_to_mes(const char *input, const char *output)
{
    size_t size;
    unsigned char *buf = build_mes_buffer(input, &size);
    if (!buf) {
        fprintf(stderr, "json_to_mes: failed to build MES from '%s'\n", input);
        return false;
    }

    FILE *f = xfopen(output, "wb");
    if (!f) {
        fprintf(stderr, "json_to_mes: cannot open '%s'\n", output);
        free(buf);
        return false;
    }

    if (fwrite(buf, 1, size, f) != size) {
        fprintf(stderr, "json_to_mes: write failed\n");
        fclose(f);
        free(buf);
        return false;
    }

    fclose(f);
    free(buf);
    return true;
}

/*
 * Convert a JSON file into Pokémon Ranger 2 MES format and compresses it using
 * LZ10.
 */
static bool json_to_meslz(const char *input, const char *output)
{
    size_t rawSize;
    unsigned char *raw = build_mes_buffer(input, &rawSize);
    if (!raw) {
        fprintf(
            stderr, "json_to_meslz: failed to build MESLZ from '%s'\n", input);
        return false;
    }

    size_t cmpSize;
    unsigned char *cmp = lz10_compress(raw, rawSize, &cmpSize);
    free(raw);

    if (!cmp) {
        fprintf(stderr, "json_to_meslz: compression failed\n");
        return false;
    }

    FILE *f = xfopen(output, "wb");
    if (!f) {
        fprintf(stderr, "json_to_meslz: cannot open '%s'\n", output);
        free(cmp);
        return false;
    }

    if (fwrite(cmp, 1, cmpSize, f) != cmpSize) {
        fprintf(stderr, "json_to_meslz: write failed\n");
        fclose(f);
        free(cmp);
        return false;
    }

    fclose(f);
    free(cmp);
    return true;
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
        printf("ra2mes - MES text file converter for Pokémon Ranger: Shadows "
               "of Almia\n");
        printf("Copyright (c) 2026 SombrAbsol\n\n");
        printf("Usage:\n");
        printf("  %s --to-json  <in.mes|in.meslz> [out.json]\n", argv[0]);
        printf("  %s --to-mes   <in.json>         [out.mes]\n", argv[0]);
        printf("  %s --to-meslz <in.json>         [out.meslz]\n", argv[0]);
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

    } else if (strcmp(argv[1], "--to-meslz") == 0) {
        output = outarg ? xstrdup(outarg) : make_output_path(input, ".meslz");
        if (!output) {
            return EXIT_FAILURE;
        }
        ok = json_to_meslz(input, output);

    } else {
        fprintf(stderr, "Unknown option: '%s'\n", argv[1]);
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
    }

    free(output);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
