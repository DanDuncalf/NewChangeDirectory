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

/* ================================================================ internal helpers  */

static void *sb_xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "NCD: out of memory in string builder\n");
        exit(1);
    }
    return p;
}

static void *sb_xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (!p) {
        fprintf(stderr, "NCD: out of memory in string builder\n");
        exit(1);
    }
    return p;
}

/* ================================================================ lifecycle  */

void sb_init(StringBuilder *sb)
{
    if (!sb) return;
    sb->cap = SB_INITIAL_CAP;
    sb->buf = sb_xmalloc(sb->cap);
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
    
    sb->buf = sb_xrealloc(sb->buf, new_cap);
    sb->cap = new_cap;
    return true;
}

/* ================================================================ append operations  */

void sb_appendn(StringBuilder *sb, const char *s, size_t n)
{
    if (!sb || !s || n == 0) return;
    if (!sb_ensure_cap(sb, sb->len + n + 1)) return;
    
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

void sb_append(StringBuilder *sb, const char *s)
{
    if (!sb || !s) return;
    sb_appendn(sb, s, strlen(s));
}

void sb_appendc(StringBuilder *sb, char c)
{
    if (!sb) return;
    if (!sb_ensure_cap(sb, sb->len + 2)) return;
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
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

void sb_append_json_str(StringBuilder *sb, const char *s)
{
    if (!sb) return;
    sb_appendc(sb, '"');
    for (const char *p = s; p && *p; p++) {
        switch (*p) {
            case '"':  sb_append(sb, "\\\""); break;
            case '\\': sb_append(sb, "\\\\"); break;
            case '\n': sb_append(sb, "\\n");  break;
            case '\r': sb_append(sb, "\\r");  break;
            case '\t': sb_append(sb, "\\t");  break;
            case '\b': sb_append(sb, "\\b");  break;
            case '\f': sb_append(sb, "\\f");  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    sb_appendf(sb, "\\u%04x", (unsigned char)*p);
                } else {
                    sb_appendc(sb, *p);
                }
        }
    }
    sb_appendc(sb, '"');
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
