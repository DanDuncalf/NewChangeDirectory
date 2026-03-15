/*
 * database.c  --  Load, save, and manipulate the NCD directory database
 *
 * Storage format (compact JSON, written on one line):
 * {
 *   "version":  1,
 *   "lastScan": <unix-timestamp>,
 *   "options":  { "showHidden": false, "showSystem": false },
 *   "drives": [
 *     {
 *       "letter": "C",
 *       "label":  "Windows",
 *       "type":   3,
 *       "dirs": [
 *         { "parent": -1, "name": "Users",     "h": 0, "s": 0 },
 *         { "parent":  0, "name": "Scott",     "h": 0, "s": 0 },
 *         { "parent":  1, "name": "Downloads", "h": 0, "s": 0 }
 *       ]
 *     }
 *   ]
 * }
 *
 * Key design decisions vs. the previous version:
 *
 *  1. No JSON tree.
 *     The old code built a full JsonNode/JsonPair tree in memory (~1.7 M
 *     heap allocations for a 100 K-directory database), then walked it once
 *     and discarded it.  db_load now uses a zero-allocation streaming reader
 *     (Rdr) that fills DirEntry arrays directly.  db_save writes JSON text
 *     straight into a growing string buffer with no intermediate tree.
 *
 *  2. String pool.
 *     DirEntry no longer carries a char name[256] field.  All names for a
 *     given drive are packed into a single heap buffer (DriveData.name_pool)
 *     and each DirEntry stores only a uint32_t byte offset.  sizeof(DirEntry)
 *     drops from 268 bytes to 12 bytes.
 *
 *  3. No redundant id field.
 *     The old DirEntry.id was always equal to the array index.  It has been
 *     removed from both the struct and the saved file.  Old database files
 *     that still contain an "id" key are silently tolerated during load.
 */

#include "database.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ================================================================ helpers */

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "NCD: out of memory\n"); exit(1); }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (!p) { fprintf(stderr, "NCD: out of memory\n"); exit(1); }
    return p;
}

/* ======================================================= string builder   */
/*
 * Used by db_save to assemble the JSON output without any intermediate tree.
 * Buffer grows by doubling so appends are amortized O(1).
 */
typedef struct { char *buf; size_t len; size_t cap; } SB;

static void sb_init(SB *sb)
{
    sb->cap = 4096;
    sb->buf = xmalloc(sb->cap);
    sb->buf[0] = '\0';
    sb->len = 0;
}

static void sb_grow(SB *sb, size_t need)
{
    if (sb->len + need + 1 <= sb->cap) return;
    while (sb->len + need + 1 > sb->cap) sb->cap *= 2;
    sb->buf = xrealloc(sb->buf, sb->cap);
}

static void sb_appendc(SB *sb, char c)
{
    sb_grow(sb, 1);
    sb->buf[sb->len++] = c;
    sb->buf[sb->len]   = '\0';
}

static void sb_append(SB *sb, const char *s)
{
    size_t n = strlen(s);
    sb_grow(sb, n);
    memcpy(sb->buf + sb->len, s, n + 1);
    sb->len += n;
}

static void sb_appendf(SB *sb, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    sb_append(sb, tmp);
}

/* Write a JSON string literal with necessary escape sequences. */
static void sb_append_json_str(SB *sb, const char *s)
{
    sb_appendc(sb, '"');
    for (; *s; s++) {
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
}

/* =============================================================== lifecycle */

NcdDatabase *db_create(void)
{
    NcdDatabase *db = xmalloc(sizeof(NcdDatabase));
    memset(db, 0, sizeof(NcdDatabase));
    db->version = NCD_VERSION;
    return db;
}

void db_free(NcdDatabase *db)
{
    if (!db) return;
    if (db->is_blob) {
        /*
         * Binary-blob load: dirs[] and name_pool pointers inside each
         * DriveData point directly into blob_buf -- do NOT free them
         * individually.  Just free the two separately-allocated blocks.
         */
        free(db->drives);
        free(db->blob_buf);
    } else {
        for (int i = 0; i < db->drive_count; i++) {
            free(db->drives[i].dirs);
            free(db->drives[i].name_pool);
        }
        free(db->drives);
    }
    free(db);
}

/* ============================================================ path helper */

#if NCD_PLATFORM_WINDOWS

char *db_default_path(char *buf, size_t buf_size)
{
    char local_app[MAX_PATH];
    if (!platform_get_env("LOCALAPPDATA", local_app, sizeof(local_app))) return NULL;

    int written = snprintf(buf, buf_size, "%s\\%s", local_app, NCD_DB_FILENAME);
    if (written < 0 || (size_t)written >= buf_size) return NULL;
    return buf;
}

char *db_drive_path(char letter, char *buf, size_t buf_size)
{
    char local_app[MAX_PATH];
    if (!platform_get_env("LOCALAPPDATA", local_app, sizeof(local_app))) return NULL;

    /* Ensure %LOCALAPPDATA%\NCD\ exists (no-op if already present). */
    char dir[MAX_PATH];
    snprintf(dir, sizeof(dir), "%s\\NCD", local_app);
    platform_create_dir(dir);

    int w = snprintf(buf, buf_size, "%s\\NCD\\ncd_%c.database",
                     local_app, toupper((unsigned char)letter));
    return (w > 0 && (size_t)w < buf_size) ? buf : NULL;
}

#else /* NCD_PLATFORM_LINUX */

/*
 * Return the XDG data home base directory: $XDG_DATA_HOME if set,
 * otherwise $HOME/.local/share.  Result is NUL-terminated in buf.
 */
static bool xdg_data_home(char *buf, size_t buf_size)
{
    if (platform_get_env("XDG_DATA_HOME", buf, buf_size) && buf[0])
        return true;
    char home[MAX_PATH];
    if (!platform_get_env("HOME", home, sizeof(home)) || !home[0])
        return false;
    int w = snprintf(buf, buf_size, "%s/.local/share", home);
    return w > 0 && (size_t)w < buf_size;
}

char *db_default_path(char *buf, size_t buf_size)
{
    char base[MAX_PATH];
    if (!xdg_data_home(base, sizeof(base))) return NULL;

    /* Ensure ~/.local/share/ncd/ exists. */
    char dir[MAX_PATH + 8];
    snprintf(dir, sizeof(dir), "%s/ncd", base);
    platform_create_dir(dir);

    int w = snprintf(buf, buf_size, "%s/ncd/%s", base, NCD_DB_FILENAME);
    if (w < 0 || (size_t)w >= buf_size) return NULL;
    return buf;
}

/*
 * On Linux, `letter` is a mount-index byte (0x01..0x1A).
 * The per-mount database file is named  ncd_XX.database  where XX is the
 * two-digit hex value of the byte.
 */
char *db_drive_path(char letter, char *buf, size_t buf_size)
{
    char base[MAX_PATH];
    if (!xdg_data_home(base, sizeof(base))) return NULL;

    char dir[MAX_PATH + 8];
    snprintf(dir, sizeof(dir), "%s/ncd", base);
    platform_create_dir(dir);

    int w = snprintf(buf, buf_size, "%s/ncd/ncd_%02x.database",
                     base, (unsigned char)letter);
    return (w > 0 && (size_t)w < buf_size) ? buf : NULL;
}

#endif /* NCD_PLATFORM_WINDOWS / NCD_PLATFORM_LINUX */

bool db_save_binary_single(const NcdDatabase *db, int drive_idx,
                            const char *path)
{
    if (drive_idx < 0 || drive_idx >= db->drive_count) return false;
    const DriveData *drv = &db->drives[drive_idx];

    /* ---- compute total output size (same layout as db_save_binary) ---- */
    size_t dirs_bytes = (size_t)drv->dir_count * sizeof(DirEntry);
    size_t used       = dirs_bytes + drv->name_pool_len;
    size_t data_padded = (used + 3) & ~(size_t)3;
    size_t total = sizeof(BinFileHdr) + sizeof(BinDriveHdr) + data_padded;

    char *buf = xmalloc(total);
    size_t pos = 0;

    /* ---- BinFileHdr (drive_count == 1) ---- */
    BinFileHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = NCD_BIN_MAGIC;
    hdr.version     = NCD_BIN_VERSION;
    hdr.show_hidden = db->default_show_hidden ? 1 : 0;
    hdr.show_system = db->default_show_system ? 1 : 0;
    hdr.last_scan   = (int64_t)db->last_scan;
    hdr.drive_count = 1;
    memcpy(buf + pos, &hdr, sizeof(hdr));
    pos += sizeof(hdr);

    /* ---- BinDriveHdr ---- */
    BinDriveHdr dhdr;
    memset(&dhdr, 0, sizeof(dhdr));
    dhdr.letter    = (uint8_t)drv->letter;
    dhdr.type      = drv->type;
    memcpy(dhdr.label, drv->label, sizeof(dhdr.label));
    dhdr.dir_count = (uint32_t)drv->dir_count;
    dhdr.pool_size = (uint32_t)drv->name_pool_len;
    memcpy(buf + pos, &dhdr, sizeof(dhdr));
    pos += sizeof(dhdr);

    /* ---- DirEntry[] + name_pool + padding ---- */
    memcpy(buf + pos, drv->dirs, dirs_bytes);
    pos += dirs_bytes;
    memcpy(buf + pos, drv->name_pool, drv->name_pool_len);
    pos += drv->name_pool_len;
    size_t padded = (pos + 3) & ~(size_t)3;
    while (pos < padded) buf[pos++] = 0;

    /* ---- atomic write (.tmp -> rename) ---- */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) { free(buf); return false; }

    size_t written = fwrite(buf, 1, pos, f);
    fclose(f);
    free(buf);

    if (written != pos) { platform_delete_file(tmp_path); return false; }

    char old_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.old", path);
    platform_delete_file(old_path);
    platform_move_file(path, old_path);
    if (!platform_move_file(tmp_path, path)) {
        platform_move_file(old_path, path);
        return false;
    }
    return true;
}

/* ============================================================= drive mgmt */

DriveData *db_add_drive(NcdDatabase *db, char letter)
{
    if (db->drive_count >= db->drive_capacity) {
        db->drive_capacity = db->drive_capacity ? db->drive_capacity * 2 : 8;
        db->drives = xrealloc(db->drives, sizeof(DriveData) * db->drive_capacity);
    }
    DriveData *drv = &db->drives[db->drive_count++];
    memset(drv, 0, sizeof(DriveData));
    drv->letter = (char)toupper((unsigned char)letter);
    return drv;
}

DriveData *db_find_drive(NcdDatabase *db, char letter)
{
    letter = (char)toupper((unsigned char)letter);
    for (int i = 0; i < db->drive_count; i++)
        if (db->drives[i].letter == letter)
            return &db->drives[i];
    return NULL;
}

/* ============================================================== dir mgmt  */

int db_add_dir(DriveData *drv, const char *name, int32_t parent,
               bool is_hidden, bool is_system)
{
    /* ---- append name to the per-drive string pool ---- */
    size_t nlen = strlen(name) + 1;          /* include NUL terminator */

    if (drv->name_pool_len + nlen > drv->name_pool_cap) {
        drv->name_pool_cap = drv->name_pool_cap ? drv->name_pool_cap * 2 : 4096;
        while (drv->name_pool_len + nlen > drv->name_pool_cap)
            drv->name_pool_cap *= 2;         /* handles unusually long names */
        drv->name_pool = xrealloc(drv->name_pool, drv->name_pool_cap);
    }
    uint32_t name_off = (uint32_t)drv->name_pool_len;
    memcpy(drv->name_pool + drv->name_pool_len, name, nlen);
    drv->name_pool_len += nlen;

    /* ---- append DirEntry ---- */
    if (drv->dir_count >= drv->dir_capacity) {
        drv->dir_capacity = drv->dir_capacity ? drv->dir_capacity * 2 : 64;
        drv->dirs = xrealloc(drv->dirs,
                             sizeof(DirEntry) * (size_t)drv->dir_capacity);
    }
    int id      = drv->dir_count;
    DirEntry *e = &drv->dirs[id];
    e->parent    = parent;
    e->name_off  = name_off;
    e->is_hidden = is_hidden ? 1 : 0;
    e->is_system = is_system ? 1 : 0;
    drv->dir_count++;
    return id;
}

/* =========================================================== path helper  */

char *db_full_path(const DriveData *drv, int dir_index,
                   char *buf, size_t buf_size)
{
#if NCD_PLATFORM_WINDOWS
    if (dir_index < 0 || dir_index >= drv->dir_count) {
        snprintf(buf, buf_size, "%c:\\", drv->letter);
        return buf;
    }

    /* Walk up the parent chain collecting component pointers. */
    const int MAX_DEPTH = 128;
    const char *parts[128];
    int depth = 0;
    int cur   = dir_index;
    while (cur >= 0 && depth < MAX_DEPTH) {
        parts[depth++] = drv->name_pool + drv->dirs[cur].name_off;
        cur = drv->dirs[cur].parent;
    }

    /* Build "X:\comp[n-1]\...\comp[0]" */
    size_t pos = 0;
    pos += (size_t)snprintf(buf + pos, buf_size - pos, "%c:\\", drv->letter);
    for (int i = depth - 1; i >= 0 && pos < buf_size - 1; i--) {
        if (i < depth - 1) {
            buf[pos++] = '\\';
            buf[pos]   = '\0';
        }
        size_t part_len = strlen(parts[i]);
        if (pos + part_len >= buf_size) part_len = buf_size - pos - 1;
        memcpy(buf + pos, parts[i], part_len);
        pos += part_len;
        buf[pos] = '\0';
    }
    return buf;

#else /* NCD_PLATFORM_LINUX */
    /*
     * On Linux the mount root path is stored in drv->label.
     * dir_index == -1 means the mount root itself.
     */
    if (dir_index < 0 || dir_index >= drv->dir_count) {
        snprintf(buf, buf_size, "%s", drv->label);
        return buf;
    }

    /* Walk up the parent chain collecting component pointers. */
    const int MAX_DEPTH = 128;
    const char *parts[128];
    int depth = 0;
    int cur   = dir_index;
    while (cur >= 0 && depth < MAX_DEPTH) {
        parts[depth++] = drv->name_pool + drv->dirs[cur].name_off;
        cur = drv->dirs[cur].parent;
    }

    /* Start with the mount root (e.g. "/" or "/home"). */
    size_t pos = 0;
    size_t root_len = strlen(drv->label);
    if (root_len >= buf_size) root_len = buf_size - 1;
    memcpy(buf, drv->label, root_len);
    pos = root_len;
    buf[pos] = '\0';

    /* Remove trailing slash from root so we don't double up. */
    if (pos > 0 && buf[pos - 1] == '/')
        pos--;

    /* Append "/comp[n-1]/.../comp[0]" */
    for (int i = depth - 1; i >= 0 && pos < buf_size - 1; i--) {
        buf[pos++] = '/';
        buf[pos]   = '\0';
        size_t part_len = strlen(parts[i]);
        if (pos + part_len >= buf_size) part_len = buf_size - pos - 1;
        memcpy(buf + pos, parts[i], part_len);
        pos += part_len;
        buf[pos] = '\0';
    }
    return buf;
#endif
}

/* ================================================================== load  */

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf)  { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

/* --------------------------------------------------------------- mini-Rdr
 *
 * A zero-allocation streaming JSON reader.  Walks the text forward-only,
 * filling our structs directly without building any intermediate tree.
 *
 * Design principles:
 *  - Fields may appear in any order (handles old "id" fields gracefully).
 *  - Unknown keys are skipped via rdr_skip_value.
 *  - r.err is set on any parse failure; callers check it.
 */

typedef struct { const char *p; int err; } Rdr;

static void rdr_skip_ws(Rdr *r)
{
    while (isspace((unsigned char)*r->p)) r->p++;
}

/* Consume expected character c (after optional whitespace). */
static void rdr_eat(Rdr *r, char c)
{
    rdr_skip_ws(r);
    if (*r->p == c) r->p++;
    else r->err = 1;
}

/* Read a quoted JSON string into buf with full escape handling. */
static bool rdr_read_str(Rdr *r, char *buf, size_t cap)
{
    rdr_skip_ws(r);
    if (*r->p != '"') { r->err = 1; return false; }
    r->p++;

    char  *out = buf;
    char  *lim = buf + cap - 1;

    while (*r->p && *r->p != '"') {
        char c;
        if (*r->p == '\\') {
            r->p++;
            switch (*r->p) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'u':  /* 4-hex Unicode: skip 4 hex digits, store '?' */
                    { int i; for (i = 0; i < 4 && r->p[1]; i++) r->p++; }
                    c = '?'; break;
                default:   c = *r->p; break;
            }
        } else {
            c = *r->p;
        }
        if (out < lim) *out++ = c;
        r->p++;
    }
    *out = '\0';

    if (*r->p == '"') r->p++;
    else r->err = 1;
    return !r->err;
}

/* Read a signed 64-bit integer. */
static long long rdr_read_ll(Rdr *r)
{
    rdr_skip_ws(r);
    char *end;
    long long v = strtoll(r->p, &end, 10);
    if (end == r->p) r->err = 1;
    else r->p = end;
    return v;
}

/* Read true/false boolean. */
static bool rdr_read_bool(Rdr *r)
{
    rdr_skip_ws(r);
    if (strncmp(r->p, "true",  4) == 0) { r->p += 4; return true;  }
    if (strncmp(r->p, "false", 5) == 0) { r->p += 5; return false; }
    r->err = 1;
    return false;
}

/* Skip any JSON value at the current position (handles nesting). */
static void rdr_skip_value(Rdr *r)
{
    rdr_skip_ws(r);
    char c = *r->p;

    if (c == '"') {                       /* string */
        r->p++;
        while (*r->p && *r->p != '"') {
            if (*r->p == '\\' && r->p[1]) r->p++;
            r->p++;
        }
        if (*r->p == '"') r->p++;

    } else if (c == '{' || c == '[') {    /* object or array */
        int depth = 1;
        r->p++;
        while (*r->p && depth > 0) {
            if (*r->p == '"') {           /* skip string to avoid brace confusion */
                r->p++;
                while (*r->p && *r->p != '"') {
                    if (*r->p == '\\' && r->p[1]) r->p++;
                    r->p++;
                }
                if (*r->p) r->p++;
            } else if (*r->p == '{' || *r->p == '[') {
                depth++; r->p++;
            } else if (*r->p == '}' || *r->p == ']') {
                depth--; r->p++;
            } else {
                r->p++;
            }
        }

    } else {                              /* number / bool / null */
        while (*r->p && *r->p != ',' && *r->p != '}' && *r->p != ']')
            r->p++;
    }
}

/*
 * Try to read the next key from an object and consume ':'.
 * Call immediately after '{' or after consuming a comma.
 * Returns true if a key was read; false at '}' or on error.
 */
static bool rdr_next_key(Rdr *r, char *key, size_t cap)
{
    rdr_skip_ws(r);
    if (*r->p == '}' || r->err) return false;
    if (!rdr_read_str(r, key, cap)) return false;
    rdr_eat(r, ':');
    return !r->err;
}

/*
 * After reading a value inside an object or array, consume ',' if present.
 * The caller is responsible for checking '}' / ']' when this returns false.
 */
static bool rdr_eat_comma(Rdr *r)
{
    rdr_skip_ws(r);
    if (*r->p == ',') { r->p++; return true; }
    return false;
}

/* ---- shrink a drive's arrays to exact size after all entries are loaded  */
static void drv_shrink_to_fit(DriveData *drv)
{
    if (drv->dir_count > 0 && drv->dir_count < drv->dir_capacity) {
        drv->dirs = xrealloc(drv->dirs,
                             sizeof(DirEntry) * (size_t)drv->dir_count);
        drv->dir_capacity = drv->dir_count;
    }
    if (drv->name_pool_len > 0 && drv->name_pool_len < drv->name_pool_cap) {
        drv->name_pool     = xrealloc(drv->name_pool, drv->name_pool_len);
        drv->name_pool_cap = drv->name_pool_len;
    }
}

NcdDatabase *db_load(const char *path)
{
    char *text = read_file(path);
    if (!text) return NULL;

    Rdr r = { text, 0 };
    NcdDatabase *db = db_create();
    strncpy(db->db_path, path, NCD_MAX_PATH - 1);

    char key[128];

    rdr_eat(&r, '{');

    while (!r.err && rdr_next_key(&r, key, sizeof(key))) {

        /* ------ top-level fields ------ */
        if (strcmp(key, "version") == 0) {
            db->version = (int)rdr_read_ll(&r);

        } else if (strcmp(key, "lastScan") == 0) {
            db->last_scan = (time_t)rdr_read_ll(&r);

        } else if (strcmp(key, "options") == 0) {
            rdr_eat(&r, '{');
            while (!r.err && rdr_next_key(&r, key, sizeof(key))) {
                if      (strcmp(key, "showHidden") == 0)
                    db->default_show_hidden = rdr_read_bool(&r);
                else if (strcmp(key, "showSystem") == 0)
                    db->default_show_system = rdr_read_bool(&r);
                else
                    rdr_skip_value(&r);
                rdr_eat_comma(&r);
            }
            rdr_eat(&r, '}');

        } else if (strcmp(key, "drives") == 0) {
            rdr_eat(&r, '[');
            rdr_skip_ws(&r);

            while (!r.err && *r.p == '{') {
                DriveData *drv = NULL;

                rdr_eat(&r, '{');
                while (!r.err && rdr_next_key(&r, key, sizeof(key))) {

                    if (strcmp(key, "letter") == 0) {
                        char lbuf[8] = {0};
                        rdr_read_str(&r, lbuf, sizeof(lbuf));
                        if (lbuf[0]) drv = db_add_drive(db, lbuf[0]);

                    } else if (strcmp(key, "label") == 0) {
                        char lbuf[64] = {0};
                        rdr_read_str(&r, lbuf, sizeof(lbuf));
                        if (drv) snprintf(drv->label, sizeof(drv->label), "%s", lbuf);

                    } else if (strcmp(key, "type") == 0) {
                        UINT t = (UINT)rdr_read_ll(&r);
                        if (drv) drv->type = t;

                    } else if (strcmp(key, "dirs") == 0) {
                        rdr_eat(&r, '[');
                        rdr_skip_ws(&r);

                        while (!r.err && drv && *r.p == '{') {
                            int32_t parent   = -1;
                            char    name[NCD_MAX_NAME];
                            uint8_t h        = 0;
                            uint8_t s        = 0;
                            bool    got_name = false;
                            name[0] = '\0';

                            rdr_eat(&r, '{');
                            while (!r.err && rdr_next_key(&r, key, sizeof(key))) {
                                /* "id" is silently skipped (backward compat) */
                                if      (strcmp(key, "parent") == 0)
                                    parent = (int32_t)rdr_read_ll(&r);
                                else if (strcmp(key, "name") == 0) {
                                    rdr_read_str(&r, name, sizeof(name));
                                    got_name = true;
                                }
                                else if (strcmp(key, "h") == 0)
                                    h = (uint8_t)rdr_read_ll(&r);
                                else if (strcmp(key, "s") == 0)
                                    s = (uint8_t)rdr_read_ll(&r);
                                else
                                    rdr_skip_value(&r);
                                rdr_eat_comma(&r);
                            }
                            rdr_eat(&r, '}');

                            if (!r.err && got_name)
                                db_add_dir(drv, name, parent, h, s);

                            rdr_skip_ws(&r);
                            if (*r.p == ',') { r.p++; rdr_skip_ws(&r); }
                        }
                        rdr_eat(&r, ']');

                        /* Shrink to exact size -- no wasted capacity */
                        if (drv) drv_shrink_to_fit(drv);

                    } else {
                        rdr_skip_value(&r);   /* unknown drive key */
                    }

                    rdr_eat_comma(&r);
                }
                rdr_eat(&r, '}');

                rdr_skip_ws(&r);
                if (*r.p == ',') { r.p++; rdr_skip_ws(&r); }
            }
            rdr_eat(&r, ']');

        } else {
            rdr_skip_value(&r);               /* unknown root key */
        }

        rdr_eat_comma(&r);
    }
    rdr_eat(&r, '}');

    free(text);

    if (r.err) {
        db_free(db);
        return NULL;
    }
    return db;
}

/* ================================================================== save  */

bool db_save(const NcdDatabase *db, const char *path)
{
    SB sb;
    sb_init(&sb);

    /* root object open */
    sb_appendf(&sb, "{\"version\":%d,\"lastScan\":%lld,",
               NCD_VERSION, (long long)db->last_scan);

    sb_appendf(&sb, "\"options\":{\"showHidden\":%s,\"showSystem\":%s},",
               db->default_show_hidden ? "true" : "false",
               db->default_show_system ? "true" : "false");

    sb_append(&sb, "\"drives\":[");

    for (int di = 0; di < db->drive_count; di++) {
        const DriveData *drv = &db->drives[di];
        if (di) sb_appendc(&sb, ',');

        sb_appendf(&sb, "{\"letter\":\"%c\",\"label\":", drv->letter);
        sb_append_json_str(&sb, drv->label);
        sb_appendf(&sb, ",\"type\":%u,\"dirs\":[", drv->type);

        for (int ei = 0; ei < drv->dir_count; ei++) {
            const DirEntry *e    = &drv->dirs[ei];
            const char     *name = drv->name_pool + e->name_off;
            if (ei) sb_appendc(&sb, ',');
            sb_appendf(&sb, "{\"parent\":%d,\"name\":", e->parent);
            sb_append_json_str(&sb, name);
            sb_appendf(&sb, ",\"h\":%d,\"s\":%d}", e->is_hidden, e->is_system);
        }

        sb_append(&sb, "]}");
    }

    sb_append(&sb, "]}");   /* close drives array and root object */

    /* Write to .tmp first, then atomic rename */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) { free(sb.buf); return false; }

    size_t written = fwrite(sb.buf, 1, sb.len, f);
    fclose(f);
    bool ok = (written == sb.len);
    free(sb.buf);

    if (!ok) { platform_delete_file(tmp_path); return false; }

    /* Rotate: current -> .old, .tmp -> current */
    char old_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.old", path);
    platform_delete_file(old_path);
    platform_move_file(path, old_path);                 /* ok if path didn't exist */
    if (!platform_move_file(tmp_path, path)) {
        platform_move_file(old_path, path);             /* restore on failure */
        return false;
    }
    return true;
}

/* ============================================================ binary save */

/*
 * File layout written by db_save_binary():
 *
 *   [BinFileHdr : 24 bytes]
 *   [BinDriveHdr × drive_count : 80 bytes each]
 *   For each drive (same order):
 *     [DirEntry × dir_count : 12 bytes each]  -- 4-byte aligned
 *     [name_pool : pool_size bytes]
 *     [0–3 zero padding bytes to restore 4-byte alignment]
 *
 * All integer fields are in host byte order (little-endian on x86-64).
 */

/* Compile-time sanity checks on the on-disk struct sizes. */
_Static_assert(sizeof(BinFileHdr)  == 24, "BinFileHdr size changed");
_Static_assert(sizeof(BinDriveHdr) == 80, "BinDriveHdr size changed");
_Static_assert(sizeof(DirEntry)    == 12, "DirEntry size changed");

bool db_save_binary(const NcdDatabase *db, const char *path)
{
    /* ---- compute total output size ---- */
    size_t total = sizeof(BinFileHdr)
                 + (size_t)db->drive_count * sizeof(BinDriveHdr);

    for (int i = 0; i < db->drive_count; i++) {
        const DriveData *drv = &db->drives[i];
        size_t used = (size_t)drv->dir_count * sizeof(DirEntry)
                    + drv->name_pool_len;
        total += (used + 3) & ~(size_t)3;   /* pad to 4-byte boundary */
    }

    char *buf = xmalloc(total);
    size_t pos = 0;

    /* ---- BinFileHdr ---- */
    BinFileHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic       = NCD_BIN_MAGIC;
    hdr.version     = NCD_BIN_VERSION;
    hdr.show_hidden = db->default_show_hidden ? 1 : 0;
    hdr.show_system = db->default_show_system ? 1 : 0;
    hdr.last_scan   = (int64_t)db->last_scan;
    hdr.drive_count = (uint32_t)db->drive_count;
    memcpy(buf + pos, &hdr, sizeof(hdr));
    pos += sizeof(hdr);

    /* ---- BinDriveHdr array ---- */
    for (int i = 0; i < db->drive_count; i++) {
        const DriveData *drv = &db->drives[i];
        BinDriveHdr dhdr;
        memset(&dhdr, 0, sizeof(dhdr));
        dhdr.letter    = (uint8_t)drv->letter;
        dhdr.type      = drv->type;
        memcpy(dhdr.label, drv->label, sizeof(dhdr.label));
        dhdr.dir_count = (uint32_t)drv->dir_count;
        dhdr.pool_size = (uint32_t)drv->name_pool_len;
        memcpy(buf + pos, &dhdr, sizeof(dhdr));
        pos += sizeof(dhdr);
    }

    /* ---- data sections (DirEntry[] + name_pool per drive) ---- */
    for (int i = 0; i < db->drive_count; i++) {
        const DriveData *drv = &db->drives[i];

        size_t dirs_bytes = (size_t)drv->dir_count * sizeof(DirEntry);
        memcpy(buf + pos, drv->dirs, dirs_bytes);
        pos += dirs_bytes;

        memcpy(buf + pos, drv->name_pool, drv->name_pool_len);
        pos += drv->name_pool_len;

        /* Zero-pad to next 4-byte boundary */
        size_t padded = (pos + 3) & ~(size_t)3;
        while (pos < padded) buf[pos++] = 0;
    }

    /* ---- atomic write (same pattern as db_save) ---- */
    char tmp_path[MAX_PATH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) { free(buf); return false; }

    size_t written = fwrite(buf, 1, pos, f);
    fclose(f);
    free(buf);

    if (written != pos) { platform_delete_file(tmp_path); return false; }

    char old_path[MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s.old", path);
    platform_delete_file(old_path);
    platform_move_file(path, old_path);
    if (!platform_move_file(tmp_path, path)) {
        platform_move_file(old_path, path);
        return false;
    }
    return true;
}

/* ============================================================ binary load */

NcdDatabase *db_load_binary(const char *path)
{
    /* ---- read entire file into one heap buffer ---- */
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    if (sz < (long)sizeof(BinFileHdr)) { fclose(f); return NULL; }

    char *blob = malloc((size_t)sz);
    if (!blob) { fclose(f); return NULL; }

    size_t rd = fread(blob, 1, (size_t)sz, f);
    fclose(f);

    if ((long)rd != sz) { free(blob); return NULL; }

    /* ---- validate header ---- */
    BinFileHdr hdr;
    memcpy(&hdr, blob, sizeof(hdr));

    if (hdr.magic   != NCD_BIN_MAGIC  ||
        hdr.version != NCD_BIN_VERSION ||
        (long)(sizeof(BinFileHdr) + (size_t)hdr.drive_count * sizeof(BinDriveHdr)) > sz) {
        free(blob);
        return NULL;
    }

    /* ---- build NcdDatabase from the blob ---- */
    NcdDatabase *db = db_create();
    strncpy(db->db_path, path, NCD_MAX_PATH - 1);
    db->default_show_hidden = hdr.show_hidden != 0;
    db->default_show_system = hdr.show_system != 0;
    db->last_scan           = (time_t)hdr.last_scan;
    db->blob_buf            = blob;
    db->is_blob             = true;

    /* Pre-allocate the DriveData array (separate from the blob). */
    if (hdr.drive_count > 0) {
        db->drives = xmalloc(sizeof(DriveData) * hdr.drive_count);
        memset(db->drives, 0, sizeof(DriveData) * hdr.drive_count);
        db->drive_count    = (int)hdr.drive_count;
        db->drive_capacity = (int)hdr.drive_count;
    }

    /*
     * Walk the BinDriveHdr array and the interleaved data sections.
     * p  -- cursor through the BinDriveHdr entries
     * dp -- cursor through the data sections (DirEntry[] + pool per drive)
     *
     * The DriveData.dirs and DriveData.name_pool fields are set as pointers
     * directly into the blob.  The blob owns the memory; db_free() releases
     * it via blob_buf when is_blob is set.
     *
     * Casting char* → DirEntry*: valid because malloc() returns maximally
     * aligned memory and all our offsets within the blob are multiples of 4,
     * satisfying DirEntry's 4-byte alignment requirement.
     */
    const char *p  = blob + sizeof(BinFileHdr);
    const char *dp = blob + sizeof(BinFileHdr)
                          + (size_t)hdr.drive_count * sizeof(BinDriveHdr);

    for (uint32_t i = 0; i < hdr.drive_count; i++) {
        BinDriveHdr dhdr;
        memcpy(&dhdr, p, sizeof(dhdr));
        p += sizeof(dhdr);

        /* Bounds check: ensure DirEntry[] + pool fit within the blob. */
        size_t used  = (size_t)dhdr.dir_count * sizeof(DirEntry)
                     + dhdr.pool_size;
        size_t padded = (used + 3) & ~(size_t)3;
        if (dp + padded > blob + sz) {
            db_free(db);
            return NULL;
        }

        DriveData *drv    = &db->drives[i];
        drv->letter       = (char)dhdr.letter;
        drv->type         = dhdr.type;
        memcpy(drv->label, dhdr.label, sizeof(drv->label));
        drv->dir_count    = (int)dhdr.dir_count;
        drv->dir_capacity = (int)dhdr.dir_count;
        drv->name_pool_len = (size_t)dhdr.pool_size;
        drv->name_pool_cap = (size_t)dhdr.pool_size;

        /* Point directly into the blob (no copy). */
        drv->dirs      = (DirEntry *)(void *)dp;
        drv->name_pool = (char *)dp + (size_t)dhdr.dir_count * sizeof(DirEntry);

        dp += padded;
    }

    return db;
}

/* ========================================================== auto-detect  */

/*
 * Inspect the first 4 bytes of path to determine the format:
 *   - Starts with NCD_BIN_MAGIC bytes ('N','C','D','B') → binary
 *   - Anything else (e.g. '{')                          → JSON (legacy)
 *
 * If a JSON file is found it is loaded and immediately resaved as binary
 * so that future loads use the faster path.
 */
NcdDatabase *db_load_auto(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    uint8_t sig[4] = {0};
    size_t  rd = fread(sig, 1, sizeof(sig), f);
    fclose(f);
    if (rd < 1) return NULL;

    /* Binary magic: 'N'=0x4E, 'C'=0x43, 'D'=0x44, 'B'=0x42 */
    if (rd == 4 &&
        sig[0] == 0x4E && sig[1] == 0x43 &&
        sig[2] == 0x44 && sig[3] == 0x42) {
        return db_load_binary(path);
    }

    /* JSON (legacy) -- load then silently convert for next run */
    NcdDatabase *db = db_load(path);
    if (db) {
        db_save_binary(db, path);   /* best-effort; ignore failure */
    }
    return db;
}
