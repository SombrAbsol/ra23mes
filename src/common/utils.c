/*
 * Utility functions.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "utils.h"

/*
 * Cross-platform wrapper around fopen. Use fopen_s on Windows, or fopen
 * otherwise.
 */
FILE *xfopen(const char *path, const char *mode) {
#ifdef _WIN32
    FILE *f = NULL;
    return (fopen_s(&f, path, mode) == 0) ? f : NULL;
#else
    return fopen(path, mode);
#endif
}

/*
 * Read a 32-bit unsigned integer from a byte buffer in little-endian order.
 */
uint32_t read_u32_le(const unsigned char *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

/*
 * Write a 32-bit unsigned integer into a byte buffer in little-endian order.
 */
void write_u32_le(unsigned char *b, uint32_t v) {
    b[0] = (unsigned char)(v);
    b[1] = (unsigned char)(v >> 8);
    b[2] = (unsigned char)(v >> 16);
    b[3] = (unsigned char)(v >> 24);
}

/*
 * Compute the number of padding bytes required to align 'n' to the next 4-byte
 * boundary.
 */
uint32_t pad4(uint32_t n) {
    return (4 - (n & 3)) & 3;
}

/*
 * strdup replacement using malloc.
 */
char *xstrdup(const char *s) {
    if (!s)
        return NULL;

    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p)
        memcpy(p, s, len);

    return p;
}

/*
 * Check whether a file exists at the given path.
 */
int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/*
 * Read an entire file into memory.
 */
unsigned char *read_file(const char *path, size_t *out_size) {
    FILE *f = NULL;
    unsigned char *buf = NULL;

    f = xfopen(path, "rb");
    if (!f)
        return NULL;

    // determine file size
    if (fseek(f, 0, SEEK_END) != 0)
        goto error;
    long sz = ftell(f);
    if (sz < 0)
        goto error;
    rewind(f);

    buf = malloc((size_t)sz);
    if (!buf)
        goto error;

    // read entire file
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz)
        goto error;

    fclose(f);

    if (out_size)
        *out_size = (size_t)sz;
    return buf;

error:
    if (f)
        fclose(f);
    free(buf);
    return NULL;
}

/*
 * Build a new file path by replacing or appending an extension.
 */
char *make_output_path(const char *input, const char *ext) {
    const char *slash = strrchr(input, '/');

#ifdef _WIN32
    const char *bslash = strrchr(input, '\\');
    if (!slash || (bslash && bslash > slash))
        slash = bslash;
#endif

    const char *base = slash ? slash + 1 : input;
    const char *dot = strrchr(base, '.');

    size_t dir_len = (size_t)(base - input);
    size_t name_len = dot ? (size_t)(dot - base) : strlen(base);
    size_t ext_len = strlen(ext);

    char *out = malloc(dir_len + name_len + ext_len + 1);
    if (!out)
        return NULL;

    memcpy(out, input, dir_len);
    memcpy(out + dir_len, base, name_len);
    memcpy(out + dir_len + name_len, ext, ext_len + 1);

    return out;
}

/*
 * Extract all string values from a flat JSON object.
 */
char **json_parse_strings(const char *json, uint32_t *out_count) {
    const char *p = json;
    uint32_t capacity = 16;
    uint32_t count = 0;

    char **strings = malloc(capacity * sizeof(char *));
    if (!strings)
        return NULL;

    while (*p) {
        while (isspace((unsigned char)*p))
            p++;

        // find key opening quote
        if (*p != '"') {
            p++;
            continue;
        }

        // skip key
        p++;
        int escaped = 0;
        while (*p) {
            if (escaped) {
                escaped = 0;
            } else if (*p == '\\') {
                escaped = 1;
            } else if (*p == '"') {
                break;
            }
            p++;
        }
        if (!*p)
            break;
        p++;

        while (isspace((unsigned char)*p))
            p++;
        if (*p != ':')
            continue;
        p++;

        while (isspace((unsigned char)*p))
            p++;
        if (*p != '"')
            continue;
        p++;

        const char *start = p;

        // read value string
        escaped = 0;
        while (*p) {
            if (escaped) {
                escaped = 0;
            } else if (*p == '\\') {
                escaped = 1;
            } else if (*p == '"') {
                break;
            }
            p++;
        }
        if (!*p)
            break;

        size_t len = (size_t)(p - start);

        char *val = unescape_json_string(start, len);
        if (!val)
            goto error;

        // grow array if needed
        if (count == capacity) {
            capacity *= 2;
            char **tmp = realloc(strings, capacity * sizeof(char *));
            if (!tmp) {
                free(val);
                goto error;
            }
            strings = tmp;
        }

        strings[count++] = val;
        p++;
    }

    if (out_count)
        *out_count = count;
    return strings;

error:
    for (uint32_t i = 0; i < count; i++)
        free(strings[i]);
    free(strings);
    return NULL;
}

/*
 * Read a JSON file and extracts all string values.
 */
char **read_json_strings(const char *path, uint32_t *out_count) {
    size_t size;
    unsigned char *buf = read_file(path, &size);
    if (!buf)
        return NULL;

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

/*
 * Write an array of strings into a flat JSON object.
 */
int write_json_strings(const char *output, char *const *strings,
                       uint32_t count) {
    FILE *f = xfopen(output, "wb");
    if (!f)
        return EXIT_FAILURE;

    fputs("{\n", f);

    // compute required zero-padding width
    uint32_t max_index = (count > 0) ? (count - 1) : 0;
    unsigned int width = 1;
    while (max_index >= 10) {
        max_index /= 10;
        width++;
    }

    /*
     * MES files in vanilla games never contain more than 999 strings, so we
     * clamp the padding to a minimum of 3 digits for consistency.
     */
    if (width < 3)
        width = 3;

    for (uint32_t i = 0; i < count; i++) {
        char *esc = escape_json_string(strings[i], strlen(strings[i]));
        if (!esc) {
            fclose(f);
            return EXIT_FAILURE;
        }

        fprintf(f, "  \"%0*u\": \"%s\"%s\n", width, i, esc,
                (i + 1 < count) ? "," : "");

        free(esc);
    }

    fputs("}\n", f);
    fclose(f);

    return EXIT_SUCCESS;
}

/*
 * Escape a set of characters for JSON output.
 */
char *escape_json_string(const char *s, size_t len) {
    char *esc = malloc(len * 2 + 1); // worst case
    if (!esc)
        return NULL;

    char *d = esc;

    for (size_t i = 0; i < len && s[i]; i++) {
        unsigned char c = (unsigned char)s[i];

        if (c == '"' || c == '\\') {
            *d++ = '\\';
            *d++ = c;
        } else if (c == '\n') {
            *d++ = '\\';
            *d++ = 'n';
        } else if (c == '\r') {
            *d++ = '\\';
            *d++ = 'r';
        } else {
            *d++ = c;
        }
    }

    *d = '\0';
    return esc;
}

/*
 * Reverse escape_json_string for supported sequences.
 */
char *unescape_json_string(const char *start, size_t len) {
    char *out = malloc(len + 1);
    if (!out)
        return NULL;

    char *d = out;

    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            i++;
            if (start[i] == 'n')
                *d++ = '\n';
            else if (start[i] == 'r')
                *d++ = '\r';
            else
                *d++ = start[i];
        } else {
            *d++ = start[i];
        }
    }

    *d = '\0';
    return out;
}

/*
 * Free an array of strings.
 */
void free_string_array(char **strings, uint32_t count) {
    if (!strings)
        return;

    for (uint32_t i = 0; i < count; i++)
        free(strings[i]);

    free(strings);
}
