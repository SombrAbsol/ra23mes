// Copyright (c) 2026 SombrAbsol

#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stdlib.h>

// read/write a 32-bit value in little-endian byte order
uint32_t read_u32_le(const unsigned char *b);
void write_u32_le(unsigned char *b, uint32_t v);

// padding to 4-byte boundary
uint32_t pad4(uint32_t n);

// strdup implementation
char *xstrdup(const char *s);

// file helpers
int file_exists(const char *path);
int is_json_file(const unsigned char *buf, size_t size);
int is_json_buffer(const unsigned char *buf, size_t size);
int is_mes_buffer(const unsigned char *buf, size_t size);
unsigned char *read_file(const char *path, size_t *out_size);

// build output path from input filename
char *make_output_path(const char *input, const char *ext);

// json helpers
char **read_json_strings(const char *path, uint32_t *out_count);
int write_json_strings(const char *output, char *const *strings, uint32_t count);
uint32_t json_count_entries(const char *json);
char   **json_parse_strings(const char *json, uint32_t *out_count);

// (un)espace json string values
char *escape_json_string(const char *s, size_t maxlen);
char *unescape_json_string(const char *start, size_t len);

// free an array of heap-allocated strings
void free_string_array(char **strings, uint32_t count);

#endif /* UTILS_H */
