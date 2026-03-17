/*
 * json.c  --  Minimal JSON parser / builder implementation
 */

#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ helpers */

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "NCD: out of memory\n"); exit(1); }
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

/* ================================================================ parsing */

typedef struct {
    const char *p;   /* current position */
    int         err; /* non-zero on error */
} Parser;

static void skip_ws(Parser *ps)
{
    while (*ps->p && isspace((unsigned char)*ps->p))
        ps->p++;
}

static JsonNode *parse_value(Parser *ps);

/* Parse a JSON string starting at '"'; returns malloc'd C string. */
static char *parse_string_raw(Parser *ps)
{
    if (*ps->p != '"') { ps->err = 1; return NULL; }
    ps->p++;

    /* first pass: measure length */
    const char *start = ps->p;
    size_t len = 0;
    for (const char *q = start; *q && *q != '"'; ) {
        if (*q == '\\') { q++; if (*q) q++; }
        else q++;
        len++;
    }

    char *buf = xmalloc(len + 1);
    char *out = buf;

    while (*ps->p && *ps->p != '"') {
        if (*ps->p == '\\') {
            ps->p++;
            switch (*ps->p) {
                case '"':  *out++ = '"';  break;
                case '\\': *out++ = '\\'; break;
                case '/':  *out++ = '/';  break;
                case 'n':  *out++ = '\n'; break;
                case 'r':  *out++ = '\r'; break;
                case 't':  *out++ = '\t'; break;
                case 'b':  *out++ = '\b'; break;
                case 'f':  *out++ = '\f'; break;
                case 'u': {
                    /* 4-hex-digit Unicode – store as '?' for simplicity */
                    int i; for (i = 0; i < 4 && ps->p[1]; i++) ps->p++;
                    *out++ = '?';
                    break;
                }
                default:   *out++ = *ps->p; break;
            }
            ps->p++;
        } else {
            *out++ = *ps->p++;
        }
    }
    *out = '\0';

    if (*ps->p == '"') ps->p++;
    else ps->err = 1;

    return buf;
}

static JsonNode *parse_string(Parser *ps)
{
    char *s = parse_string_raw(ps);
    if (!s) return NULL;
    JsonNode *n = json_make_str(s);
    free(s);
    return n;
}

static JsonNode *parse_number(Parser *ps)
{
    char *end;
    long long v = strtoll(ps->p, &end, 10);
    if (end == ps->p) { ps->err = 1; return NULL; }
    ps->p = end;
    return json_make_int(v);
}

static JsonNode *parse_array(Parser *ps)
{
    ps->p++; /* skip '[' */
    JsonNode *arr = json_make_arr();
    skip_ws(ps);
    if (*ps->p == ']') { ps->p++; return arr; }
    for (;;) {
        skip_ws(ps);
        JsonNode *item = parse_value(ps);
        if (!item) { ps->err = 1; json_free(arr); return NULL; }
        json_arr_add(arr, item);
        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == ']') { ps->p++; break; }
        ps->err = 1; json_free(arr); return NULL;
    }
    return arr;
}

static JsonNode *parse_object(Parser *ps)
{
    ps->p++; /* skip '{' */
    JsonNode *obj = json_make_obj();
    skip_ws(ps);
    if (*ps->p == '}') { ps->p++; return obj; }
    for (;;) {
        skip_ws(ps);
        if (*ps->p != '"') { ps->err = 1; json_free(obj); return NULL; }
        char *key = parse_string_raw(ps);
        if (!key) { ps->err = 1; json_free(obj); return NULL; }
        skip_ws(ps);
        if (*ps->p != ':') { free(key); ps->err = 1; json_free(obj); return NULL; }
        ps->p++;
        skip_ws(ps);
        JsonNode *val = parse_value(ps);
        if (!val) { free(key); ps->err = 1; json_free(obj); return NULL; }
        json_obj_set(obj, key, val);
        free(key);
        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == '}') { ps->p++; break; }
        ps->err = 1; json_free(obj); return NULL;
    }
    return obj;
}

static JsonNode *parse_value(Parser *ps)
{
    skip_ws(ps);
    char c = *ps->p;
    if (c == '"')  return parse_string(ps);
    if (c == '{')  return parse_object(ps);
    if (c == '[')  return parse_array(ps);
    if (c == '-' || isdigit((unsigned char)c)) return parse_number(ps);
    if (strncmp(ps->p, "true",  4) == 0) { ps->p += 4; return json_make_bool(true);  }
    if (strncmp(ps->p, "false", 5) == 0) { ps->p += 5; return json_make_bool(false); }
    if (strncmp(ps->p, "null",  4) == 0) { ps->p += 4; return json_make_null();      }
    ps->err = 1;
    return NULL;
}

JsonNode *json_parse(const char *text)
{
    if (!text) return NULL;
    Parser ps = { text, 0 };
    JsonNode *root = parse_value(&ps);
    if (ps.err) { json_free(root); return NULL; }
    return root;
}

/* ============================================================= json_free  */

void json_free(JsonNode *node)
{
    if (!node) return;
    switch (node->type) {
        case JSON_STRING:
            free(node->sval);
            break;
        case JSON_ARRAY: {
            JsonNode *cur = node->arr.head;
            while (cur) { JsonNode *nx = cur->next; json_free(cur); cur = nx; }
            break;
        }
        case JSON_OBJECT: {
            JsonPair *cur = node->obj.head;
            while (cur) {
                JsonPair *nx = cur->next;
                free(cur->key);
                json_free(cur->value);
                free(cur);
                cur = nx;
            }
            break;
        }
        default: break;
    }
    free(node);
}

/* ============================================================== accessors */

JsonNode *json_get(const JsonNode *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (JsonPair *p = obj->obj.head; p; p = p->next)
        if (strcmp(p->key, key) == 0) return p->value;
    return NULL;
}

JsonNode *json_at(const JsonNode *arr, int index)
{
    if (!arr || arr->type != JSON_ARRAY) return NULL;
    if (index < 0 || index >= arr->arr.count) return NULL;

    /*
     * Use arr->arr.count as a hard upper bound so a corrupted list with a
     * cycle can never loop forever -- we bail out after at most count steps.
     */
    int       limit = arr->arr.count;
    int       i     = 0;
    JsonNode *cur   = arr->arr.head;

    while (cur && i < limit) {
        if (i == index) return cur;
        cur = cur->next;
        i++;
    }
    return NULL;   /* index out of range or cycle detected */
}

int json_count(const JsonNode *arr)
{
    if (!arr || arr->type != JSON_ARRAY) return 0;
    return arr->arr.count;
}

long long json_int(const JsonNode *node)
{
    if (!node || node->type != JSON_INT) return 0;
    return node->ival;
}

bool json_bool(const JsonNode *node)
{
    if (!node || node->type != JSON_BOOL) return false;
    return node->bval;
}

const char *json_str(const JsonNode *node)
{
    if (!node || node->type != JSON_STRING) return "";
    return node->sval;
}

/* =============================================================== builders */

static JsonNode *new_node(JsonType t)
{
    JsonNode *n = xmalloc(sizeof(JsonNode));
    memset(n, 0, sizeof(JsonNode));
    n->type = t;
    return n;
}

JsonNode *json_make_null(void)   { return new_node(JSON_NULL); }
JsonNode *json_make_bool(bool v) { JsonNode *n = new_node(JSON_BOOL);   n->bval = v;          return n; }
JsonNode *json_make_int(long long v){ JsonNode *n = new_node(JSON_INT); n->ival = v;           return n; }
JsonNode *json_make_arr(void)    { return new_node(JSON_ARRAY);  }
JsonNode *json_make_obj(void)    { return new_node(JSON_OBJECT); }

JsonNode *json_make_str(const char *val)
{
    JsonNode *n = new_node(JSON_STRING);
    n->sval = xstrdup(val ? val : "");
    return n;
}

void json_obj_set(JsonNode *obj, const char *key, JsonNode *val)
{
    if (!obj || obj->type != JSON_OBJECT) return;
    /* overwrite if key already exists */
    for (JsonPair *p = obj->obj.head; p; p = p->next) {
        if (strcmp(p->key, key) == 0) {
            json_free(p->value);
            p->value = val;
            return;
        }
    }
    JsonPair *pair = xmalloc(sizeof(JsonPair));
    pair->key   = xstrdup(key);
    pair->value = val;
    pair->next  = NULL;
    if (obj->obj.tail) obj->obj.tail->next = pair;
    else               obj->obj.head       = pair;
    obj->obj.tail = pair;
}

void json_arr_add(JsonNode *arr, JsonNode *val)
{
    if (!arr || arr->type != JSON_ARRAY) return;
    val->next = NULL;
    if (arr->arr.tail) arr->arr.tail->next = val;
    else               arr->arr.head       = val;
    arr->arr.tail = val;
    arr->arr.count++;
}

/* ============================================================ stringify   */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} SB;   /* string builder */

static void sb_init(SB *sb)  { sb->buf = xmalloc(256); sb->buf[0]='\0'; sb->len=0; sb->cap=256; }
static void sb_grow(SB *sb, size_t need)
{
    if (sb->len + need + 1 <= sb->cap) return;
    while (sb->len + need + 1 > sb->cap) sb->cap *= 2;
    sb->buf = realloc(sb->buf, sb->cap);
    if (!sb->buf) { fprintf(stderr,"NCD: out of memory\n"); exit(1); }
}
static void sb_append(SB *sb, const char *s)
{
    size_t n = strlen(s);
    sb_grow(sb, n);
    memcpy(sb->buf + sb->len, s, n + 1);
    sb->len += n;
}
static void sb_appendc(SB *sb, char c)
{
    sb_grow(sb, 1);
    sb->buf[sb->len++] = c;
    sb->buf[sb->len]   = '\0';
}

static void stringify_node(const JsonNode *node, SB *sb)
{
    if (!node) { sb_append(sb, "null"); return; }
    char tmp[64];
    switch (node->type) {
        case JSON_NULL:   sb_append(sb, "null");  break;
        case JSON_BOOL:   sb_append(sb, node->bval ? "true" : "false"); break;
        case JSON_INT:    snprintf(tmp, sizeof(tmp), "%lld", node->ival);
                          sb_append(sb, tmp); break;
        case JSON_STRING: {
            sb_appendc(sb, '"');
            for (const char *s = node->sval; *s; s++) {
                switch (*s) {
                    case '"':  sb_append(sb, "\\\""); break;
                    case '\\': sb_append(sb, "\\\\"); break;
                    case '\n': sb_append(sb, "\\n");  break;
                    case '\r': sb_append(sb, "\\r");  break;
                    case '\t': sb_append(sb, "\\t");  break;
                    default:   sb_appendc(sb, *s);    break;
                }
            }
            sb_appendc(sb, '"');
            break;
        }
        case JSON_ARRAY: {
            sb_appendc(sb, '[');
            int first = 1;
            for (JsonNode *cur = node->arr.head; cur; cur = cur->next) {
                if (!first) sb_appendc(sb, ',');
                stringify_node(cur, sb);
                first = 0;
            }
            sb_appendc(sb, ']');
            break;
        }
        case JSON_OBJECT: {
            sb_appendc(sb, '{');
            int first = 1;
            for (JsonPair *p = node->obj.head; p; p = p->next) {
                if (!first) sb_appendc(sb, ',');
                sb_appendc(sb, '"');
                sb_append(sb, p->key);
                sb_append(sb, "\":");
                stringify_node(p->value, sb);
                first = 0;
            }
            sb_appendc(sb, '}');
            break;
        }
    }
}

char *json_stringify(const JsonNode *node)
{
    SB sb;
    sb_init(&sb);
    stringify_node(node, &sb);
    return sb.buf;  /* caller must free() */
}
