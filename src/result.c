/*
 * result.c  --  Result output functions for NCD
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "ncd.h"
#include "result.h"
#include "platform.h"

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#endif

/* External console output functions (from main.c) */
extern void ncd_print(const char *s);
extern void ncd_println(const char *s);
extern void ncd_printf(const char *fmt, ...);

/* ============================================================= result output */

void write_result(bool ok, const char *drive, const char *path,
                  const char *message)
{
    char tmp_dir[MAX_PATH] = {0};
    if (!platform_get_temp_path(tmp_dir, sizeof(tmp_dir))) {
        ncd_println("NCD: could not resolve temp path.");
        fprintf(stderr, "NCD ERROR: platform_get_temp_path failed\n");
        return;
    }

    char result_path[MAX_PATH];
    snprintf(result_path, sizeof(result_path), "%s%s", tmp_dir, NCD_RESULT_FILE);

    NCD_DEBUG_LOG("NCD DEBUG: Writing result to: %s (ok=%d)\n", result_path, ok);

    FILE *f = fopen(result_path, "w");
    if (!f) {
        ncd_printf("NCD: could not write result file: %s\r\n", result_path);
        fprintf(stderr, "NCD ERROR: fopen failed for %s\n", result_path);
        /* Try to provide more details about the failure */
        fprintf(stderr, "NCD ERROR: tmp_dir=%s, NCD_RESULT_FILE=%s\n", 
                tmp_dir, NCD_RESULT_FILE);
        fflush(stderr);
        return;
    }
    NCD_DEBUG_LOG("NCD DEBUG: fopen succeeded\n");

    char safe_drive[64];
    char safe_path[NCD_MAX_PATH * 2];
    char safe_msg[1024];

    const char *src_drive = (ok && drive) ? drive : "";
    const char *src_path  = (ok && path)  ? path  : "";
    const char *src_msg   = message ? message : "";

#if NCD_PLATFORM_WINDOWS
    /*
     * Escape for: @set "VAR=value"
     * Security: Reject control characters (except tab), replace quotes,
     * and filter out percent signs to prevent batch variable expansion attacks.
     */
    size_t j = 0;
    for (size_t i = 0; src_drive[i] && j + 2 < sizeof(safe_drive); i++) {
        char c = src_drive[i];
        /* Reject control characters except common whitespace */
        if ((unsigned char)c < 32 && c != '\t') {
            c = '_';  /* Replace control chars with underscore */
        }
        /* Replace dangerous characters for batch files */
        if (c == '"') c = '\'';      /* Quotes could break out of string */
        if (c == '%') c = '_';       /* Prevent %VAR% expansion */
        if (c == '!') c = '_';       /* Prevent delayed expansion */
        if (c == '\r' || c == '\n') c = ' ';
        safe_drive[j++] = c;
    }
    safe_drive[j] = '\0';

    j = 0;
    for (size_t i = 0; src_path[i] && j + 2 < sizeof(safe_path); i++) {
        char c = src_path[i];
        /* Reject control characters except common whitespace */
        if ((unsigned char)c < 32 && c != '\t') {
            c = '_';
        }
        /* Replace dangerous characters */
        if (c == '"') c = '\'';
        if (c == '%') c = '_';
        if (c == '!') c = '_';
        if (c == '\r' || c == '\n') c = ' ';
        safe_path[j++] = c;
    }
    safe_path[j] = '\0';

    j = 0;
    for (size_t i = 0; src_msg[i] && j + 2 < sizeof(safe_msg); i++) {
        char c = src_msg[i];
        /* Reject control characters except common whitespace */
        if ((unsigned char)c < 32 && c != '\t') {
            c = '_';
        }
        /* Replace dangerous characters */
        if (c == '"') c = '\'';
        if (c == '%') c = '_';
        if (c == '!') c = '_';
        if (c == '\r' || c == '\n') c = ' ';
        safe_msg[j++] = c;
    }
    safe_msg[j] = '\0';

    fprintf(f, "@set \"NCD_STATUS=%s\"\r\n", ok ? "OK" : "ERROR");
    fprintf(f, "@set \"NCD_DRIVE=%s\"\r\n",  safe_drive);
    fprintf(f, "@set \"NCD_PATH=%s\"\r\n",   safe_path);
    fprintf(f, "@set \"NCD_MESSAGE=%s\"\r\n", safe_msg);
#else
    /*
     * Escape for: export VAR='value'
     * Security: Reject control characters and shell metacharacters
     * that could be used for command injection.
     */
    size_t j = 0;
    for (size_t i = 0; src_drive[i] && j + 2 < sizeof(safe_drive); i++) {
        char c = src_drive[i];
        /* Reject control characters */
        if ((unsigned char)c < 32) c = '_';
        /* Reject shell metacharacters */
        if (c == '\'' || c == '"' || c == '$' || c == '`' || 
            c == '\\' || c == '|' || c == '&' || c == ';' ||
            c == '<' || c == '>' || c == '(' || c == ')' ||
            c == '{' || c == '}' || c == '*' || c == '?') {
            c = '_';
        }
        safe_drive[j++] = c;
    }
    safe_drive[j] = '\0';

    j = 0;
    for (size_t i = 0; src_path[i] && j + 2 < sizeof(safe_path); i++) {
        char c = src_path[i];
        /* Reject control characters */
        if ((unsigned char)c < 32) c = '_';
        /* Reject shell metacharacters */
        if (c == '\'' || c == '"' || c == '$' || c == '`' || 
            c == '\\' || c == '|' || c == '&' || c == ';' ||
            c == '<' || c == '>' || c == '(' || c == ')' ||
            c == '{' || c == '}' || c == '*' || c == '?') {
            c = '_';
        }
        safe_path[j++] = c;
    }
    safe_path[j] = '\0';

    j = 0;
    for (size_t i = 0; src_msg[i] && j + 2 < sizeof(safe_msg); i++) {
        char c = src_msg[i];
        /* Reject control characters */
        if ((unsigned char)c < 32) c = '_';
        /* Reject shell metacharacters */
        if (c == '\'' || c == '"' || c == '$' || c == '`' || 
            c == '\\' || c == '|' || c == '&' || c == ';' ||
            c == '<' || c == '>' || c == '(' || c == ')' ||
            c == '{' || c == '}' || c == '*' || c == '?') {
            c = '_';
        }
        safe_msg[j++] = c;
    }
    safe_msg[j] = '\0';

    fprintf(f, "NCD_STATUS='%s'\n", ok ? "OK" : "ERROR");
    fprintf(f, "NCD_DRIVE='%s'\n",  safe_drive);
    fprintf(f, "NCD_PATH='%s'\n",   safe_path);
    fprintf(f, "NCD_MESSAGE='%s'\n", safe_msg);
#endif
    /* Flush to ensure data is written before fclose */
    fflush(f);
    
    if (fclose(f) != 0) {
        NCD_DEBUG_LOG("NCD ERROR: fclose failed for %s\n", result_path);
    } else {
        NCD_DEBUG_LOG("NCD DEBUG: Result file written successfully\n");
    }
}

void result_error(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    write_result(false, "", "", msg);
    ncd_printf("NCD: %s\r\n", msg);
}

void result_cancel(void)
{
    write_result(false, "", "", "NCD: Cancelled.");
}

void result_ok(const char *full_path, char drive_letter)
{
    char msg[MAX_PATH + 32];
    snprintf(msg, sizeof(msg), "Changed to %s", full_path);
#if NCD_PLATFORM_WINDOWS
    char drive[3] = { drive_letter, ':', '\0' };
    write_result(true, drive, full_path, msg);
#else
    (void)drive_letter;
    write_result(true, "", full_path, msg);
#endif
}

/* ============================================================= heuristic helpers */

void heur_sanitize(const char *src, char *dst, size_t dst_size, bool to_lower)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;

    size_t len = strlen(src);
    size_t a = 0;
    while (a < len && isspace((unsigned char)src[a])) a++;
    size_t b = len;
    while (b > a && isspace((unsigned char)src[b - 1])) b--;

    size_t j = 0;
    for (size_t i = a; i < b && j + 1 < dst_size; i++) {
        char c = src[i];
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
        if (to_lower) c = (char)tolower((unsigned char)c);
        dst[j++] = c;
    }
    dst[j] = '\0';
}

void heur_promote_match(NcdMatch *matches, int count, const char *preferred_path)
{
    if (!matches || count <= 1 || !preferred_path || !preferred_path[0]) return;
    for (int i = 0; i < count; i++) {
#if NCD_PLATFORM_WINDOWS
        if (_stricmp(matches[i].full_path, preferred_path) == 0) {
#else
        if (strcasecmp(matches[i].full_path, preferred_path) == 0) {
#endif
            if (i == 0) return;
            NcdMatch tmp = matches[0];
            matches[0] = matches[i];
            matches[i] = tmp;
            return;
        }
    }
}

/* ================================================================ frequency-based sort */

/*
 * heur_sort_by_frequency  --  Sort matches by frequency using index-based quicksort
 *
 * matches: Array of NcdMatch to sort (in-place reordering)
 * count: Number of matches
 *
 * Sorts by frequency descending (highest frequency first).
 * Uses index array for fast comparison - avoids comparing large NcdMatch structures.
 * Final in-place reorder uses cycle detection to avoid O(n^2) swaps.
 */
void heur_sort_by_frequency(NcdMatch *matches, int count)
{
    if (!matches || count <= 1) return;
    
    /* Allocate index array for indirect sorting */
    int *indices = (int *)malloc((size_t)count * sizeof(int));
    if (!indices) return;
    
    /* Initialize indices: indices[i] = i */
    for (int i = 0; i < count; i++) {
        indices[i] = i;
    }
    
    /* Sort indices by frequency (descending) using iterative quicksort.
     * Comparison: matches[indices[a]].frequency > matches[indices[b]].frequency */
    int left = 0, right = count - 1;
    int stack[64];
    int top = -1;
    
    stack[++top] = left;
    stack[++top] = right;
    
    while (top >= 0) {
        right = stack[top--];
        left = stack[top--];
        
        while (left < right) {
            int mid = left + (right - left) / 2;
            uint32_t pivot = matches[indices[mid]].frequency;
            
            /* Move pivot to end */
            int tmp_idx = indices[mid];
            indices[mid] = indices[right];
            indices[right] = tmp_idx;
            
            int i = left - 1;
            for (int j = left; j < right; j++) {
                if (matches[indices[j]].frequency > pivot) {
                    i++;
                    tmp_idx = indices[i];
                    indices[i] = indices[j];
                    indices[j] = tmp_idx;
                }
            }
            
            tmp_idx = indices[i + 1];
            indices[i + 1] = indices[right];
            indices[right] = tmp_idx;
            
            int pivot_idx = i + 1;
            
            if (top < 64 - 2) {
                stack[++top] = pivot_idx + 1;
                stack[++top] = right;
            }
            
            right = pivot_idx - 1;
        }
    }
    
    /* Now reorder matches in-place using cycle detection.
     * We want matches[i] = matches[indices[i]], but we need to do it in-place.
     * The permutation is: final_matches[i] = original_matches[indices[i]]
     * We follow cycles in the permutation. */
    bool *visited = (bool *)calloc((size_t)count, sizeof(bool));
    if (!visited) {
        free(indices);
        return;
    }
    
    for (int i = 0; i < count; i++) {
        if (visited[i]) continue;
        
        int j = i;
        NcdMatch save = matches[i];
        
        while (!visited[j]) {
            visited[j] = true;
            int k = indices[j];
            
            if (k != j) {
                matches[j] = matches[k];
                j = k;
            } else {
                matches[j] = save;
                break;
            }
        }
    }
    
    free(visited);
    free(indices);
}
