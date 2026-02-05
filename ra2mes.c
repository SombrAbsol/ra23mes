// Copyright (c) 2026 SombrAbsol

#include "utils.h"
#include <stdio.h>
#include <string.h>

// convert pokémon ranger 2 mes format (sequential blocks) to json
int mes_to_json(const char *input, const char *output) {
    size_t size;
    unsigned char *buf = read_file(input, &size);
    if (!buf || size < 8) goto error;

    // header: [total_size][string_count]
    if (read_u32_le(buf) != size) goto error;
    uint32_t count = read_u32_le(buf + 4);

    char **strings = calloc(count, sizeof(char *));
    if (!strings) goto error;

    uint32_t off = 8;
    for (uint32_t i = 0; i < count; i++) {
        if (off + 4 > size) goto error_strings;

        // block size includes string + padding
        uint32_t blk = read_u32_le(buf + off);
        off += 4;
        if (off + blk > size) goto error_strings;

        strings[i] = strdup((char *)(buf + off));
        if (!strings[i]) goto error_strings;

        off += blk;
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

// convert json to pokémon ranger 2 mes format
int json_to_mes(const char *input, const char *output) {
    uint32_t count;
    char **strings = read_json_strings(input, &count);
    if (!strings) return EXIT_FAILURE;

    FILE *f = fopen(output, "wb");
    if (!f) goto error;

    unsigned char b[4];

    // write placeholder size
    write_u32_le(b, 0);      fwrite(b, 1, 4, f);
    write_u32_le(b, count); fwrite(b, 1, 4, f);

    uint32_t total = 8;

    // write block sizes and strings
    for (uint32_t i = 0; i < count; i++) {
        uint32_t len = (uint32_t)strlen(strings[i]) + 1;
        uint32_t blk = len + pad4(len);

        write_u32_le(b, blk);
        fwrite(b, 1, 4, f);
        fwrite(strings[i], 1, len, f);

        // zero padding to 4-byte alignment
        static const unsigned char zero_pad[4] = {0};
        uint32_t pad = pad4(len);
        if (pad)
            fwrite(zero_pad, 1, pad, f);

        total += 4 + blk;
    }

    // patch total file size
    fseek(f, 0, SEEK_SET);
    write_u32_le(b, total);
    fwrite(b, 1, 4, f);

    fclose(f);
    free_string_array(strings, count);
    return EXIT_SUCCESS;

    error:
    free_string_array(strings, count);
    return EXIT_FAILURE;
}

int main(int argc, char **argv) {
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); // force utf-8 on windows
    #endif

    if (argc >= 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        printf("ra2mes - MES text file converter for Pokémon Ranger: Shadows of Almia\n");
        printf("Copyright (c) 2026 SombrAbsol\n\n");
        printf("Usage:\n");
        printf("  %s --to-json <in.mes>  [out.json]  convert MES to JSON\n", argv[0]);
        printf("  %s --to-mes  <in.json> [out.mes]   convert JSON to MES\n", argv[0]);
        printf("  %s -h|--help                       show this help\n", argv[0]);
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
        output = outarg ? strdup(outarg) : make_output_path(input, ".json");
        if (!output) return EXIT_FAILURE;
        result = mes_to_json(input, output);

    } else if (strcmp(argv[1], "--to-mes") == 0) { // json to mes
        output = outarg ? strdup(outarg) : make_output_path(input, ".mes");
        if (!output) return EXIT_FAILURE;
        result = json_to_mes(input, output);

    } else {
        fprintf(stderr, "Unknown option: %s\n", argv[1]);
        fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
    }

    free(output);
    return result;
}
