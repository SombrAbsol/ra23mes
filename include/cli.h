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
    bool lz; // whether LZ10-compressed MES files are supported

    /*
     * Return the number of strings of a MES file, or -1 if it is not one. A
     * compressed MES file is replaced in '*data' by its decompressed contents.
     */
    int64_t (*check)(uint8_t **data, size_t *size);

    /*
     * Point 'strings' to the strings of a MES file accepted by 'check'.
     */
    void (*split)(const uint8_t *data, const char **strings);

    /*
     * Build a MES file of 'count' strings, LZ10-compressed if 'lz'. Return
     * NULL on failure after printing the reason.
     */
    uint8_t *(*build)(
        const char *const *strings, uint32_t count, bool lz, size_t *outSize);
} MesFormat;

/*
 * Run the program for a MES format version.
 */
int mes_main(int argc, char **argv, const MesFormat *format);

#endif /* CLI_H */
