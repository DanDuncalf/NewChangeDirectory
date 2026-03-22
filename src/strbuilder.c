/*
 * strbuilder.c -- String builder implementation
 */

#include "strbuilder.h"
#include "ncd.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

/* ================================================================ constants  */

#define SB_INITIAL_CAP 256
#define SB_MAX_CAP (1ULL << 31)  /* 2GB limit */

/* ================================================================ lifecycle  */

void sb_init(StringBuilder *sb)
{
    if (!sb) return;
    sb->cap = SB_INITIAL_CAP;
    sb->buf = ncd_malloc(sb->cap);
    sb->buf[0] = '\0';
    sb->len = 0;
}

void sb_free(StringBuilder *sb)
{
    if (sb && sb->buf) {
        free(sb->buf);
        sb->buf = NULL;
        sb->len = 0;
        sb->cap = 0;
    }
}

void sb_clear(StringBuilder *sb)
{
    if (sb && sb->buf) {
        sb->buf[0] = '\0';
        sb->len = 0;
    }
}

/* ================================================================ buffer management  */

bool sb_ensure_cap(StringBuilder *sb, size_t min_cap)
{
    if (!sb || min_cap > SB_MAX_CAP) return false;
    if (sb->cap >= min_cap) return true;
    
    size_t new_cap = sb->cap;
    while (new_cap < min_cap) {
        if (new_cap > SB_MAX_CAP / 2) {
            if (min_cap <= SB_MAX_CAP) {
                new_cap = SB_MAX_CAP;
                break;
            }
            return false;
        }
        new_cap *= 2;
    }
    
    sb->buf = ncd_realloc(sb->buf, new_cap);
    sb->cap = new_cap;
    return true;
}

/* ================================================================ append operations  */

bool sb_appendn(StringBuilder *sb, const char *s, size_t n)
{
    if (!sb || !s || n == 0) return true;
    if (!sb_ensure_cap(sb, sb->len + n + 1)) return false;
    
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
    return true;
}

bool sb_append(StringBuilder *sb, const char *s)
{
    if (!sb || !s) return true;
    return sb_appendn(sb, s, strlen(s));
}

bool sb_appendc(StringBuilder *sb, char c)
{
    if (!sb) return true;
    if (!sb_ensure_cap(sb, sb->len + 2)) return false;
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
    return true;
}

void sb_vappendf(StringBuilder *sb, const char *fmt, va_list ap)
{
    if (!sb || !fmt) return;
    
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    
    if (n < 0) return;
    
    size_t need = (size_t)n + 1;
    if (!sb_ensure_cap(sb, sb->len + need)) return;
    
    vsnprintf(sb->buf + sb->len, need, fmt, ap);
    sb->len += (size_t)n;
}

void sb_appendf(StringBuilder *sb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sb_vappendf(sb, fmt, ap);
    va_end(ap);
}

/* ================================================================ JSON utilities  */

bool sb_append_json_str(StringBuilder *sb, const char *s)
{
    if (!sb) return true;
    if (!s) s = "";
    
    /* First pass: calculate required space */
    size_t extra = 2;  /* opening and closing quotes */
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':
            case '\\':
            case '\n':
            case '\r':
            case '\t':
            case '\b':
            case '\f':
                extra += 2;  /* backslash + char */
                break;
            default:
                if ((unsigned char)*p < 0x20) {
                    extra += 6;  /* \uXXXX */
                } else {
                    extra += 1;
                }
        }
    }
    
    if (!sb_ensure_cap(sb, sb->len + extra + 1)) return false;
    
    /* Second pass: append directly to buffer */
    sb->buf[sb->len++] = '"';
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':
                sb->buf[sb->len++] = '\\';
                sb->buf[sb->len++] = '"';
                break;
            case '\\':
                sb->buf[sb->len++] = '\\';
                sb->buf[sb->len++] = '\\';
                break;
            case '\n':
                sb->buf[sb->len++] = '\\';
                sb->buf[sb->len++] = 'n';
                break;
            case '\r':
                sb->buf[sb->len++] = '\\';
                sb->buf[sb->len++] = 'r';
                break;
            case '\t':
                sb->buf[sb->len++] = '\\';
                sb->buf[sb->len++] = 't';
                break;
            case '\b':
                sb->buf[sb->len++] = '\\';
                sb->buf[sb->len++] = 'b';
                break;
            case '\f':
                sb->buf[sb->len++] = '\\';
                sb->buf[sb->len++] = 'f';
                break;
            default:
                if ((unsigned char)*p < 0x20) {
                    /* \uXXXX format for control characters */
                    static const char hex[] = "0123456789abcdef";
                    sb->buf[sb->len++] = '\\';
                    sb->buf[sb->len++] = 'u';
                    sb->buf[sb->len++] = '0';
                    sb->buf[sb->len++] = '0';
                    sb->buf[sb->len++] = hex[((unsigned char)*p) >> 4];
                    sb->buf[sb->len++] = hex[((unsigned char)*p) & 0xf];
                } else {
                    sb->buf[sb->len++] = *p;
                }
        }
    }
    sb->buf[sb->len++] = '"';
    sb->buf[sb->len] = '\0';
    return true;
}

/* ================================================================ buffer transfer  */

char *sb_steal(StringBuilder *sb)
{
    if (!sb) return NULL;
    char *result = sb->buf;
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
    return result;
}

char *sb_dup(const StringBuilder *sb)
{
    if (!sb || !sb->buf) return NULL;
    char *result = malloc(sb->len + 1);
    if (result) {
        memcpy(result, sb->buf, sb->len + 1);
    }
    return result;
}
