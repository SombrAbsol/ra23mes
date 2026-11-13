/*
 * Command-line interface shared by ra2mes and ra3mes.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#include "cli.h"

#include "fs.h"
#include "json.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define PRINTF_LIKE __attribute__((format(printf, 1, 2)))
#else
#define PRINTF_LIKE
#endif

// the OS built for, the UTC build date and time set by the Makefile, as
// yyyy.mm.dd-hh:mm:ss, and the Makefile recipe and static linking, if any
#if defined(_WIN32)
#define BUILD_OS "windows"
#elif defined(__APPLE__)
#define BUILD_OS "macos"
#elif defined(__linux__)
#define BUILD_OS "linux"
#else
#define BUILD_OS "unknown"
#endif
#define STRING(x)    #x
#define STRING_OF(x) STRING(x)
#ifdef BUILD_DATE
#define BUILD_UTC STRING_OF(BUILD_DATE)
#else
#define BUILD_UTC "unknown"
#endif
#ifdef MAKE_RECIPE
#define BUILD_RECIPE "-" STRING_OF(MAKE_RECIPE)
#else
#define BUILD_RECIPE ""
#endif
#ifdef STATIC_LINK
#define BUILD_STATIC "-static"
#else
#define BUILD_STATIC ""
#endif

/*
 * File to convert, loaded while its output is resolved.
 */
typedef struct {
    char in[PATH_SIZE]; // "-" for stdin
    char out[PATH_SIZE]; // "-" for stdout
    uint8_t *data; // contents, decompressed for a compressed MES file
    size_t size;
    int64_t count; // number of strings of a MES file, -1 for a JSON file
    bool compressed; // a MES file read LZ10-compressed
} FileJob;

static const MesFormat *mes; // format handled by the program
static FILE *logFile;
static int verbose = 1; // 0 quiet, 1 normal

// options shared by every converted file
static bool yes, lz;

/*
 * Print to the log stream, stdout unless the output goes to stdout.
 */
PRINTF_LIKE static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logFile, fmt, ap);
    va_end(ap);
}

/*
 * Print a command-line error and point to the help.
 */
PRINTF_LIKE static int usage_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", mes->name);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\nTry '%s --help' for more information.\n", mes->name);
    va_end(ap);
    return EXIT_FAILURE;
}

/*
 * Ask whether to replace an existing output file: 'y' answers yes, any other
 * key no. Keys typed beforehand are dropped, so that they cannot answer.
 */
static bool confirm_overwrite(const char *name)
{
    while (key_pressed()) {
        (void)get_key();
    }
    say("Replace '%s'? [y/N] ", name);
    fflush(logFile);
    bool replace = tolower(get_key()) == 'y';
    say("%s\n", replace ? "yes" : "no");
    return replace;
}

/*
 * Load a MES or JSON file, or stdin for "-", telling which. Return 1, or 0 if
 * neither but found in a directory, or -1 after printing the reason.
 */
static int load_file(const char *in, bool inDir, FileJob *job)
{
    if ((size_t)snprintf(job->in, sizeof(job->in), "%s", in)
        >= sizeof(job->in)) {
        fprintf(stderr, "%s: path too long: '%s'\n", mes->name, in);
        return -1;
    }
    job->data = read_file(in, &job->size);
    if (!job->data) {
        return -1;
    }

    // MES files are told first, as their first bytes may pass for JSON
    job->count = mes->check(&job->data, &job->size, &job->compressed);
    if (job->count < 0 && !json_detect((const char *)job->data, job->size)) {
        if (!inDir) {
            fprintf(
                stderr, "%s: '%s' is not a MES or JSON file\n", mes->name, in);
        }
        free(job->data);
        return inDir ? 0 : -1;
    }

    // with -z, a MES file is compressed, unless it already is
    if (lz && job->compressed) {
        if (!inDir) {
            fprintf(
                stderr, "%s: '%s' is already LZ10-compressed\n", mes->name, in);
        }
        free(job->data);
        return inDir ? 0 : -1;
    }
    return 1;
}

/*
 * Name the output of a loaded file: 'out' if given, or after the input, in
 * 'dir' or next to it, stdin going to stdout. Return false on failure after
 * printing the reason.
 */
static bool plan_output(const char *out, const char *dir, FileJob *job)
{
    const char *in = job->in;
    bool toStdout = out ? !strcmp(out, "-") : !strcmp(in, "-");

    /*
     * A named output file is used as is, else the input's name, extension
     * replaced: MES files become JSON files, and JSON files MES files, both
     * becoming MESLZ files with -z.
     */
    const char *name = path_basename(in);
    const char *dot = strrchr(name, '.');
    size_t len = dot && dot != name ? (size_t)(dot - name) : strlen(name);
    const char *ext = lz ? ".meslz" : job->count >= 0 ? ".json" : ".mes";
    char base[PATH_SIZE];
    if ((size_t)snprintf(base, sizeof(base), "%.*s%s", (int)len, name, ext)
        >= sizeof(base)) {
        fprintf(stderr, "%s: path too long: '%s'\n", mes->name, in);
        return false;
    }

    if (toStdout) {
        snprintf(job->out, sizeof(job->out), "-");
    } else if (out) {
        snprintf(job->out, sizeof(job->out), "%s", out);
    } else if (dir) {
        if (!join_path(job->out, sizeof(job->out), dir, base)) {
            return false;
        }
    } else if ((size_t)snprintf(job->out,
                   sizeof(job->out),
                   "%.*s%s",
                   (int)(name - in),
                   in,
                   base)
        >= sizeof(job->out)) { // next to the input, in the same directory
        fprintf(stderr, "%s: path too long: '%s'\n", mes->name, in);
        return false;
    }
    return true;
}

/*
 * Convert a loaded MES file to JSON, or a JSON file to MES. Return false on
 * failure after printing the reason.
 */
static bool convert_file(const FileJob *job)
{
    const char **strings = NULL;
    uint32_t count = 0;
    uint8_t *out = NULL;
    size_t outSize = 0;
    bool ok = false;

    // with -z, a MES file is compressed as read, unchanged
    if (job->count >= 0 && lz) {
        count = (uint32_t)job->count;
        out = mes->compress(job->data, job->size, &outSize);
        if (!out) {
            goto done; // reason already printed
        }
    } else if (job->count >= 0) {
        count = (uint32_t)job->count;
        strings = malloc((count ? count : 1) * sizeof(*strings));
        if (strings) {
            mes->split(job->data, strings);
            out = (uint8_t *)json_write(strings, count, &outSize);
        }
        if (!out) {
            fprintf(stderr, "%s: memory allocation failed\n", mes->name);
            goto done;
        }
    } else {
        int line;
        const char *err = json_read((char *)job->data, &strings, &count, &line);
        if (err) {
            fprintf(stderr,
                "%s: '%s': line %d: %s\n",
                mes->name,
                job->in,
                line,
                err);
            goto done;
        }
        out = mes->build(strings, count, &outSize);
        if (!out) {
            goto done; // reason already printed
        }

        // with -z, the MES file built is compressed
        if (lz) {
            size_t size = outSize;
            uint8_t *packed = mes->compress(out, size, &outSize);
            free(out);
            if (!(out = packed)) {
                goto done; // reason already printed
            }
        }
    }

    if (!write_file(job->out, out, outSize)) {
        goto done;
    }
    if (verbose != 0) {
        bool toStdout = !strcmp(job->out, "-");
        say("  %s: %u string%s -> %s\n",
            path_basename(job->in),
            count,
            count == 1 ? "" : "s",
            toStdout ? "(stdout)" : job->out);
    }
    ok = true;

done:
    free(out);
    free(strings);
    return ok;
}

/*
 * Print the usage and options.
 */
static void print_help(void)
{
    printf("Usage: %s [OPTION]... INPUT...\n", mes->name);
    printf("Convert %s MES text files to JSON and back.\n\n", mes->game);
    if (mes->compress) {
        printf("INPUT is a MES, MESLZ or JSON file, or a directory of them. "
               "MES and MESLZ\n"
               "files are converted to JSON, and JSON files to MES, written "
               "next to their\n"
               "input with the extension replaced by default. '-' as INPUT "
               "reads stdin,\n"
               "written to stdout unless -o is given.\n");
    } else {
        printf("INPUT is a MES or JSON file, or a directory of them. MES "
               "files are\n"
               "converted to JSON, and JSON files to MES, written next to "
               "their input\n"
               "with the extension replaced by default. '-' as INPUT reads "
               "stdin, written\n"
               "to stdout unless -o is given.\n");
    }
    printf(
        "\n"
        "Options:\n"
        "  -h, --help             show this help and exit\n"
        "      --version          show the version and exit\n"
        "  -o, --output=FILE      write the output of a single input file to "
        "FILE, '-'\n"
        "                         for stdout\n"
        "      --output-dir=DIR   write the outputs in DIR, created if "
        "missing\n");
    if (mes->compress) {
        printf("  -z, --lz               compress the MES files written with "
               "LZ10 (.meslz), and\n"
               "                         the MES files given instead of "
               "converting them\n");
    }
    printf("  -y, --yes              replace existing output files without "
           "asking\n"
           "  -q, --quiet, --silent  show errors and warnings only\n");
}

/*
 * Print the version, the UTC build date and time followed by the OS built for,
 * and the Makefile recipe and static linking, if any, then the copyright.
 */
static void print_version(void)
{
    printf("%s %s-UTC-%s%s%s\n",
        mes->name,
        BUILD_UTC,
        BUILD_OS,
        BUILD_RECIPE,
        BUILD_STATIC);
    printf("Copyright (C) 2026 SombrAbsol\n"
           "License MIT (Expat)\n"
           "This is free software: you are free to change and redistribute "
           "it.\n"
           "There is NO WARRANTY, to the extent permitted by law.\n");
}

/*
 * Run the program for a MES format version.
 */
int mes_main(int argc, char **argv, const MesFormat *format)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8); // ensure UTF-8 output on Windows
#endif
    mes = format;
    programName = format->name;
    logFile = stdout;

    /*
     * Options as getopt_long: short ones grouped or with an attached argument,
     * long ones abbreviated or with "=arg", before "--". Help and version stop.
     */
    static const struct {
        char name; // short name, or 0 for none
        const char *longName;
        bool hasArg;
        bool *flag; // flag set, or NULL
    } options[] = {
        { 'o', "output", true, NULL },
        { 0, "output-dir", true, NULL },
        { 'y', "yes", false, &yes },
        { 'q', "quiet", false, NULL },
        { 0, "silent", false, NULL },
        { 'h', "help", false, NULL },
        { 0, "version", false, NULL },
        { 'z', "lz", false, &lz }, // last, known by LZ10 formats only
    };
    size_t known = sizeof(options) / sizeof(*options) - !mes->compress;
    const char *out = NULL, *outDir = NULL;
    bool operands = false; // past "--"
    int n = 0; // operands, moved to argv[1] onwards, never past argv[i]
    for (int i = 1; i < argc; ++i) {
        char *a = argv[i];
        if (operands || a[0] != '-' || a[1] == '\0') { // "-" is stdin or stdout
            argv[++n] = a;
            continue;
        }
        if (!strcmp(a, "--")) {
            operands = true;
            continue;
        }

        // a long option or its only abbreviation, or a group of short ones
        for (const char *p = a + 1; *p;) {
            size_t k = known, len = strcspn(a + 2, "=");
            const char *arg = NULL;
            char shown[3] = { '-', *p, 0 }; // the option named in errors
            if (a[1] == '-') {
                for (size_t j = 0, found = 0; j < known; ++j) {
                    if (!strncmp(options[j].longName, a + 2, len)) {
                        bool exact = !options[j].longName[len];
                        k = exact || !found++ ? j : known + 1;
                        if (exact) {
                            break;
                        }
                    }
                }
                if (k >= known) {
                    return usage_error(k == known ? "unrecognized option '%s'"
                                                  : "option '%s' is ambiguous",
                        a);
                }
                arg = a[2 + len] ? a + 3 + len : NULL;
                if (arg && !options[k].hasArg) {
                    return usage_error(
                        "option '--%s' doesn't allow an argument",
                        options[k].longName);
                }
                shown[1] = '-';
                p = "";
            } else {
                for (k = 0; k < known && options[k].name != *p; ++k) { }
                if (k == known) {
                    return usage_error("invalid option -- '%c'", *p);
                }
                arg = options[k].hasArg && p[1] ? p + 1 : NULL;
                p = arg ? "" : p + 1;
            }
            if (options[k].hasArg && !arg && (arg = argv[++i]) == NULL) {
                return usage_error("option '%s%s' requires an argument",
                    shown,
                    shown[1] == '-' ? options[k].longName : "");
            }

            const char *name = options[k].longName;
            if (options[k].flag) {
                *options[k].flag = true;
            } else if (!strcmp(name, "output")) {
                out = arg;
            } else if (!strcmp(name, "output-dir")) {
                outDir = arg;
            } else if (!strcmp(name, "quiet") || !strcmp(name, "silent")) {
                verbose = 0;
            } else if (!strcmp(name, "version")) {
                print_version();
                return EXIT_SUCCESS;
            } else if (!strcmp(name, "help")) {
                print_help();
                return EXIT_SUCCESS;
            }
        }
    }

    // every operand is an input
    if (n == 0) {
        return usage_error("missing input operand");
    }
    if (out && outDir) {
        return usage_error("options '--output' and '--output-dir' conflict");
    }
    size_t count = 0;
    char **inputs = list_inputs(argv + 1, (size_t)n, &count);
    if (!inputs) {
        return EXIT_FAILURE;
    }

    /*
     * -o names the output of one input file, --output-dir a directory created
     * if missing. Stdin goes to stdout unless -o is given, messages to stderr.
     */
    bool failed = false;
    bool isDir = false;
    if (out && (count > 1 || (path_exists(inputs[0], &isDir) && isDir))) {
        usage_error("option '--output' needs a single input file");
        failed = true;
    }
    bool outIsDir = false;
    if (!failed && outDir && path_exists(outDir, &outIsDir) && !outIsDir) {
        fprintf(stderr, "%s: '%s' is not a directory\n", mes->name, outDir);
        failed = true;
    }
    if (failed || (outDir && !outIsDir && !mkdir_dir(outDir))) {
        for (size_t i = 0; i < count; ++i) {
            free(inputs[i]);
        }
        free(inputs);
        return EXIT_FAILURE;
    }
    for (size_t i = 0; i < count; ++i) {
        if (out ? !strcmp(out, "-") : !strcmp(inputs[i], "-")) {
            logFile = stderr;
        }
    }

    /*
     * Directories are listed before anything is written, and files are loaded
     * and confirmed first. Of two files with one output, the later one fails.
     */
    FileJob *jobs = NULL;
    int planned = 0;
    bool anyDir = false;
    for (size_t d = 0; d < count; ++d) {
        const char *in = inputs[d];
        isDir = false;
        if (strcmp(in, "-") && !path_exists(in, &isDir)) {
            fprintf(stderr, "%s: cannot access '%s'\n", mes->name, in);
            failed = true;
            continue;
        }
        anyDir |= isDir;
        size_t listed = 1;
        char **list = isDir ? list_files(in, &listed) : NULL;
        if (isDir && !list) {
            fprintf(stderr, "%s: cannot open directory '%s'\n", mes->name, in);
            failed = true;
            continue;
        }
        FileJob *grown = realloc(jobs, sizeof(*jobs) * (planned + listed + 1));
        if (!grown) {
            fprintf(stderr, "%s: memory allocation failed\n", mes->name);
            failed = true;
        }
        jobs = grown ? grown : jobs;
        if (grown && isDir && verbose != 0) {
            say("Converting all MES and JSON files in directory: '%s'\n", in);
        }

        size_t found = 0; // MES and JSON files among those listed
        for (size_t i = 0; grown && i < listed; ++i) {
            FileJob *job = &jobs[planned];
            int j = 0;
            int loaded = load_file(list ? list[i] : in, isDir, job);
            failed |= loaded < 0;
            if (loaded <= 0) {
                continue;
            }
            ++found;
            bool exists;
            if (!plan_output(out, outDir, job)) {
                failed = true;
                free(job->data);
                continue;
            }
            while (j < planned && strcmp(jobs[j].out, job->out)) {
                ++j;
            }
            if (j < planned) {
                fprintf(stderr,
                    "%s: '%s' would overwrite the output of '%s'\n",
                    mes->name,
                    job->in,
                    jobs[j].in);
                failed = true;
                free(job->data);
                continue;
            }
            if (!yes && strcmp(job->out, "-") && path_exists(job->out, &exists)
                && !confirm_overwrite(job->out)) {
                free(job->data);
                continue;
            }
            ++planned;
        }
        if (grown && isDir && found == 0 && verbose != 0) {
            say("No MES or JSON files found in '%s'\n", in);
        }

        for (size_t i = 0; list && i < listed; ++i) {
            free(list[i]);
        }
        free(list);
    }

    // an error does not stop the other files, but fails the run
    for (int i = 0; i < planned; ++i) {
        failed |= !convert_file(&jobs[i]);
    }
    bool ok = !failed && (anyDir || planned);

    for (int i = 0; i < planned; ++i) {
        free(jobs[i].data);
    }
    free(jobs);
    for (size_t i = 0; i < count; ++i) {
        free(inputs[i]);
    }
    free(inputs);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
