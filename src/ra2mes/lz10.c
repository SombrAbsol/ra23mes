/*
 * LZ10 compression handler.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "lz10.h"

/*
 * Perform an heuristic check to determine if a buffer resembles
 * LZ10-compressed data. This is not a full validation.
 */
int looks_like_lz10(const uint8_t *buf, size_t size) {
    if (!buf || size < 4)
        return 0;
    if (buf[0] != 0x10)
        return 0;

    // 24-bit little-endian decompressed size
    uint32_t decSize =
        (uint32_t)buf[1] | ((uint32_t)buf[2] << 8) | ((uint32_t)buf[3] << 16);
    return decSize > 0;
}

/*
 * Decompress an LZ10 buffer.
 */
uint8_t *lz10_decompress(const uint8_t *src, size_t srcSize, size_t *outSize) {
    if (!src || srcSize < 4 || !outSize) {
        fprintf(stderr, "lz10_decompress: invalid arguments\n");
        return NULL;
    }

    uint8_t method = src[0];
    if (method != 0x10) {
        fprintf(stderr, "lz10_decompress: unsupported method 0x%02X\n", method);
        return NULL;
    }

    uint32_t decSize =
        (uint32_t)src[1] | ((uint32_t)src[2] << 8) | ((uint32_t)src[3] << 16);
    if (decSize == 0) {
        fprintf(stderr, "lz10_decompress: zero decompressed size\n");
        return NULL;
    }

    uint8_t *dst = malloc(decSize);
    if (!dst) {
        fprintf(stderr, "lz10_decompress: allocation failed (%u bytes)\n",
                decSize);
        return NULL;
    }

    const uint8_t *sp = src + 4;         // source pointer
    const uint8_t *send = src + srcSize; // source end
    uint8_t *dp = dst;                   // destination pointer
    uint8_t *dend = dst + decSize;       // destination end

    // process flag groups
    while (dp < dend && sp < send) {
        uint8_t flags = *sp++;

        // process 8 symbols (MSB first)
        for (int bit = 0; bit < 8 && dp < dend; ++bit) {
            if ((flags & 0x80) == 0) {
                // literal byte
                if (sp >= send) {
                    fprintf(
                        stderr,
                        "lz10_decompress: unexpected end of input (literal)\n");
                    free(dst);
                    return NULL;
                }
                *dp++ = *sp++;
            } else {
                // compressed block (back-reference)
                if (sp + 1 >= send) {
                    fprintf(
                        stderr,
                        "lz10_decompress: unexpected end of input (backref)\n");
                    free(dst);
                    return NULL;
                }

                uint8_t b1 = *sp++;
                uint8_t b2 = *sp++;

                // lower 12 bits: displacement (stored as disp-1)
                size_t disp = (size_t)((((b1 & 0x0F) << 8) | b2) + 1);

                // validate back-reference
                if ((size_t)(dp - dst) < disp) {
                    fprintf(
                        stderr,
                        "lz10_decompress: invalid back-reference (disp=%zu)\n",
                        disp);
                    free(dst);
                    return NULL;
                }

                // upper 4 bits: length (stored as len-3)
                int length = (b1 >> 4) + 3;

                uint8_t *src_copy = dp - disp;

                // copy referenced bytes (overlap allowed)
                for (int k = 0; k < length && dp < dend; ++k) {
                    *dp++ = *src_copy++;
                }
            }

            flags <<= 1;
        }
    }

    // ensure exact output size was produced
    if (dp != dend) {
        fprintf(stderr, "lz10_decompress: size mismatch (expected %u)\n",
                decSize);
        free(dst);
        return NULL;
    }

    *outSize = decSize;
    return dst;
}

/*
 * Compress a buffer using an LZ10 encoder. Use a greedy longest-match search
 * within a sliding window of up to 0x1000 bytes, with a maximum match length
 * of 0x12 bytes.
 */
uint8_t *lz10_compress(const uint8_t *src, size_t srcSize, size_t *outSize) {
    if (!src || !outSize) {
        fprintf(stderr, "lz10_compress: invalid arguments\n");
        return NULL;
    }

    if (srcSize > 0xFFFFFF) {
        fprintf(stderr, "lz10_compress: input too large (%zu bytes)\n",
                srcSize);
        return NULL;
    }

    /*
     * Worst-case size: 4 bytes for the header, plus the full source size if
     * all data is emitted as literals, plus one flag byte for every 8 symbols.
     */
    size_t maxSize = 4 + srcSize + ((srcSize + 7) >> 3);

    uint8_t *out = calloc(maxSize, 1);
    if (!out) {
        fprintf(stderr, "lz10_compress: allocation failed\n");
        return NULL;
    }

    // write header
    out[0] = 0x10;
    out[1] = (uint8_t)(srcSize & 0xFF);
    out[2] = (uint8_t)((srcSize >> 8) & 0xFF);
    out[3] = (uint8_t)((srcSize >> 16) & 0xFF);

    const uint8_t *raw = src;
    const uint8_t *rawEnd = src + srcSize;
    uint8_t *pak = out + 4;

    uint8_t *flagp = NULL; // pointer to current flag byte
    uint8_t mask = 0;      // current bit mask

    while (raw < rawEnd) {
        // start a new flag byte every 8 items
        if (!(mask >>= 1)) {
            flagp = pak++;
            *flagp = 0;
            mask = 0x80;
        }

        size_t bestLen = 2; // minimum match threshold
        size_t bestPos = 0;

        // search window size (max 0x1000 bytes back)
        size_t maxPos = (size_t)(raw - src);
        if (maxPos > 0x1000)
            maxPos = 0x1000;

        // maximum match length
        size_t maxLen = (size_t)(rawEnd - raw);
        if (maxLen > 0x12)
            maxLen = 0x12;

        // brute-force search for longest match
        for (size_t p = maxPos; p > 1; --p) {
            if (raw[0] != raw[-(ptrdiff_t)p])
                continue;

            size_t l = 1;
            const uint8_t *a = raw + 1;
            const uint8_t *b = raw - p + 1;

            while (l < maxLen && *a == *b) {
                ++a;
                ++b;
                ++l;
            }

            if (l > bestLen) {
                bestLen = l;
                bestPos = p;
            }
        }

        if (bestLen > 2) {
            // encode compressed block
            *flagp |= mask;

            size_t lenField = bestLen - 3;
            size_t posField = bestPos - 1;

            *pak++ = (uint8_t)((lenField << 4) | (posField >> 8));
            *pak++ = (uint8_t)(posField & 0xFF);

            raw += bestLen;
        } else {
            // literal byte
            *pak++ = *raw++;
        }
    }

    *outSize = (size_t)(pak - out);
    return out;
}
