/*
 * File, path and console helpers.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include "fs.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <conio.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#else
#include <dirent.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define mkdir(path, mode) _mkdir(path)
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif
#endif

/*
 * Growable list of file names.
 */
typedef struct {
    char **names;
    size_t count;
    size_t capacity;
} NameList;

const char *programName;

#ifndef _WIN32
static struct termios savedTio;
static bool rawMode;

/*
 * Restore the terminal settings saved by raw_enable.
 */
static void raw_disable(void)
{
    tcsetattr(0, TCSANOW, &savedTio);
}

/*
 * Put the terminal in non-canonical mode without echo, like _getch.
 */
static void raw_enable(void)
{
    if (rawMode || !isatty(0)) {
        return;
    }
    tcgetattr(0, &savedTio);
    struct termios t = savedTio;
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    tcsetattr(0, TCSANOW, &t);
    atexit(raw_disable);
    rawMode = true;
}
#endif

/*
 * Open a file with fopen, or fopen_s on Windows.
 */
FILE *xfopen(const char *path, const char *mode)
{
#ifdef _WIN32
    FILE *f = NULL;
    return fopen_s(&f, path, mode) == 0 ? f : NULL;
#else
    return fopen(path, mode);
#endif
}

/*
 * Switch stdin or stdout to binary mode.
 */
void set_binary(FILE *f)
{
#ifdef _WIN32
    _setmode(_fileno(f), _O_BINARY);
#else
    (void)f; // no text mode on POSIX
#endif
}

/*
 * Read a whole file, or stdin for "-", null-terminated beyond '*outSize'.
 * Return NULL on failure after printing the reason.
 */
uint8_t *read_file(const char *path, size_t *outSize)
{
    bool fromStdin = !strcmp(path, "-");
    FILE *f = fromStdin ? stdin : xfopen(path, "rb");
    if (!f) {
        fprintf(stderr, "%s: cannot open '%s'\n", programName, path);
        return NULL;
    }
    if (fromStdin) {
        set_binary(stdin);
    }

    /*
     * Read by chunks into a buffer doubled when full, which suits stdin and
     * pipes, keeping one byte free for the null terminator.
     */
    size_t size = 0;
    size_t capacity = 0x10000;
    uint8_t *buf = malloc(capacity);
    while (buf && !feof(f) && !ferror(f)) {
        if (size + 1 == capacity) {
            uint8_t *grown = realloc(buf, capacity * 2);
            if (!grown) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = grown;
            capacity *= 2;
        }
        size += fread(buf + size, 1, capacity - size - 1, f);
    }

    if (!buf) {
        fprintf(stderr, "%s: memory allocation failed\n", programName);
    } else if (ferror(f)) {
        fprintf(stderr, "%s: cannot read '%s'\n", programName, path);
        free(buf);
        buf = NULL;
    } else {
        buf[size] = '\0';
        *outSize = size;
    }
    if (!fromStdin) {
        fclose(f);
    }
    return buf;
}

/*
 * Write bytes to a file, or to stdout for "-", creating or truncating it as
 * needed. Return false on failure after printing the reason.
 */
bool write_file(const char *path, const uint8_t *data, size_t size)
{
    bool toStdout = !strcmp(path, "-");
    FILE *f = toStdout ? stdout : xfopen(path, "wb");
    if (!f) {
        fprintf(stderr, "%s: cannot create '%s'\n", programName, path);
        return false;
    }
    if (toStdout) {
        set_binary(stdout);
    }

    bool ok = fwrite(data, 1, size, f) == size;
    ok = (toStdout ? fflush(f) : fclose(f)) == 0 && ok;
    if (!ok) {
        fprintf(stderr, "%s: failed to write '%s'\n", programName, path);
    }
    return ok;
}

/*
 * Create a directory if it does not already exist.
 */
bool mkdir_dir(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return true;
    }
    fprintf(stderr, "%s: failed to create '%s'\n", programName, path);
    return false;
}

/*
 * Check whether a path exists and whether it is a directory.
 */
bool path_exists(const char *path, bool *isDir)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    *isDir = S_ISDIR(st.st_mode);
    return true;
}

/*
 * Return a pointer to the filename component of a path, skipping any leading
 * directory segments.
 */
const char *path_basename(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; ++p) {
        // on Windows, "C:name" is a name in the current directory of drive C
        if (IS_SEP(*p) || (PATH_SEP[0] == '\\' && *p == ':')) {
            base = p + 1;
        }
    }
    return base;
}

/*
 * Join a directory and a name into a single path.
 */
bool join_path(char *dst, size_t dstSize, const char *dir, const char *name)
{
    size_t len = strlen(dir);
    const char *sep = len == 0 || IS_SEP(dir[len - 1]) ? "" : PATH_SEP;
    if ((size_t)snprintf(dst, dstSize, "%s%s%s", dir, sep, name) < dstSize) {
        return true;
    }
    fprintf(
        stderr, "%s: path too long: '%s%s%s'\n", programName, dir, sep, name);
    return false;
}

/*
 * Append a copy of a path to a list. Return false if memory runs out.
 */
static bool add_path(NameList *l, const char *path)
{
    if (l->count == l->capacity) {
        l->capacity = l->capacity ? l->capacity * 2 : 64;
        char **tmp = realloc(l->names, l->capacity * sizeof(*tmp));
        if (!tmp) {
            return false;
        }
        l->names = tmp;
    }
    size_t size = strlen(path) + 1;
    char *copy = malloc(size);
    if (!copy) {
        return false;
    }
    memcpy(copy, path, size);
    l->names[l->count++] = copy;
    return true;
}

/*
 * Append the path of 'name' in 'dir' to a list if it is a regular file.
 * Return false if memory runs out.
 */
static bool add_file(NameList *l, const char *dir, const char *name)
{
    char path[PATH_SIZE];
    struct stat st;
    if (!join_path(path, sizeof(path), dir, name) || stat(path, &st) != 0
        || !S_ISREG(st.st_mode)) {
        return true; // skipped, not an error
    }
    return add_path(l, path);
}

/*
 * Compare two file names for qsort.
 */
static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/*
 * List the input operands, expanding '*' and '?' on Windows as the Microsoft C
 * runtime does. Return NULL if memory runs out after printing the reason.
 */
char **list_inputs(char *const *operands, size_t n, size_t *count)
{
    NameList list = { 0 };
    bool ok = true;
    for (size_t i = 0; ok && i < n; ++i) {
        size_t before = list.count;
#ifdef _WIN32
        const char *in = operands[i];
        int dirLen = (int)(path_basename(in) - in); // kept in the paths
        struct _finddata_t file;
        intptr_t handle = strpbrk(in, "*?") ? _findfirst(in, &file) : -1;
        if (handle != -1) {
            do {
                char path[PATH_SIZE];
                if (!strcmp(file.name, ".") || !strcmp(file.name, "..")) {
                    continue;
                }
                if ((size_t)snprintf(
                        path, sizeof(path), "%.*s%s", dirLen, in, file.name)
                    >= sizeof(path)) {
                    fprintf(stderr,
                        "%s: path too long: '%.*s%s'\n",
                        programName,
                        dirLen,
                        in,
                        file.name);
                    continue;
                }
                ok = add_path(&list, path);
            } while (ok && _findnext(handle, &file) == 0);
            _findclose(handle);
        }
#endif
        // an operand without wildcards, or matching nothing, is kept as is
        if (ok && list.count == before) {
            ok = add_path(&list, operands[i]);
        }
    }

    if (!ok) {
        fprintf(stderr, "%s: memory allocation failed\n", programName);
        for (size_t i = 0; i < list.count; ++i) {
            free(list.names[i]);
        }
        free(list.names);
        return NULL;
    }
    *count = list.count;
    return list.names;
}

/*
 * List the regular files of a directory, in alphabetical order. Return NULL if
 * the directory cannot be read.
 */
char **list_files(const char *dir, size_t *count)
{
    NameList list = { 0 };
    bool ok = true;
#ifdef _WIN32
    char pattern[PATH_SIZE];
    if (!join_path(pattern, sizeof(pattern), dir, "*")) {
        return NULL;
    }
    struct _finddata_t file;
    intptr_t handle = _findfirst(pattern, &file);
    if (handle == -1) {
        return NULL;
    }
    do {
        ok = add_file(&list, dir, file.name);
    } while (ok && _findnext(handle, &file) == 0);
    _findclose(handle);
#else
    DIR *d = opendir(dir);
    if (!d) {
        return NULL;
    }
    for (struct dirent *entry; ok && (entry = readdir(d)) != NULL;) {
        ok = add_file(&list, dir, entry->d_name);
    }
    closedir(d);
#endif

    if (!ok) {
        fprintf(stderr, "%s: memory allocation failed\n", programName);
    }
    if (list.count) {
        qsort(list.names, list.count, sizeof(*list.names), compare_names);
    }
    *count = list.count;
    return list.names ? list.names : calloc(1, sizeof(char *));
}

/*
 * Check for a waiting key press without blocking.
 */
bool key_pressed(void)
{
#ifdef _WIN32
    return _kbhit() != 0;
#else
    raw_enable();
    fd_set s;
    FD_ZERO(&s);
    FD_SET(0, &s);
    struct timeval tv = { 0, 0 };
    return rawMode && select(1, &s, NULL, NULL, &tv) > 0;
#endif
}

/*
 * Read one key press without echo.
 */
int get_key(void)
{
#ifdef _WIN32
    return _getch();
#else
    raw_enable();
    unsigned char c;
    // without a terminal, or at its end, answer escape
    return rawMode && read(0, &c, 1) == 1 ? c : 0x1b;
#endif
}
