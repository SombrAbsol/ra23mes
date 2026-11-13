/*
 * Flat JSON object reader and writer.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef JSON_H
#define JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Check whether a buffer of 'size' bytes holds a JSON object: text without
 * null bytes, '{' first after an optional BOM and whitespace.
 */
bool json_detect(const char *text, size_t size);

/*
 * Parse the string values of a flat JSON object, unescaped in place, into
 * '*outValues', to be freed. Return NULL, or the error and its '*errorLine'.
 */
const char *json_read(
    char *text, const char ***outValues, uint32_t *outCount, int *errorLine);

/*
 * Write strings as a flat JSON object, one entry per line, keyed by their
 * index padded to 3 digits at least. Return NULL if memory runs out.
 */
char *json_write(const char *const *values, uint32_t count, size_t *outSize);

#endif /* JSON_H */
