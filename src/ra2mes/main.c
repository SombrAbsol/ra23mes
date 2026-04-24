// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright 2026 SombrAbsol
/*
 * Text converter for Pokémon Ranger: Shadows of Almia.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "lz10.h"
#include "utils.h"

/*
 * Build a MES-format binary buffer from a JSON file.
 */
static unsigned char *build_mes_buffer(const char *input, size_t *outSize) {
    uint32_t count;
    char **strings = read_json_strings(input, &count);
    if (!strings)
        return NULL;

    // initial buffer capacity, grows dynamically
    size_t cap = 1024;
    size_t size = 0;

    unsigned char *mem = malloc(cap);
    if (!mem)
        goto error;

// append data to the buffer, growing it if necessary
#define EMIT(ptr, len)                                                         \
    do {                                                                       \
        if (size + (len) > cap) {                                              \
            cap = (cap + (len)) * 2;                                           \
            unsigned char *tmp = realloc(mem, cap);                            \
            if (!tmp)                                                          \
                goto error;                                                    \
            mem = tmp;                                                         \
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

    uint32_t total = 8;

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
        static const unsigned char zero_pad[4] = {0};
        uint32_t pad = pad4(len);
        if (pad)
            EMIT(zero_pad, pad);

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
static int is_valid_mes(const uint8_t *buf, size_t size) {
    if (size < 8) // minimum header size
        return 0;

    uint32_t total = read_u32_le(buf);     // total file size
    uint32_t count = read_u32_le(buf + 4); // number of strings

    // stored total size must match actual buffer size
    if (total != size)
        return 0;

    uint32_t off = 8;

    // iterate over all string blocks and read them
    for (uint32_t i = 0; i < count; i++) {
        if (off + 4 > size)
            return 0;

        uint32_t blk = read_u32_le(buf + off);
        off += 4;

        if (blk == 0 || off + blk > size)
            return 0;

        off += blk;
    }

    // final offset must match file size
    return (off == size);
}

/*
 * Convert a (raw or LZ10-compressed) Pokémon Ranger 2 MES file into JSON.
 */
static int mes_to_json(const char *input, const char *output) {
    size_t size;
    unsigned char *buf = NULL;
    unsigned char *work = NULL;

    buf = read_file(input, &size);
    if (!buf)
        goto error;

    work = buf;
    size_t workSize = size;

    // attempt LZ10 decompression
    if (looks_like_lz10(buf, size)) {
        size_t decSize;
        unsigned char *dec = lz10_decompress(buf, size, &decSize);

        if (dec && is_valid_mes(dec, decSize)) {
            work = dec;
            workSize = decSize;
            free(buf);
        } else {
            free(dec);
        }

        // if decompression fails, continue with raw buffer
        if (!is_valid_mes(work, workSize))
            goto error;
    }

    if (workSize < 8)
        goto error;

    // validate header
    if (read_u32_le(work) != workSize)
        goto error;
    uint32_t count = read_u32_le(work + 4);

    char **strings = calloc(count, sizeof(char *));
    if (!strings)
        goto error;

    uint32_t off = 8;

    // read each string block
    for (uint32_t i = 0; i < count; i++) {
        if (off + 4 > workSize)
            goto error_strings;

        uint32_t blk = read_u32_le(work + off);
        off += 4;

        if (off + blk > workSize)
            goto error_strings;

        // duplicate null-terminated string
        strings[i] = xstrdup((char *)(work + off));
        if (!strings[i])
            goto error_strings;

        off += blk;
    }

    int res = write_json_strings(output, strings, count);

    free_string_array(strings, count);
    free(work);
    return res;

error_strings:
    free_string_array(strings, count);
error:
    free(work);
    return EXIT_FAILURE;
}

/*
 * Convert a JSON file into Pokémon Ranger 2 MES format.
 */
static int json_to_mes(const char *input, const char *output) {
    size_t size;
    unsigned char *buf = build_mes_buffer(input, &size);
    if (!buf)
        return EXIT_FAILURE;

    FILE *f = xfopen(output, "wb");
    if (!f) {
        free(buf);
        return EXIT_FAILURE;
    }

    fwrite(buf, 1, size, f);
    fclose(f);
    free(buf);

    return EXIT_SUCCESS;
}

/*
 * Convert a JSON file into Pokémon Ranger 2 MES format and compresses it using
 * LZ10.
 */
static int json_to_meslz(const char *input, const char *output) {
    size_t rawSize;
    unsigned char *raw = build_mes_buffer(input, &rawSize);
    if (!raw)
        return EXIT_FAILURE;

    size_t cmpSize;
    unsigned char *cmp = lz10_compress(raw, rawSize, &cmpSize);
    free(raw);

    if (!cmp)
        return EXIT_FAILURE;

    FILE *f = xfopen(output, "wb");
    if (!f) {
        free(cmp);
        return EXIT_FAILURE;
    }

    fwrite(cmp, 1, cmpSize, f);
    fclose(f);
    free(cmp);

    return EXIT_SUCCESS;
}

/*
 * Command-line interface.
 */
int main(int argc, char **argv) {
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
    int result = EXIT_FAILURE;

    const char *input = argv[2];
    const char *outarg = (argc == 4) ? argv[3] : NULL;

    if (!file_exists(input)) {
        fprintf(stderr, "Invalid path: '%s'\n", input);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "--to-json") == 0) {
        output = outarg ? xstrdup(outarg) : make_output_path(input, ".json");
        if (!output)
            return EXIT_FAILURE;
        result = mes_to_json(input, output);

    } else if (strcmp(argv[1], "--to-mes") == 0) {
        output = outarg ? xstrdup(outarg) : make_output_path(input, ".mes");
        if (!output)
            return EXIT_FAILURE;
        result = json_to_mes(input, output);

    } else if (strcmp(argv[1], "--to-meslz") == 0) {
        output = outarg ? xstrdup(outarg) : make_output_path(input, ".meslz");
        if (!output)
            return EXIT_FAILURE;
        result = json_to_meslz(input, output);

    } else {
        fprintf(stderr, "Unknown option: %s\n", argv[1]);
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
    }

    free(output);
    return result;
}
