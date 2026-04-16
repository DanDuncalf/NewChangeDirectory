#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int wildcard_match(const char *pattern, const char *text) {
    const char *p = pattern;
    const char *t = text;
    const char *star_p = NULL;
    const char *star_t = NULL;
    
    printf("  wildcard_match: pattern='%s' text='%s'\n", pattern, text);
    
    while (*t) {
        if (*p == '*') {
            star_p = p++;
            star_t = t;
            printf("    Found * at pattern pos %d, recording star_t at text pos %d ('%s')\n", 
                   (int)(p-1-pattern), (int)(star_t-text), star_t);
        } else if (*p == '?' || tolower((unsigned char)*p) == tolower((unsigned char)*t)) {
            printf("    Match: '%c' == '%c', advancing both\n", *p, *t);
            p++;
            t++;
        } else if (star_p) {
            printf("    Mismatch: '%c' != '%c', backtracking to star\n", *p, *t);
            p = star_p + 1;
            t = ++star_t;
            printf("    Now at p='%s', t='%s'\n", p, t);
        } else {
            printf("    Mismatch and no star, returning FALSE\n");
            return 0;
        }
    }
    
    while (*p == '*') p++;
    printf("  End of text, remaining pattern: '%s', result: %s\n", p, *p == '\0' ? "TRUE" : "FALSE");
    return *p == '\0';
}

int main() {
    printf("Testing: pattern='*\\build\\*.obj' text='build\\test.obj'\n");
    int result = wildcard_match("*\\build\\*.obj", "build\\test.obj");
    printf("Result: %s\n\n", result ? "MATCH" : "NO MATCH");
    
    printf("Testing: pattern='*\\build\\*.obj' text='project\\build\\main.obj'\n");
    result = wildcard_match("*\\build\\*.obj", "project\\build\\main.obj");
    printf("Result: %s\n", result ? "MATCH" : "NO MATCH");
    
    return 0;
}
