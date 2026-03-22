/*
 * strbuilder.h -- Dynamic string builder for NCD
 *
 * Provides a resizable string buffer with efficient append operations.
 * Used for building JSON output and other dynamic string construction.
 */

#ifndef NCD_STRBUILDER_H
#define NCD_STRBUILDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

/* ================================================================ types  */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} StringBuilder;

/* ================================================================ lifecycle  */

/* Initialize a new string builder with default capacity. */
void sb_init(StringBuilder *sb);

/* Free all memory associated with the string builder. */
void sb_free(StringBuilder *sb);

/* Reset the string builder without freeing memory (reuse buffer). */
void sb_clear(StringBuilder *sb);

/* ================================================================ append operations  */

/* Append a null-terminated string. Returns false on OOM. */
bool sb_append(StringBuilder *sb, const char *s);

/* Append a single character. Returns false on OOM. */
bool sb_appendc(StringBuilder *sb, char c);

/* Append exactly n characters from string s (may contain nulls). Returns false on OOM. */
bool sb_appendn(StringBuilder *sb, const char *s, size_t n);

/* Append formatted string (printf-style). */
void sb_appendf(StringBuilder *sb, const char *fmt, ...);

/* Append formatted string with va_list. */
void sb_vappendf(StringBuilder *sb, const char *fmt, va_list ap);

/* ================================================================ JSON utilities  */

/* Append a JSON-escaped string literal (including surrounding quotes). Returns false on OOM. */
bool sb_append_json_str(StringBuilder *sb, const char *s);

/* ================================================================ buffer management  */

/* 
 * Transfer ownership of the buffer to the caller.
 * The caller is responsible for freeing the returned pointer.
 * The StringBuilder is reset and can be reused.
 */
char *sb_steal(StringBuilder *sb);

/* Return a malloc'd copy of the current buffer. Caller must free. */
char *sb_dup(const StringBuilder *sb);

/* Ensure buffer has at least min_cap capacity. Returns false on failure. */
bool sb_ensure_cap(StringBuilder *sb, size_t min_cap);

#ifdef __cplusplus
}
#endif

#endif /* NCD_STRBUILDER_H */
