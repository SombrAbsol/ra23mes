/*
 * MES text file converter for Pokémon Ranger: Shadows of Almia.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include "cli.h"
#include "fs.h"
#include "lz10.h"

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
 * Return the number of strings of an uncompressed MES file, or -1 if 'data' is
 * not one.
 */
static int64_t parse_mes(const uint8_t *data, size_t size)
{
    if (size < HEADER_SIZE || get_u32(data) != size) {
        return -1;
    }

    // blocks of a size, then a null-terminated string and its padding
    uint32_t count = get_u32(data + 4);
    size_t off = HEADER_SIZE;
    for (uint32_t i = 0; i < count; ++i) {
        if (size - off < 4) {
            return -1;
        }
        uint32_t block = get_u32(data + off);
        off += 4;
        if (block == 0 || block > size - off || !memchr(data + off, 0, block)) {
            return -1;
        }
        off += block;
    }
    return off == size ? (int64_t)count : -1;
}

/*
 * Return the number of strings of a MES file, or -1 if it is not one. A
 * compressed MES file is replaced in '*data' by its decompressed contents,
 * '*compressed' telling which.
 */
static int64_t check_mes(uint8_t **data, size_t *size, bool *compressed)
{
    int64_t count = parse_mes(*data, *size);
    *compressed = false;
    if (count >= 0) {
        return count;
    }

    // MESLZ files, used by the European release, are LZ10-compressed
    size_t decSize;
    uint8_t *dec = lz10_decompress(*data, *size, &decSize);
    count = dec ? parse_mes(dec, decSize) : -1;
    if (count < 0) {
        free(dec);
        return -1;
    }
    free(*data);
    *data = dec;
    *size = decSize;
    *compressed = true;
    return count;
}

/*
 * Point 'strings' to the strings of a MES file accepted by check_mes.
 */
static void split_mes(const uint8_t *data, const char **strings)
{
    uint32_t count = get_u32(data + 4);
    size_t off = HEADER_SIZE;
    for (uint32_t i = 0; i < count; ++i) {
        strings[i] = (const char *)data + off + 4; // after the block size
        off += 4 + get_u32(data + off);
    }
}

/*
 * Build a MES file of 'count' strings.
 */
static uint8_t *build_mes(
    const char *const *strings, uint32_t count, size_t *outSize)
{
    // each string is stored with its null terminator, padded to 4 bytes
    size_t size = HEADER_SIZE;
    for (uint32_t i = 0; i < count; ++i) {
        size_t len = strlen(strings[i]) + 1;
        size += 4 + len + pad4(len);
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

    uint8_t *p = mes + HEADER_SIZE;
    for (uint32_t i = 0; i < count; ++i) {
        size_t len = strlen(strings[i]) + 1;
        size_t block = len + pad4(len);
        put_u32(p, (uint32_t)block);
        memcpy(p + 4, strings[i], len);
        p += 4 + block;
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
        "ra2mes",
        "Pokémon Ranger: Shadows of Almia",
        lz10_compress,
        check_mes,
        split_mes,
        build_mes,
    };
    return mes_main(argc, argv, &format);
}
