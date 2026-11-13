/*
 * Command-line interface shared by ra2mes and ra3mes.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef CLI_H
#define CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * MES format version of a game, handled by one program.
 */
typedef struct {
    const char *name; // program name
    const char *game; // game whose MES files are converted

    /*
     * Compress a MES file with LZ10, NULL if the game has no compressed MES
     * files. Return NULL on failure after printing the reason.
     */
    uint8_t *(*compress)(const uint8_t *src, size_t srcSize, size_t *outSize);

    /*
     * Return the number of strings of a MES file, or -1 if it is not one. A
     * compressed MES file is replaced in '*data' by its decompressed contents,
     * '*compressed' telling which.
     */
    int64_t (*check)(uint8_t **data, size_t *size, bool *compressed);

    /*
     * Point 'strings' to the strings of a MES file accepted by 'check'.
     */
    void (*split)(const uint8_t *data, const char **strings);

    /*
     * Build a MES file of 'count' strings. Return NULL on failure after
     * printing the reason.
     */
    uint8_t *(*build)(
        const char *const *strings, uint32_t count, size_t *outSize);
} MesFormat;

/*
 * Run the program for a MES format version.
 */
int mes_main(int argc, char **argv, const MesFormat *format);

#endif /* CLI_H */
