// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright 2026 SombrAbsol
/*
 * LZ10 compression handler.
 */

#ifndef LZ10_H
#define LZ10_H

#include <stdint.h>
#include <stdlib.h>

/*
 * Perform an heuristic check to determine if a buffer resembles
 * LZ10-compressed data. This is not a full validation.
 */
int looks_like_lz10(const uint8_t *buf, size_t size);

/*
 * Decompress an LZ10 buffer.
 */
uint8_t *lz10_decompress(const uint8_t *src, size_t srcSize, size_t *outSize);

/*
 * Compress a buffer using an LZ10 encoder. Use a greedy longest-match search
 * within a sliding window of up to 0x1000 bytes, with a maximum match length
 * of 0x12 bytes.
 */
uint8_t *lz10_compress(const uint8_t *src, size_t srcSize, size_t *outSize);

#endif /* LZ10_H */
