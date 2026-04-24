// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright 2026 SombrAbsol
/*
 * Text converter for Pokémon Ranger: Guardian Signs.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "utils.h"

/*
 * Convert a Pokémon Ranger 3 MES file into JSON.
 */
static int mes_to_json(const char *input, const char *output) {
    size_t size;
    unsigned char *buf = read_file(input, &size);
    if (!buf || size < 8)
        goto error;

    uint32_t file_size = read_u32_le(buf);
    uint32_t count = read_u32_le(buf + 4);

    /*
     * Validate that the file size matches the header and that the offset table
     * fits within the file.
     */
    if (file_size != size || 8 + count * 4 > size)
        goto error;

    char **strings = calloc(count, sizeof(char *));
    if (!strings)
        goto error;

    // read each string via offset table
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = read_u32_le(buf + 8 + i * 4);

        // ensure offset is within file
        if (off >= size)
            goto error_strings;

        // duplicate null-terminated string
        strings[i] = xstrdup((char *)(buf + off));
        if (!strings[i])
            goto error_strings;
    }

    int res = write_json_strings(output, strings, count);

    free_string_array(strings, count);
    free(buf);
    return res;

error_strings:
    free_string_array(strings, count);
error:
    free(buf);
    return EXIT_FAILURE;
}

/*
 * Convert a JSON file into Pokémon Ranger 3 MES format.
 */
static int json_to_mes(const char *input, const char *output) {
    uint32_t count;
    char **strings = read_json_strings(input, &count);
    if (!strings)
        return EXIT_FAILURE;

    // allocate offset table
    uint32_t *offsets = malloc(count * sizeof(uint32_t));
    if (!offsets)
        goto error;

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
    if (!f)
        goto error;

    unsigned char b[4];

    // write header
    write_u32_le(b, cur);
    fwrite(b, 1, 4, f); // total file size
    write_u32_le(b, count);
    fwrite(b, 1, 4, f); // string count

    // write offset table
    for (uint32_t i = 0; i < count; i++) {
        write_u32_le(b, offsets[i]);
        fwrite(b, 1, 4, f);
    }

    // write string data
    for (uint32_t i = 0; i < count; i++) {
        uint32_t len = (uint32_t)strlen(strings[i]) + 1;

        // write null-terminated string
        fwrite(strings[i], 1, len, f);

        // pad to 4-byte boundary
        static const unsigned char zero_pad[4] = {0};
        uint32_t pad = pad4(len);
        if (pad)
            fwrite(zero_pad, 1, pad, f);
    }

    fclose(f);
    free(offsets);
    free_string_array(strings, count);
    return EXIT_SUCCESS;

error:
    free(offsets);
    free_string_array(strings, count);
    return EXIT_FAILURE;
}

/*
 * Command-line interface.
 */
int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); // ensure UTF-8 output on Windows
#endif

    // help
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

    } else {
        fprintf(stderr, "Unknown option: %s\n", argv[1]);
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
    }

    free(output);
    return result;
}
