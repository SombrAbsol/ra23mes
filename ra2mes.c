// Copyright (c) 2026 SombrAbsol

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

// build mes into memory buffer
static unsigned char *build_mes_buffer(const char *input, size_t *outSize) {
    uint32_t count;
    char **strings = read_json_strings(input, &count);
    if (!strings) return NULL;

    size_t cap = 1024;
    size_t size = 0;
    unsigned char *mem = malloc(cap);
    if (!mem) goto error;

    #define EMIT(ptr, len) \
    do { \
        if (size + (len) > cap) { \
            cap = (cap + (len)) * 2; \
            unsigned char *tmp = realloc(mem, cap); \
            if (!tmp) goto error; \
                mem = tmp; \
        } \
        memcpy(mem + size, (ptr), (len)); \
        size += (len); \
    } while (0)

    unsigned char b[4];

    // write placeholder size
    write_u32_le(b, 0);      EMIT(b, 4);
    write_u32_le(b, count); EMIT(b, 4);

    uint32_t total = 8;

    // write block sizes + strings
    for (uint32_t i = 0; i < count; i++) {
        uint32_t len = (uint32_t)strlen(strings[i]) + 1;
        uint32_t blk = len + pad4(len);

        write_u32_le(b, blk);
        EMIT(b, 4);
        EMIT(strings[i], len);

        // zero padding to 4-byte alignment
        static const unsigned char zero_pad[4] = {0};
        uint32_t pad = pad4(len);
        if (pad)
            EMIT(zero_pad, pad);

        total += 4 + blk;
    }

    // patch total file size
    write_u32_le(mem, total);

    free_string_array(strings, count);
    *outSize = size;
    return mem;

    error:
    free(mem);
    free_string_array(strings, count);
    return NULL;
}

// convert pokémon ranger 2 mes format (sequential blocks) to json
static int mes_to_json(const char *input, const char *output) {
    size_t size;
    unsigned char *buf  = NULL;
    unsigned char *work = NULL;

    buf = read_file(input, &size);
    if (!buf) goto error;

    work = buf;
    size_t workSize = size;

    // try lz10 decomp
    if (looks_like_lz10(buf, size)) {
        size_t decSize;
        unsigned char *dec = lz10_decompress(buf, size, &decSize);

        if (dec) {
            work = dec;
            workSize = decSize;
            free(buf);
        }
        // fall back to raw data if decomp fails
    }

    if (workSize < 8) goto error;

    // header: [total_size][string_count]
    if (read_u32_le(work) != workSize) goto error;
    uint32_t count = read_u32_le(work + 4);

    char **strings = calloc(count, sizeof(char *));
    if (!strings) goto error;

    uint32_t off = 8;
    for (uint32_t i = 0; i < count; i++) {
        if (off + 4 > workSize) goto error_strings;

        // block size includes string + padding
        uint32_t blk = read_u32_le(work + off);
        off += 4;
        if (off + blk > workSize) goto error_strings;

        strings[i] = xstrdup((char *)(work + off));
        if (!strings[i]) goto error_strings;

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

// convert json to pokémon ranger 2 mes format
static int json_to_mes(const char *input, const char *output) {
    size_t size;
    unsigned char *buf = build_mes_buffer(input, &size);
    if (!buf) return EXIT_FAILURE;

    FILE *f = fopen(output, "wb");
    if (!f) {
        free(buf);
        return EXIT_FAILURE;
    }

    fwrite(buf, 1, size, f);
    fclose(f);
    free(buf);
    return EXIT_SUCCESS;
}

// convert json to pokémon ranger 2 mes format + lz10 compression
static int json_to_meslz(const char *input, const char *output) {
    size_t rawSize;
    unsigned char *raw = build_mes_buffer(input, &rawSize);
    if (!raw) return EXIT_FAILURE;

    size_t cmpSize;
    unsigned char *cmp = lz10_compress(raw, rawSize, &cmpSize);
    free(raw);

    if (!cmp) return EXIT_FAILURE;

    FILE *f = fopen(output, "wb");
    if (!f) {
        free(cmp);
        return EXIT_FAILURE;
    }

    fwrite(cmp, 1, cmpSize, f);
    fclose(f);
    free(cmp);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); // force utf-8 on windows
    #endif

    if (argc >= 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        printf("ra2mes - MES text file converter for Pokémon Ranger: Shadows of Almia\n");
        printf("Copyright (c) 2026 SombrAbsol\n\n");
        printf("Usage:\n");
        printf("  %s --to-json  <in.mes|in.meslz> [out.json]   convert MES to JSON\n", argv[0]);
        printf("  %s --to-mes   <in.json>         [out.mes]    convert JSON to MES\n", argv[0]);
        printf("  %s --to-meslz <in.json>         [out.meslz]  convert JSON to LZ10-compressed MES\n", argv[0]);
        printf("  %s -h|--help                                 show this help\n", argv[0]);
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

    const char *input  = argv[2];
    const char *outarg = (argc == 4) ? argv[3] : NULL;

    if (!file_exists(input)) {
        fprintf(stderr, "Invalid path: '%s'\n", input);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "--to-json") == 0) { // mes to json
        output = outarg ? xstrdup(outarg) : make_output_path(input, ".json");
        if (!output) return EXIT_FAILURE;
        result = mes_to_json(input, output);

    } else if (strcmp(argv[1], "--to-mes") == 0) { // json to mes
        output = outarg ? xstrdup(outarg) : make_output_path(input, ".mes");
        if (!output) return EXIT_FAILURE;
        result = json_to_mes(input, output);

    } else if (strcmp(argv[1], "--to-meslz") == 0) { // json to lz10-compressed mes
        output = outarg ? xstrdup(outarg) : make_output_path(input, ".meslz");
        if (!output) return EXIT_FAILURE;
        result = json_to_meslz(input, output);

    } else {
        fprintf(stderr, "Unknown option: %s\n", argv[1]);
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
    }

    free(output);
    return result;
}
