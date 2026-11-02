/* test_load_integration.c -- Integration tests: real NCD binaries against an
 * isolated data directory (LOCALAPPDATA / XDG_DATA_HOME), NCD_TEST_MODE=1. */
#include "test_framework.h"
#include "agent_test_common.h"
#include "service_test_common.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if !NCD_PLATFORM_WINDOWS
#include <unistd.h>
#endif

typedef struct {
    char base[NCD_MAX_PATH];   /* LOCALAPPDATA / XDG_DATA_HOME value */
    char data[NCD_MAX_PATH];   /* base\NCD or base/ncd */
    char tmp[NCD_MAX_PATH];    /* TEMP/TMP or XDG_RUNTIME_DIR for result file */
    char fs[NCD_MAX_PATH];     /* scratch filesystem tree for rescans */
} ItEnv;

static inline void it_mkdir(const char *p) { AGENT_MKDIR(p, 0755); }

static inline void it_env_init(ItEnv *e, const char *suffix)
{
    agent_get_temp_dir(e->base, sizeof(e->base), suffix);
    agent_rm_rf(e->base);
    it_mkdir(e->base);
    agent_build_ncd_dir(e->data, sizeof(e->data), e->base);
    it_mkdir(e->data);
#if NCD_PLATFORM_WINDOWS
    snprintf(e->tmp, sizeof(e->tmp), "%s%stmp", e->base, NCD_PATH_SEP);
#else
    /* XDG_RUNTIME_DIR holds the service's AF_UNIX socket: it must be on a
     * native Linux filesystem (WSL2 /mnt/<x> mounts do not support sockets)
     * and short enough for sun_path (108 bytes).  The harness's TEMP is a
     * long /mnt/c/... path, so use /tmp. */
    snprintf(e->tmp, sizeof(e->tmp), "/tmp/ncd_it_%s_%d", suffix, (int)getpid());
    agent_rm_rf(e->tmp);
#endif
    it_mkdir(e->tmp);
    /* The scratch tree lives under the test's cwd, not TEMP: --agent:query
     * loads only the cwd drive's database, so rescanned dirs must be on the
     * same drive (agent_test_drive_letter()) to be visible to it. */
    char cwd[NCD_MAX_PATH];
    platform_get_current_dir(cwd, sizeof(cwd));
    snprintf(e->fs, sizeof(e->fs), "%s%sit_fs_%s", cwd, NCD_PATH_SEP, suffix);
    agent_rm_rf(e->fs);
    it_mkdir(e->fs);
}

static inline void it_env_cleanup(ItEnv *e)
{
    agent_rm_rf(e->fs);
    agent_rm_rf(e->tmp);   /* separate dir on Linux; inside base on Windows */
    agent_rm_rf(e->base);
}

static inline void it_db_path(const ItEnv *e, char letter, char *buf, size_t n)
{
    agent_build_db_path(buf, n, e->data, letter);
}

/* Point this test process at the isolated env (for in-process service starts). */
static inline void it_set_process_env(const ItEnv *e)
{
#if NCD_PLATFORM_WINDOWS
    _putenv_s("LOCALAPPDATA", e->base);
    _putenv_s("NCD_TEST_MODE", "1");
#else
    setenv("XDG_DATA_HOME", e->base, 1);
    setenv("XDG_RUNTIME_DIR", e->tmp, 1);   /* IPC socket dir must match it_run_ncd */
    setenv("NCD_TEST_MODE", "1", 1);
#endif
}

/* Run the real client with raw args; captures stdout+stderr. Returns exit code.
 * stdin is empty so an unexpected interactive prompt fails fast instead of hanging. */
static inline int it_run_ncd(const ItEnv *e, const char *extra_env, const char *args,
                      char *out, size_t out_size)
{
    const char *exe = agent_find_exe();
    if (!exe) { snprintf(out, out_size, "EXE_NOT_FOUND"); return -1; }
    char cmd[NCD_MAX_PATH * 6];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd),
        "set \"LOCALAPPDATA=%s\" && set \"TEMP=%s\" && set \"TMP=%s\" && set NCD_TEST_MODE=1 && %s%s\"%s\" %s <nul 2>&1",
        e->base, e->tmp, e->tmp, extra_env ? extra_env : "", extra_env ? " && " : "", exe, args);
#else
    snprintf(cmd, sizeof(cmd),
        "XDG_DATA_HOME='%s' XDG_RUNTIME_DIR='%s' NCD_TEST_MODE=1 %s '%s' %s </dev/null 2>&1",
        e->base, e->tmp, extra_env ? extra_env : "", exe, args);
#endif
    FILE *fp = AGENT_POPEN(cmd, "r");
    if (!fp) return -1;
    size_t n = fread(out, 1, out_size - 1, fp);
    out[n] = '\0';
    return AGENT_PCLOSE(fp);
}

/* Read the wrapper result file the client writes (ncd_result.bat / .sh). */
static inline bool it_read_result(const ItEnv *e, char *buf, size_t n)
{
    char path[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(path, sizeof(path), "%s\\ncd_result.bat", e->tmp);
#else
    snprintf(path, sizeof(path), "%s/ncd_result.sh", e->tmp);
#endif
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t r = fread(buf, 1, n - 1, f);
    buf[r] = '\0';
    fclose(f);
    return true;
}

/* ---------------------------------------------------------------- Task 1 */

static uint64_t ref_crc64(const uint8_t *b, size_t len)
{
    static uint64_t t[256]; static int ready = 0;
    if (!ready) {
        for (int i = 0; i < 256; i++) {
            uint64_t c = (uint64_t)i;
            for (int j = 0; j < 8; j++) c = (c & 1) ? (c >> 1) ^ 0x42F0E1EBA9EA3693ULL : c >> 1;
            t[i] = c;
        }
        ready = 1;
    }
    if (len == 0) return 0;
    uint64_t c = ~0ULL;
    for (size_t i = 0; i < len; i++) c = t[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

/* The on-disk checksum is unchanged: a file saved by the new code carries the
 * historical byte-wise CRC, and the real binary accepts it. */
TEST(it_crc_db_file_checksum_unchanged_and_loads) {
    ItEnv e; it_env_init(&e, "it_crc");
    char letter = agent_test_drive_letter();
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, letter);
    int a = db_add_dir(drv, "crcroot", -1, false, false);
    db_add_dir(drv, "crc_unique_leaf_7731", a, false, false);
    char path[NCD_MAX_PATH]; it_db_path(&e, letter, path, sizeof(path));
    ASSERT_TRUE(db_save_binary_single(db, 0, path));
    db_free(db);

    FILE *f = fopen(path, "rb"); ASSERT_NOT_NULL(f);
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    uint8_t *blob = (uint8_t *)malloc((size_t)sz);
    ASSERT_TRUE(fread(blob, 1, (size_t)sz, f) == (size_t)sz); fclose(f);
    size_t off = (sz >= 2 && blob[0] == 0xFF && blob[1] == 0xFE) ? 2 : 0;
    BinFileHdr hdr; memcpy(&hdr, blob + off, sizeof(hdr));
    ASSERT_TRUE(hdr.checksum == ref_crc64(blob + off + sizeof(hdr), (size_t)sz - off - sizeof(hdr)));
    free(blob);

    char out[8192];
    it_run_ncd(&e, NULL, "crc_unique_leaf_7731", out, sizeof(out));
    char res[4096]; ASSERT_TRUE(it_read_result(&e, res, sizeof(res)));
    ASSERT_STR_CONTAINS(res, "crc_unique_leaf_7731");
    it_env_cleanup(&e);
    return 0;
}

/* ---------------------------------------------------------------- Task 2 */

static int it_raw_add(DriveData *drv, const char *name, int parent)
{
    size_t nl = strlen(name) + 1;
    if (drv->name_pool_len + nl > drv->name_pool_cap) {
        drv->name_pool_cap = (drv->name_pool_cap ? drv->name_pool_cap * 2 : 4096) + nl;
        drv->name_pool = realloc(drv->name_pool, drv->name_pool_cap);
    }
    memcpy(drv->name_pool + drv->name_pool_len, name, nl);
    if (drv->dir_count >= drv->dir_capacity) {
        drv->dir_capacity = drv->dir_capacity ? drv->dir_capacity * 2 : 64;
        drv->dirs = realloc(drv->dirs, sizeof(DirEntry) * (size_t)drv->dir_capacity);
    }
    DirEntry *d = &drv->dirs[drv->dir_count];
    memset(d, 0, sizeof(*d));
    d->parent = parent;
    d->name_off = (uint32_t)drv->name_pool_len;
    drv->name_pool_len += nl;
    return drv->dir_count++;
}

/* Case-variant siblings + 200K filler dirs. */
static void it_write_case_variant_db(const ItEnv *e, char letter, int filler)
{
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, letter);
    int root = it_raw_add(drv, "itroot", -1);
    int up   = it_raw_add(drv, "Foo", root);
    int lo   = it_raw_add(drv, "foo", root);
    it_raw_add(drv, "it_upper_child_q8", up);
    int lc   = it_raw_add(drv, "it_lower_child_w4", lo);
    it_raw_add(drv, "it_deep_leaf_k5", lc);
    char name[32];
    for (int i = 0; i < filler; i++) {
        snprintf(name, sizeof(name), "f%07d", i);
        it_raw_add(drv, name, i < 64 ? root : 6 + (i / 64) - 1);
    }
    char path[NCD_MAX_PATH]; it_db_path(e, letter, path, sizeof(path));
    db_save_binary_single(db, 0, path);
    db_free(db);
}

TEST(it_standalone_merge_keeps_parents_and_is_fast) {
    ItEnv e; it_env_init(&e, "it_merge");
    it_write_case_variant_db(&e, agent_test_drive_letter(), 200000);
    char out[8192];
    time_t t0 = time(NULL);
    it_run_ncd(&e, NULL, "it_deep_leaf_k5", out, sizeof(out));
    double secs = difftime(time(NULL), t0);
    char res[4096]; ASSERT_TRUE(it_read_result(&e, res, sizeof(res)));
    ASSERT_STR_CONTAINS(res, "it_lower_child_w4");
    ASSERT_STR_CONTAINS(res, "it_deep_leaf_k5");
    printf("  standalone search over 200K dirs: %.0f s\n", secs);
    ASSERT_TRUE(secs <= 5.0);   /* old O(n^2) merge: ~15 s */
    it_env_cleanup(&e);
    return 0;
}

static bool it_wait_service_ready(const ItEnv *e, int timeout_s)
{
    char out[4096];
    for (int i = 0; i < timeout_s * 4; i++) {
        it_run_ncd(e, NULL, "--agent:check --service-status", out, sizeof(out));
        if (strstr(out, "READY")) return true;
        platform_sleep_ms(250);
    }
    return false;
}

TEST(it_service_merge_keeps_parents_and_is_fast) {
    ItEnv e; it_env_init(&e, "it_svc");
    it_write_case_variant_db(&e, agent_test_drive_letter(), 200000);
    it_set_process_env(&e);
    agent_kill_any_service();
    char out[8192];
    run_service_command("start", out, sizeof(out));
    time_t t0 = time(NULL);
    bool ready = it_wait_service_ready(&e, 20);
    double secs = difftime(time(NULL), t0);
    printf("  service READY after %.0f s\n", secs);
    ASSERT_TRUE(ready);
    ASSERT_TRUE(secs <= 5.0);   /* old: ~15 s for 200K dirs */

    it_run_ncd(&e, NULL, "it_deep_leaf_k5", out, sizeof(out));
    char res[4096]; ASSERT_TRUE(it_read_result(&e, res, sizeof(res)));
    ASSERT_STR_CONTAINS(res, "it_lower_child_w4");
    ASSERT_STR_CONTAINS(res, "it_deep_leaf_k5");

    run_service_command("stop", out, sizeof(out));
    ensure_service_stopped();
    it_env_cleanup(&e);
    return 0;
}

/* ---------------------------------------------------------------- Task 3 */

static void it_mk(const ItEnv *e, const char *rel)
{
    char p[NCD_MAX_PATH];
    snprintf(p, sizeof(p), "%s%s%s", e->fs, NCD_PATH_SEP, rel);
    it_mkdir(p);
}

static int it_count(const char *hay, const char *needle)
{
    int n = 0;
    for (const char *p = strstr(hay, needle); p; p = strstr(p + 1, needle)) n++;
    return n;
}

TEST(it_subdir_rescan_twice_no_duplicates) {
    ItEnv e; it_env_init(&e, "it_rescan");
    it_mk(&e, "proj_r1");
    it_mk(&e, "proj_r1" NCD_PATH_SEP "rescan_unique_leaf_m9");
    char args[NCD_MAX_PATH * 2], out[65536];
    snprintf(args, sizeof(args), "-r \"%s\"", e.fs);
    it_run_ncd(&e, NULL, args, out, sizeof(out));
    snprintf(args, sizeof(args), "-r \"%s%sproj_r1\"", e.fs, NCD_PATH_SEP);
    it_run_ncd(&e, NULL, args, out, sizeof(out));
    it_run_ncd(&e, NULL, args, out, sizeof(out));
    it_run_ncd(&e, NULL, "--agent:query rescan_unique_leaf_m9 --json --limit 50", out, sizeof(out));
    /* JSON echoes the query, so count result objects rather than the name. */
    ASSERT_STR_CONTAINS(out, "rescan_unique_leaf_m9");
    ASSERT_EQ_INT(1, it_count(out, "\"path\":"));
    it_env_cleanup(&e);
    return 0;
}

/* ---------------------------------------------------------------- Task 4 */

/* Write an initial ncd.metadata into the isolated data dir so CLI commands
 * like -x do not stop at the interactive first-run setup prompt. */
static bool it_write_initial_metadata(const ItEnv *e)
{
    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s%sncd.metadata", e->data, NCD_PATH_SEP);
    NcdMetadata *meta = db_metadata_create();
    db_metadata_set_override(path);
    bool ok = db_metadata_save(meta);
    db_metadata_set_override(NULL);
    db_metadata_free(meta);
    return ok;
}

TEST(it_rescan_honors_large_exclusion_list) {
    ItEnv e; it_env_init(&e, "it_excl");
    it_mk(&e, "keep_dir_e1");
    it_mk(&e, "keep_dir_e1" NCD_PATH_SEP "keep_leaf_e2");
    it_mk(&e, "skip_lit_e3");
    it_mk(&e, "skip_lit_e3" NCD_PATH_SEP "skip_inner_e4");
    it_mk(&e, "wild_e5.tmpx");
    ASSERT_TRUE(it_write_initial_metadata(&e));
    char out[65536], args[NCD_MAX_PATH * 2];
    /* Over the hash threshold: one rooted literal (exercises the hash sets;
     * rooted because the unified rules match bare non-root literals only at
     * the drive root), one wildcard (linear part), and 8 fillers. */
    snprintf(args, sizeof(args), "-x \"%s%s%sskip_lit_e3\"",
             NCD_PATH_SEP, db_exclusion_rel_path(e.fs), NCD_PATH_SEP);
    it_run_ncd(&e, NULL, args, out, sizeof(out));
    ASSERT_STR_CONTAINS(out, "Added exclusion");
    it_run_ncd(&e, NULL, "-x \"*.tmpx\"", out, sizeof(out));
    ASSERT_STR_CONTAINS(out, "Added exclusion");
    for (int i = 0; i < 8; i++) {
        snprintf(args, sizeof(args), "-x filler_excl_%d", i);
        it_run_ncd(&e, NULL, args, out, sizeof(out));
    }
    snprintf(args, sizeof(args), "-r \"%s\"", e.fs);
    it_run_ncd(&e, NULL, args, out, sizeof(out));

    it_run_ncd(&e, NULL, "--agent:query keep_leaf_e2 --json", out, sizeof(out));
    ASSERT_EQ_INT(1, it_count(out, "\"path\":"));
    it_run_ncd(&e, NULL, "--agent:query skip_inner_e4 --json", out, sizeof(out));
    ASSERT_EQ_INT(0, it_count(out, "\"path\":"));   /* child of excluded literal */
    it_run_ncd(&e, NULL, "--agent:query skip_lit_e3 --json", out, sizeof(out));
    ASSERT_EQ_INT(0, it_count(out, "\"path\":"));
    it_run_ncd(&e, NULL, "--agent:query wild_e5 --json", out, sizeof(out));
    ASSERT_EQ_INT(0, it_count(out, "\"path\":"));   /* wildcard *.tmpx */
    it_env_cleanup(&e);
    return 0;
}

TEST_MAIN(
    agent_kill_any_service();
    RUN_TEST(it_crc_db_file_checksum_unchanged_and_loads);
    RUN_TEST(it_standalone_merge_keeps_parents_and_is_fast);
    RUN_TEST(it_service_merge_keeps_parents_and_is_fast);
    RUN_TEST(it_subdir_rescan_twice_no_duplicates);
    RUN_TEST(it_rescan_honors_large_exclusion_list);
)
