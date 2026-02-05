// Copyright (c) 2026 SombrAbsol

#include "utils.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// read a 32-bit value in little-endian byte order
uint32_t read_u32_le(const unsigned char *b) {
    return (uint32_t)b[0]
    | ((uint32_t)b[1] << 8)
    | ((uint32_t)b[2] << 16)
    | ((uint32_t)b[3] << 24);
}

// write a 32-bit value in little-endian byte order
void write_u32_le(unsigned char *b, uint32_t v) {
    b[0] = (unsigned char)(v);
    b[1] = (unsigned char)(v >> 8);
    b[2] = (unsigned char)(v >> 16);
    b[3] = (unsigned char)(v >> 24);
}

// padding to 4-byte boundary
uint32_t pad4(uint32_t n) {
    return (4 - (n & 3)) & 3;
}

// check if input file exists
int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// strdup implementation
char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

// read entire file into memory
unsigned char *read_file(const char *path, size_t *out_size) {
    FILE *f = NULL;
    unsigned char *buf = NULL;

    f = fopen(path, "rb");
    if (!f) return NULL;

    // get file size
    if (fseek(f, 0, SEEK_END) != 0) goto error;
    long sz = ftell(f);
    if (sz < 0) goto error;
    rewind(f);

    buf = malloc((size_t)sz);
    if (!buf) goto error;

    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) goto error;

    fclose(f);
    if (out_size) *out_size = (size_t)sz;
    return buf;

    error:
    if (f) fclose(f);
    free(buf);
    return NULL;
}

// build output path from input filename
char *make_output_path(const char *input, const char *ext) {
    const char *slash = strrchr(input, '/');
    #ifdef _WIN32
    const char *bslash = strrchr(input, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
    #endif

    const char *base = slash ? slash + 1 : input;
    const char *dot  = strrchr(base, '.');

    size_t dir_len  = (size_t)(base - input);
    size_t name_len = dot ? (size_t)(dot - base) : strlen(base);
    size_t ext_len  = strlen(ext);

    char *out = malloc(dir_len + name_len + ext_len + 1);
    if (!out) return NULL;

    memcpy(out, input, dir_len);
    memcpy(out + dir_len, base, name_len);
    memcpy(out + dir_len + name_len, ext, ext_len + 1);
    return out;
}

// load json file and get string values
char **read_json_strings(const char *path, uint32_t *out_count) {
    size_t size;
    unsigned char *buf = read_file(path, &size);
    if (!buf) return NULL;

    char *json = malloc(size + 1);
    if (!json) {
        free(buf);
        return NULL;
    }

    memcpy(json, buf, size);
    json[size] = '\0';
    free(buf);

    char **strings = json_parse_strings(json, out_count);
    free(json);
    return strings;
}

// write strings as flat json object
int write_json_strings(const char *output, char *const *strings, uint32_t count) {
    FILE *f = fopen(output, "w");
    if (!f) return EXIT_FAILURE;

    fputs("{\n", f);

    for (uint32_t i = 0; i < count; i++) {
        char *esc = escape_json_string(strings[i], strlen(strings[i]));
        if (!esc) {
            fclose(f);
            return EXIT_FAILURE;
        }

        fprintf(f,
                "  \"%03u\": \"%s\"%s\n",
                i, esc, (i + 1 < count) ? "," : "");

        free(esc);
    }

    fputs("}\n", f);
    fclose(f);
    return EXIT_SUCCESS;
}

// escape minimal set of characters
char *escape_json_string(const char *s, size_t maxlen) {
    // worst case, every character needs escaping
    char *esc = malloc(maxlen * 2 + 1);
    if (!esc) return NULL;

    char *d = esc;
    for (size_t i = 0; i < maxlen && s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            *d++ = '\\';
            *d++ = c;
        } else if (c == '\n') { // jp ra2 only
            *d++ = '\\';
            *d++ = 'n';
        } else if (c == '\r') { // 0258.mes in jp ra3 only
            *d++ = '\\';
            *d++ = 'r';
        } else {
            *d++ = c;
        }
    }
    *d = '\0';
    return esc;
}

// undo escaping
char *unescape_json_string(const char *start, size_t len) {
    char *out = malloc(len + 1);
    if (!out) return NULL;

    char *d = out;
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            i++;
            if (start[i] == 'n') *d++ = '\n';
            else if (start[i] == 'r') *d++ = '\r';
            else *d++ = start[i];
        } else {
            *d++ = start[i];
        }
    }
    *d = '\0';
    return out;
}

// count json entries from key/values separators
uint32_t json_count_entries(const char *json) {
    uint32_t count = 0;
    const char *p = json;
    while ((p = strstr(p, "\":"))) {
        count++;
        p += 2;
    }
    return count;
}

// extract all string values from a flat json object
char **json_parse_strings(const char *json, uint32_t *out_count) {
    uint32_t count = json_count_entries(json);
    if (out_count) *out_count = count;


    char **strings = calloc(count, sizeof(char *));
    if (!strings) return NULL;


    const char *p = json;
    uint32_t idx = 0;
    while ((p = strstr(p, "\":")) && idx < count) {
        p += 2;
        while (isspace((unsigned char)*p)) p++;
        if (*p++ != '"') continue;

        // find closing quote and ignore escaped ones
        const char *start = p;
        while (*p && !(*p == '"' && p[-1] != '\\')) p++;
        size_t len = (size_t)(p - start);


        strings[idx] = unescape_json_string(start, len);
        if (!strings[idx]) {
            for (uint32_t i = 0; i < idx; i++) free(strings[i]);
            free(strings);
            return NULL;
        }
        idx++;
        p++;
    }
    return strings;
}

// free string array returned by json helpers
void free_string_array(char **strings, uint32_t count) {
    if (!strings) return;
    for (uint32_t i = 0; i < count; i++)
        free(strings[i]);
    free(strings);
}
