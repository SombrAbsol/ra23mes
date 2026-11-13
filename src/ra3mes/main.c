/*
 * MES text file converter for Pokémon Ranger: Guardian Signs.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include "cli.h"
#include "fs.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 8 // total size, then number of strings

/*
 * Read a 32-bit little-endian integer.
 */
static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16
        | (uint32_t)p[3] << 24;
}

/*
 * Write a 32-bit little-endian integer.
 */
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/*
 * Compute the number of padding bytes required to align 'n' to the next
 * 4-byte boundary.
 */
static size_t pad4(size_t n)
{
    return (4 - (n & 3)) & 3;
}

/*
 * Return the number of strings of a MES file, or -1 if it is not one, never
 * compressed for this game.
 */
static int64_t check_mes(uint8_t **data, size_t *size, bool *compressed)
{
    *compressed = false;
    const uint8_t *mes = *data;
    if (*size < HEADER_SIZE || get_u32(mes) != *size) {
        return -1;
    }

    // the offset table must fit within the file
    uint32_t count = get_u32(mes + 4);
    if (count > (*size - HEADER_SIZE) / 4) {
        return -1;
    }
    size_t tableEnd = HEADER_SIZE + (size_t)count * 4;

    // strings follow the table in order, each null-terminated before the next
    for (uint32_t i = 0; i < count; ++i) {
        size_t off = get_u32(mes + HEADER_SIZE + i * 4);
        size_t end
            = i + 1 < count ? get_u32(mes + HEADER_SIZE + i * 4 + 4) : *size;
        if (off < tableEnd || end <= off || end > *size
            || !memchr(mes + off, 0, end - off)) {
            return -1;
        }
    }
    return count;
}

/*
 * Point 'strings' to the strings of a MES file accepted by check_mes.
 */
static void split_mes(const uint8_t *data, const char **strings)
{
    uint32_t count = get_u32(data + 4);
    for (uint32_t i = 0; i < count; ++i) {
        strings[i] = (const char *)data + get_u32(data + HEADER_SIZE + i * 4);
    }
}

/*
 * Build a MES file of 'count' strings.
 */
static uint8_t *build_mes(
    const char *const *strings, uint32_t count, size_t *outSize)
{
    // the offset table, then each string with its null terminator, padded
    size_t size = HEADER_SIZE + (size_t)count * 4;
    for (uint32_t i = 0; i < count; ++i) {
        size_t len = strlen(strings[i]) + 1;
        size += len + pad4(len);
    }
    if (size > UINT32_MAX) {
        fprintf(stderr, "%s: too much text for a MES file\n", programName);
        return NULL;
    }

    uint8_t *mes = calloc(size, 1); // zeroed, padding included
    if (!mes) {
        fprintf(stderr, "%s: memory allocation failed\n", programName);
        return NULL;
    }
    put_u32(mes, (uint32_t)size);
    put_u32(mes + 4, count);

    size_t off = HEADER_SIZE + (size_t)count * 4; // absolute string offset
    for (uint32_t i = 0; i < count; ++i) {
        size_t len = strlen(strings[i]) + 1;
        put_u32(mes + HEADER_SIZE + i * 4, (uint32_t)off);
        memcpy(mes + off, strings[i], len);
        off += len + pad4(len);
    }

    *outSize = size;
    return mes;
}

/*
 * Command-line interface.
 */
int main(int argc, char **argv)
{
    static const MesFormat format = {
        "ra3mes",
        "Pokémon Ranger: Guardian Signs",
        NULL,
        check_mes,
        split_mes,
        build_mes,
    };
    return mes_main(argc, argv, &format);
}
