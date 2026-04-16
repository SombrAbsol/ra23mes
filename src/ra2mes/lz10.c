// Copyright (c) 2026 SombrAbsol

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "lz10.h"

// check if mes seems lz10-compressed
int looks_like_lz10(const uint8_t *buf, size_t size) {
    if (!buf || size < 4)
        return 0;

    if (buf[0] != 0x10)
        return 0;

    uint32_t decSize =
    (uint32_t)buf[1] |
    ((uint32_t)buf[2] << 8) |
    ((uint32_t)buf[3] << 16);

    return decSize > 0;
}

// lz10 decompression
uint8_t *lz10_decompress(const uint8_t *src, size_t srcSize, size_t *outSize) {
    if (!src || srcSize < 4 || !outSize) return NULL; // invalid input

    uint8_t method = src[0];
    if (method != 0x10) return NULL; // check compression type

    // read decompressed size from header
    uint32_t decSize = (uint32_t)src[1] | ((uint32_t)src[2] << 8) | ((uint32_t)src[3] << 16);
    if (decSize == 0) return NULL;

    uint8_t *dst = malloc(decSize); // allocate output buffer
    if (!dst) return NULL;

    const uint8_t *sp = src + 4;
    const uint8_t *send = src + srcSize;
    uint8_t *dp = dst;
    uint8_t *dend = dst + decSize;

    // main decompression loop
    while (dp < dend && sp < send) {
        uint8_t flags = *sp++;
        for (int bit = 0; bit < 8 && dp < dend; ++bit) {
            if ((flags & 0x80) == 0) { // literal byte
                if (sp >= send) { free(dst); return NULL; }
                *dp++ = *sp++;
            } else { // compressed block
                if (sp + 1 >= send) { free(dst); return NULL; }
                uint8_t b1 = *sp++;
                uint8_t b2 = *sp++;
                int length = (b1 >> 4) + 3; // length of copy
                size_t disp = (size_t)((((b1 & 0x0F) << 8) | b2) + 1); // displacement

                if ((size_t)(dp - dst) < disp) { free(dst); return NULL; }

                uint8_t *src_copy = dp - disp;
                for (int k = 0; k < length && dp < dend; ++k) {
                    *dp++ = *src_copy++;
                }
            }
            flags <<= 1;
        }
    }

    if (dp != dend) { free(dst); return NULL; }
    *outSize = decSize;
    return dst;
}

// lz10 compression
uint8_t *lz10_compress(const uint8_t *src, size_t srcSize, size_t *outSize) {
    if (!src || !outSize)
        return NULL;

    // worst-case size
    size_t maxSize = 4 + srcSize + ((srcSize + 7) >> 3);
    uint8_t *out = calloc(maxSize, 1);
    if (!out)
        return NULL;

    // header: 0x10 + 24-bit raw size
    out[0] = 0x10;
    out[1] = (uint8_t)(srcSize & 0xFF);
    out[2] = (uint8_t)((srcSize >> 8) & 0xFF);
    out[3] = (uint8_t)((srcSize >> 16) & 0xFF);

    const uint8_t *raw = src;
    const uint8_t *rawEnd = src + srcSize;
    uint8_t *pak = out + 4;

    uint8_t *flagp = NULL;
    uint8_t mask = 0;

    while (raw < rawEnd) {
        // start a new flag byte every 8 symbols
        if (!(mask >>= 1)) {
            flagp = pak++;
            *flagp = 0;
            mask = 0x80;
        }

        size_t bestLen = 2;
        size_t bestPos = 0;

        size_t maxPos = (size_t)(raw - src);
        if (maxPos > 0x1000)
            maxPos = 0x1000;

        size_t maxLen = (size_t)(rawEnd - raw);
        if (maxLen > 0x12)
            maxLen = 0x12;

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
                if (l == maxLen)
                    break;
            }
        }

        if (bestLen > 2) {
            // compressed block
            *flagp |= mask;

            size_t lenField = bestLen - (2 + 1);
            size_t posField = bestPos - 1;

            *pak++ = (uint8_t)((lenField << 4) | (posField >> 8));
            *pak++ = (uint8_t)(posField & 0xFF);

            raw += bestLen;
        } else {
            *pak++ = *raw++; // literal byte
        }
    }

    *outSize = (size_t)(pak - out);
    return out;
}
