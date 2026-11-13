/*
 * LZ10 compression and decompression.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include "lz10.h"

#include "fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_SIZE 0x1000 // maximum displacement
#define MAX_MATCH   0x12 // maximum match length
#define MIN_MATCH   3 // minimum match length
#define HASH_BITS   13
#define HASH_SIZE   (1u << HASH_BITS)
#define NO_POS      UINT32_MAX // end of a hash chain

/*
 * Hash the 3-byte prefix starting at 'p'.
 */
static uint32_t hash3(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    return (v * 2654435761u) >> (32 - HASH_BITS);
}

/*
 * Decompress an LZ10 buffer. Return NULL without printing anything if it is
 * not a valid LZ10 stream or memory runs out.
 */
uint8_t *lz10_decompress(const uint8_t *src, size_t srcSize, size_t *outSize)
{
    if (srcSize < 4 || src[0] != 0x10) {
        return NULL;
    }

    size_t decSize
        = (size_t)src[1] | (size_t)src[2] << 8 | (size_t)src[3] << 16;
    // zeroed only for static analyzers, unable to tell that back-references
    // never read bytes not yet written; an empty stream gives an empty buffer
    uint8_t *dst = calloc(decSize ? decSize : 1, 1);
    if (!dst) {
        return NULL;
    }

    const uint8_t *sp = src + 4; // source pointer
    const uint8_t *send = src + srcSize; // source end
    uint8_t *dp = dst; // destination pointer
    uint8_t *dend = dst + decSize; // destination end

    // process flag groups
    while (dp < dend && sp < send) {
        uint8_t flags = *sp++;

        // process 8 symbols (MSB first)
        for (int bit = 0; bit < 8 && dp < dend; ++bit, flags <<= 1) {
            if (!(flags & 0x80)) {
                // literal byte
                if (sp >= send) {
                    goto invalid;
                }
                *dp++ = *sp++;
                continue;
            }

            // compressed block (back-reference)
            if (send - sp < 2) {
                goto invalid;
            }

            // upper 4 bits: length (stored as len-3)
            // lower 12 bits: displacement (stored as disp-1)
            size_t length = (size_t)(sp[0] >> 4) + MIN_MATCH;
            size_t disp = ((size_t)(sp[0] & 0x0F) << 8 | sp[1]) + 1;
            sp += 2;

            if ((size_t)(dp - dst) < disp) {
                goto invalid; // before the start of the output
            }

            // copy referenced bytes (overlap allowed)
            for (; length && dp < dend; --length, ++dp) {
                *dp = dp[-(ptrdiff_t)disp];
            }
        }
    }

    // ensure exact output size was produced
    if (dp == dend) {
        *outSize = decSize;
        return dst;
    }

invalid:
    free(dst);
    return NULL;
}

/*
 * Compress a buffer with LZ10, using a greedy longest-match search over a
 * 0x1000-byte window. Return NULL on failure after printing the reason.
 */
uint8_t *lz10_compress(const uint8_t *src, size_t srcSize, size_t *outSize)
{
    if (srcSize > 0xFFFFFF) {
        fprintf(stderr, "%s: file too large to compress\n", programName);
        return NULL;
    }

    /*
     * Worst-case size: 4 bytes for the header, plus the full source size if
     * all data is emitted as literals, plus one flag byte for every 8 symbols.
     */
    uint8_t *out = malloc(4 + srcSize + ((srcSize + 7) >> 3));

    /*
     * Hash chains of 3-byte prefixes, oldest to newest so candidates come
     * farthest first. Zeroing them only helps analyzers: links are set first.
     */
    uint32_t *next = calloc(srcSize + 1, sizeof(*next));
    uint32_t head[HASH_SIZE]; // oldest position of each chain
    uint32_t tail[HASH_SIZE]; // newest position of each chain

    if (!out || !next) {
        fprintf(stderr, "%s: memory allocation failed\n", programName);
        free(out);
        free(next);
        return NULL;
    }
    memset(head, 0xFF, sizeof(head)); // all chains start empty

    // write header
    out[0] = 0x10;
    out[1] = (uint8_t)srcSize;
    out[2] = (uint8_t)(srcSize >> 8);
    out[3] = (uint8_t)(srcSize >> 16);

    uint8_t *pak = out + 4;
    uint8_t *flagp = NULL; // pointer to current flag byte
    uint8_t mask = 0; // current bit mask
    size_t pos = 0; // current input position
    size_t hashed = 0; // positions below this are in the hash chains

    while (pos < srcSize) {
        // start a new flag byte every 8 items
        mask >>= 1;
        if (!mask) {
            flagp = pak++;
            *flagp = 0;
            mask = 0x80;
        }

        // maximum match length
        size_t maxLen = srcSize - pos;
        if (maxLen > MAX_MATCH) {
            maxLen = MAX_MATCH;
        }

        size_t bestLen = MIN_MATCH - 1; // minimum match threshold
        size_t bestDisp = 0;

        if (maxLen >= MIN_MATCH) {
            for (; hashed < pos; ++hashed) {
                uint32_t h = hash3(src + hashed);
                if (head[h] == NO_POS) {
                    head[h] = (uint32_t)hashed;
                } else {
                    next[tail[h]] = (uint32_t)hashed;
                }
                tail[h] = (uint32_t)hashed;
                next[hashed] = NO_POS;
            }

            // drop chain entries that slid out of the window
            uint32_t h = hash3(src + pos);
            size_t winStart = pos > WINDOW_SIZE ? pos - WINDOW_SIZE : 0;
            while (head[h] != NO_POS && head[h] < winStart) {
                head[h] = next[head[h]];
            }

            /*
             * Find the longest match, farthest first so ties keep the largest
             * displacement, never 1, and stop once no longer match is possible.
             */
            for (uint32_t c = head[h];
                c != NO_POS && c + 2 <= pos && bestLen < maxLen;
                c = next[c]) {
                const uint8_t *a = src + pos;
                const uint8_t *b = src + c;
                size_t l = 0;
                while (l < maxLen && a[l] == b[l]) {
                    ++l;
                }

                if (l > bestLen) {
                    bestLen = l;
                    bestDisp = pos - c;
                }
            }
        }

        if (bestDisp) {
            // encode compressed block
            *flagp |= mask;

            size_t lenField = bestLen - MIN_MATCH;
            size_t dispField = bestDisp - 1;

            *pak++ = (uint8_t)(lenField << 4 | dispField >> 8);
            *pak++ = (uint8_t)dispField;

            pos += bestLen;
        } else {
            // literal byte
            *pak++ = src[pos++];
        }
    }

    free(next);
    *outSize = (size_t)(pak - out);
    return out;
}
