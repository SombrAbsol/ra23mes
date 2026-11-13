/*
 * LZ10 compression and decompression.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LZ10_H
#define LZ10_H

#include <stddef.h>
#include <stdint.h>

/*
 * Decompress an LZ10 buffer. Return NULL without printing anything if it is
 * not a valid LZ10 stream or memory runs out.
 */
uint8_t *lz10_decompress(const uint8_t *src, size_t srcSize, size_t *outSize);

/*
 * Compress a buffer with LZ10, using a greedy longest-match search over a
 * 0x1000-byte window. Return NULL on failure after printing the reason.
 */
uint8_t *lz10_compress(const uint8_t *src, size_t srcSize, size_t *outSize);

#endif /* LZ10_H */
