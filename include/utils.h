/*
 * Utility functions.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stdio.h>

/*
 * Cross-platform wrapper around fopen.
 * On Windows, uses fopen_s for parameter handling.
 */
FILE *xfopen(const char *path, const char *mode);

/*
 * Read and write a 32-bit unsigned integer from a byte buffer in little-endian
 * order.
 */
uint32_t read_u32_le(const unsigned char *b);
void write_u32_le(unsigned char *b, uint32_t v);

/*
 * Compute the number of padding bytes required to align 'n' to the next 4-byte
 * boundary.
 */
uint32_t pad4(uint32_t n);

/*
 * strdup replacement using malloc.
 */
char *xstrdup(const char *s);

/*
 * Check whether a file exists at the given path.
 */
int file_exists(const char *path);

/*
 * Read an entire file into memory.
 */
unsigned char *read_file(const char *path, size_t *out_size);

/*
 * Build a new file path by replacing or appending an extension.
 */
char *make_output_path(const char *input, const char *ext);

/*
 * Extract all string values from a flat JSON object.
 */
char **json_parse_strings(const char *json, uint32_t *out_count);

/*
 * Read a JSON file and extracts all string values.
 */
char **read_json_strings(const char *path, uint32_t *out_count);

/*
 * Write an array of strings into a flat JSON object
 */
int write_json_strings(const char *output, char *const *strings,
                       uint32_t count);

/*
 * (Un)escape a set of characters for JSON output.
 */
char *escape_json_string(const char *s, size_t maxlen);
char *unescape_json_string(const char *start, size_t len);

/*
 * Free an array of strings.
 */
void free_string_array(char **strings, uint32_t count);

#endif /* UTILS_H */
