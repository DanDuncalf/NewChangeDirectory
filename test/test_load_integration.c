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
    snprintf(e->tmp, sizeof(e->tmp), "%s%stmp", e->base, NCD_PATH_SEP);
    it_mkdir(e->tmp);
    snprintf(e->fs, sizeof(e->fs), "%s%sfs", e->base, NCD_PATH_SEP);
    it_mkdir(e->fs);
}

static inline void it_env_cleanup(ItEnv *e) { agent_rm_rf(e->base); }

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
    setenv("NCD_TEST_MODE", "1", 1);
#endif
}

/* Run the real client with raw args; captures stdout+stderr. Returns exit code. */
static inline int it_run_ncd(const ItEnv *e, const char *extra_env, const char *args,
                      char *out, size_t out_size)
{
    const char *exe = agent_find_exe();
    if (!exe) { snprintf(out, out_size, "EXE_NOT_FOUND"); return -1; }
    char cmd[NCD_MAX_PATH * 6];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd),
        "set \"LOCALAPPDATA=%s\" && set \"TEMP=%s\" && set \"TMP=%s\" && set NCD_TEST_MODE=1 && %s%s\"%s\" %s 2>&1",
        e->base, e->tmp, e->tmp, extra_env ? extra_env : "", extra_env ? " && " : "", exe, args);
#else
    snprintf(cmd, sizeof(cmd),
        "XDG_DATA_HOME='%s' XDG_RUNTIME_DIR='%s' NCD_TEST_MODE=1 %s '%s' %s 2>&1",
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

TEST_MAIN(
    agent_kill_any_service();
    RUN_TEST(it_crc_db_file_checksum_unchanged_and_loads);
)
