/*
 * Flat JSON object reader and writer.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include "json.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UTF8_BOM "\xEF\xBB\xBF"

/*
 * Read position in the parsed text, with its line for error messages.
 */
typedef struct {
    char *p;
    int line;
} Parser;

/*
 * Advance the parser past any JSON whitespace, counting the lines.
 */
static void skip_ws(Parser *ps)
{
    for (;; ++ps->p) {
        if (*ps->p == '\n') {
            ++ps->line;
        } else if (*ps->p != ' ' && *ps->p != '\t' && *ps->p != '\r') {
            return;
        }
    }
}

/*
 * Parse the 4 hexadecimal digits of a \u escape.
 */
static bool parse_hex4(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= (uint32_t)(c - 'A' + 10);
        } else {
            return false; // also stops at the null terminator
        }
    }
    *out = v;
    return true;
}

/*
 * Encode a code point as UTF-8. Return the number of bytes written.
 */
static size_t utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | cp >> 12);
        out[1] = (char)(0x80 | (cp >> 6 & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | cp >> 18);
    out[1] = (char)(0x80 | (cp >> 12 & 0x3F));
    out[2] = (char)(0x80 | (cp >> 6 & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/*
 * Parse a string literal, unescaped in place, as no escape is shorter than the
 * bytes it stands for. Return NULL, or the reason of the failure.
 */
static const char *parse_string(Parser *ps, char **out)
{
    if (*ps->p != '"') {
        return "expected a string";
    }

    char *s = ps->p + 1; // read position
    char *d = s; // write position, never ahead of the read one
    *out = d;
    while (*s != '"') {
        if (*s == '\0') {
            return "unterminated string";
        }
        if ((unsigned char)*s < 0x20) {
            return "control character in string";
        }
        if (*s != '\\') {
            *d++ = *s++;
            continue;
        }

        // escape sequence
        ++s;
        switch (*s++) {
        case '"':
            *d++ = '"';
            break;
        case '\\':
            *d++ = '\\';
            break;
        case '/':
            *d++ = '/';
            break;
        case 'b':
            *d++ = '\b';
            break;
        case 'f':
            *d++ = '\f';
            break;
        case 'n':
            *d++ = '\n';
            break;
        case 'r':
            *d++ = '\r';
            break;
        case 't':
            *d++ = '\t';
            break;
        case 'u': {
            uint32_t cp;
            uint32_t low;
            if (!parse_hex4(s, &cp)) {
                return "invalid \\u escape";
            }
            s += 4;

            // a code point past U+FFFF is written as a surrogate pair
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                if (s[0] != '\\' || s[1] != 'u' || !parse_hex4(s + 2, &low)
                    || low < 0xDC00 || low > 0xDFFF) {
                    return "unpaired surrogate in \\u escape";
                }
                s += 6;
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                return "unpaired surrogate in \\u escape";
            } else if (cp == 0) {
                return "null character in string"; // it would end the string
            }
            d += utf8_encode(cp, d);
            break;
        }
        default:
            return "invalid escape sequence";
        }
    }

    *d = '\0';
    ps->p = s + 1;
    return NULL;
}

/*
 * Check whether a buffer of 'size' bytes holds a JSON object: text without
 * null bytes, '{' first after an optional BOM and whitespace.
 */
bool json_detect(const char *text, size_t size)
{
    if (strlen(text) != size) {
        return false; // binary data, holding null bytes
    }
    if (!strncmp(text, UTF8_BOM, 3)) {
        text += 3;
    }
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        ++text;
    }
    return *text == '{';
}

/*
 * Parse the string values of a flat JSON object, unescaped in place, into
 * '*outValues', to be freed. Return NULL, or the error and its '*errorLine'.
 */
const char *json_read(
    char *text, const char ***outValues, uint32_t *outCount, int *errorLine)
{
    Parser ps = { text, 1 };
    const char **values = NULL;
    uint32_t capacity = 0; // array capacity; doubled on overflow
    uint32_t count = 0;
    const char *err = NULL;

    if (!strncmp(ps.p, UTF8_BOM, 3)) {
        ps.p += 3;
    }
    skip_ws(&ps);
    if (*ps.p != '{') {
        err = "expected '{' at start of object";
        goto error;
    }
    ++ps.p;

    for (;;) {
        skip_ws(&ps);
        if (*ps.p == '}') {
            ++ps.p;
            break;
        }

        // the key, only checked
        char *key;
        if ((err = parse_string(&ps, &key)) != NULL) {
            goto error;
        }
        skip_ws(&ps);
        if (*ps.p != ':') {
            err = "expected ':' after key";
            goto error;
        }
        ++ps.p;
        skip_ws(&ps);

        char *value;
        if ((err = parse_string(&ps, &value)) != NULL) {
            goto error;
        }
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 256;
            const char **tmp = realloc(values, capacity * sizeof(*values));
            if (!tmp) {
                err = "memory allocation failed";
                goto error;
            }
            values = tmp;
        }
        values[count++] = value;

        skip_ws(&ps);
        if (*ps.p == ',') {
            ++ps.p;
            continue;
        }
        if (*ps.p == '}') {
            ++ps.p;
            break;
        }
        err = "expected ',' or '}' after value";
        goto error;
    }

    skip_ws(&ps);
    if (*ps.p != '\0') { // trailing garbage after the closing brace
        err = "trailing data after '}'";
        goto error;
    }

    *outValues = values;
    *outCount = count;
    return NULL;

error:
    free(values);
    *errorLine = ps.line;
    return err;
}

/*
 * Write strings as a flat JSON object, one entry per line, keyed by their
 * index padded to 3 digits at least. Return NULL if memory runs out.
 */
char *json_write(const char *const *values, uint32_t count, size_t *outSize)
{
    // key width: the digits of the last index, 3 at least
    int width = 1;
    for (uint32_t last = count ? count - 1 : 0; last >= 10; last /= 10) {
        ++width;
    }
    width = width < 3 ? 3 : width;

    /*
     * Worst case: braces and terminator, plus per entry its indent, key,
     * quotes, separators and newline, and its value escaped as \u00XX.
     */
    size_t capacity = 5;
    for (uint32_t i = 0; i < count; ++i) {
        capacity += 10 + (size_t)width + strlen(values[i]) * 6;
    }
    char *out = malloc(capacity);
    if (!out) {
        return NULL;
    }

    char *d = out;
    *d++ = '{';
    *d++ = '\n';
    for (uint32_t i = 0; i < count; ++i) {
        d += snprintf(
            d, capacity - (size_t)(d - out), "  \"%0*u\": \"", width, i);
        for (const char *s = values[i]; *s; ++s) {
            unsigned char c = (unsigned char)*s;
            const char *named = c == '"' ? "\\\""
                : c == '\\'              ? "\\\\"
                : c == '\b'              ? "\\b"
                : c == '\f'              ? "\\f"
                : c == '\n'              ? "\\n"
                : c == '\r'              ? "\\r"
                : c == '\t'              ? "\\t"
                                         : NULL;
            if (named) {
                *d++ = named[0];
                *d++ = named[1];
            } else if (c < 0x20) { // other control characters
                d += snprintf(d, 7, "\\u%04x", c);
            } else {
                *d++ = (char)c;
            }
        }
        *d++ = '"';
        if (i + 1 < count) { // omit trailing comma on last entry
            *d++ = ',';
        }
        *d++ = '\n';
    }
    *d++ = '}';
    *d++ = '\n';
    *d = '\0';

    *outSize = (size_t)(d - out);
    return out;
}
