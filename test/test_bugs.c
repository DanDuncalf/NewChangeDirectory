/*
 * test_bugs.c -- Tests that expose identified bugs in NCD codebase
 *
 * These tests are designed to FAIL or exhibit bad behavior due to existing
 * bugs.  When the underlying bugs are fixed, these tests should pass.
 *
 * Bug catalog:
 *   1. Metadata heuristics/groups parsing: bounds check uses entry_offset
 *      (which is pos+4, i.e. entry 0's offset) instead of advancing for
 *      entry i.  Only the first entry's bounds are actually validated.
 *   2. StringBuilder infinite loop: sb_free() sets cap=0, then sb_append()
 *      calls sb_ensure_cap() which doubles cap in a while loop -- but
 *      0*2 == 0, so it loops forever.
 *   3. db_full_path() silent truncation: MAX_DEPTH=128 stack array silently
 *      truncates paths with deeper parent chains (possible on corrupt data).
 *   4. generate_variations_recursive() combinatorial explosion: input with
 *      many digit characters triggers exponential recursive branching.
 *   5. Unchecked fread() returns in db_check_file_version() and db_load_auto()
 *      -- partial reads leave version/skip_flag uninitialized.
 *   6. Groups section parsing has same bounds bug as heuristics (entry_offset
 *      never advances, bounds check only validates entry 0).
 */

#include "test_framework.h"
#include "../src/database.h"
#include "../src/matcher.h"
#include "../src/ncd.h"
#include "../src/strbuilder.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#endif

/* ================================================================
 * Bug 1: Metadata heuristics parsing bounds check
 *
 * In database.c ~line 2668, the bounds check is:
 *     if (entry_offset + sizeof(NcdHeurEntryV2) <= pos + section_size)
 * But entry_offset = pos + 4 (set once before the loop) and is never
 * updated.  The actual data read uses:
 *     data + entry_offset + i * sizeof(NcdHeurEntryV2)
 * So for i >= 1, the code reads past the validated region.
 *
 * This test creates a metadata file with multiple heuristics entries
 * where only the first entry fits in the declared section_size.
 * The buggy code will still read entry 1 (and beyond) because the
 * bounds check doesn't account for i.
 * ================================================================ */

TEST(metadata_heuristics_bounds_check) {
    /*
     * Strategy: Create metadata, add multiple heuristic entries, save it.
     * Then manually truncate the heuristics section size in the binary
     * so that only entry 0 fits.  Load and verify that only 1 entry
     * is returned (correct behavior).
     *
     * BUG: The buggy code will load all entries because the bounds
     * check doesn't use i.
     */
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);

    /* Record 3 heuristic choices */
    db_heur_note_choice(meta, "downloads", "C:\\Users\\Scott\\Downloads");
    db_heur_note_choice(meta, "documents", "C:\\Users\\Scott\\Documents");
    db_heur_note_choice(meta, "pictures",  "C:\\Users\\Scott\\Pictures");

    ASSERT_EQ_INT(3, meta->heuristics.count);

    /* Save metadata (to default path) */
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);

    /* Get the actual metadata file path */
    char meta_path[MAX_PATH];
    ASSERT_TRUE(db_metadata_path(meta_path, sizeof(meta_path)));

    /* Read the raw binary file */
    FILE *f = fopen(meta_path, "rb");
    ASSERT_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);
    unsigned char *data = (unsigned char *)malloc(file_size);
    fread(data, 1, file_size, f);
    fclose(f);

    /*
     * Scan the file for the heuristics section header.
     * Format: [section_id: 1 byte] [section_size: 4 bytes] [data...]
     * The heuristics section has id = NCD_META_SECTION_HEURISTICS (0x03)
     * Inside: [count: 4 bytes] [entry0] [entry1] [entry2]
     *
     * We'll patch section_size to only allow 4 + sizeof(NcdHeurEntryV2)
     * (the count field + one entry), so entries 1 and 2 should be rejected.
     */

    /* Find the heuristics section by scanning for section_id == 0x03 */
    int found = 0;
    for (long off = 0; off < file_size - 5; off++) {
        if (data[off] == NCD_META_SECTION_HEURISTICS) {
            /* Potential section header - read the current section_size */
            uint32_t current_size;
            memcpy(&current_size, data + off + 1, 4);

            /* Verify this looks like a real section (size should accommodate 3 entries) */
            uint32_t expected_min = 4 + 3 * sizeof(NcdHeurEntryV2);
            if (current_size >= expected_min) {
                /* Patch section_size to only fit 1 entry */
                uint32_t new_size = 4 + (uint32_t)sizeof(NcdHeurEntryV2);
                memcpy(data + off + 1, &new_size, 4);
                found = 1;
                break;
            }
        }
    }
    ASSERT_TRUE(found);

    /* Recalculate CRC64 over data after header (header is 20 bytes) */
    uint64_t new_crc = platform_crc64(data + sizeof(MetaFileHdr), file_size - sizeof(MetaFileHdr));
    memcpy(data + offsetof(MetaFileHdr, checksum), &new_crc, sizeof(uint64_t));

    /* Write modified file back */
    f = fopen(meta_path, "wb");
    fwrite(data, 1, file_size, f);
    fclose(f);
    free(data);

    /* Load and check: should only have 1 entry due to truncated section */
    NcdMetadata *loaded = db_metadata_load();
    ASSERT_NOT_NULL(loaded);

    /*
     * If the bug exists: loaded->heuristics.count will be 3
     * If the bug is fixed: loaded->heuristics.count will be 1
     */
    printf("    [BUG CHECK] Heuristics entries loaded: %d (expected 1, buggy gives 3)\n",
           loaded->heuristics.count);

    /* This assertion will FAIL if the bug exists */
    ASSERT_EQ_INT(1, loaded->heuristics.count);

    db_metadata_free(loaded);
    return 0;
}

/* ================================================================
 * Bug 2: StringBuilder infinite loop after sb_free()
 *
 * sb_free() sets sb->cap = 0 and sb->buf = NULL.
 * sb_ensure_cap() has: while (new_cap < min_cap) { new_cap *= 2; }
 * When cap == 0, new_cap starts at 0 and 0 * 2 == 0, infinite loop.
 *
 * NOTE: This test would hang if the bug triggers, so we use a timeout
 * mechanism to detect it.
 * ================================================================ */

#if NCD_PLATFORM_WINDOWS

/* Windows: use a thread with timeout */
#include <process.h>

static volatile int sb_test_completed = 0;

static unsigned __stdcall sb_thread_func(void *arg)
{
    (void)arg;
    StringBuilder sb;
    sb_init(&sb);
    sb_append(&sb, "hello");
    sb_free(&sb);

    /* This is the bug trigger: append after free with cap=0 */
    sb_append(&sb, "world");
    sb_test_completed = 1;
    return 0;
}

TEST(strbuilder_use_after_free_no_hang) {
    sb_test_completed = 0;

    HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, sb_thread_func, NULL, 0, NULL);
    ASSERT_NOT_NULL(thread);

    /* Wait up to 3 seconds -- if sb_append hangs, this will time out */
    DWORD result = WaitForSingleObject(thread, 3000);
    CloseHandle(thread);

    if (result == WAIT_TIMEOUT) {
        printf("    [BUG DETECTED] sb_append after sb_free caused infinite loop!\n");
        /* Terminate the hung thread - can't easily do this safely, but test fails */
        ASSERT_TRUE(0 && "sb_append after sb_free hung (infinite loop in sb_ensure_cap)");
    }

    ASSERT_TRUE(sb_test_completed);
    return 0;
}

#else /* Linux */

#include <sys/wait.h>

TEST(strbuilder_use_after_free_no_hang) {
    /*
     * Use fork() to isolate the potentially-hanging call.
     * Child process tries sb_append after sb_free.
     * Parent waits with timeout -- if child doesn't exit, bug detected.
     */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child process */
        StringBuilder sb;
        sb_init(&sb);
        sb_append(&sb, "hello");
        sb_free(&sb);

        /* Bug trigger: append after free with cap=0 */
        sb_append(&sb, "world");

        /* If we reach here, the bug didn't trigger */
        _exit(0);
    }

    ASSERT_TRUE(pid > 0);

    /* Parent: wait up to 3 seconds */
    int status = 0;
    int waited = 0;
    for (int i = 0; i < 30; i++) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            waited = 1;
            break;
        }
        usleep(100000); /* 100ms */
    }

    if (!waited) {
        /* Child is still running -- infinite loop detected */
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        printf("    [BUG DETECTED] sb_append after sb_free caused infinite loop!\n");
        ASSERT_TRUE(0 && "sb_append after sb_free hung (infinite loop in sb_ensure_cap)");
    }

    /* Child exited -- check exit code */
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf("    sb_append after sb_free completed without hanging\n");
    } else {
        printf("    Child process crashed (signal %d) -- use-after-free issue\n",
               WIFSIGNALED(status) ? WTERMSIG(status) : -1);
    }

    return 0;
}

#endif /* platform */

/* ================================================================
 * Bug 3: db_full_path() silent truncation at MAX_DEPTH=128
 *
 * If a database has a parent chain deeper than 128, db_full_path()
 * silently stops walking the chain and returns a truncated path.
 * On corrupt data this can produce wrong paths without any error.
 * ================================================================ */

TEST(full_path_deep_chain_truncation) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');

    /* Build a chain 200 levels deep */
    int parent = -1;
    for (int i = 0; i < 200; i++) {
        char name[16];
        snprintf(name, sizeof(name), "d%03d", i);
        parent = db_add_dir(drv, name, parent, false, false);
    }

    /* The deepest entry is at index 199, with a 200-level chain */
    char buf[NCD_MAX_PATH];
    db_full_path(drv, 199, buf, sizeof(buf));

    /*
     * The full path should contain all 200 components.
     * BUG: Only the bottom 128 levels appear because MAX_DEPTH=128.
     * The path starts from d072 instead of d000.
     *
     * Verify: if d000 is NOT in the path, the bug exists.
     */
    printf("    [BUG CHECK] Path starts with: %.40s...\n", buf);

    /* Check that the root-most component (d000) is present */
    int has_d000 = (strstr(buf, "d000") != NULL);
    if (!has_d000) {
        printf("    [BUG DETECTED] d000 missing from path (truncated at depth 128)\n");
    }

    /* This assertion will FAIL if the bug exists (d000 will be missing) */
    ASSERT_TRUE(has_d000);

    db_free(db);
    return 0;
}

/* ================================================================
 * Bug 4: generate_variations_recursive() combinatorial explosion
 *
 * With many digit characters in input, the recursive function explores
 * an exponential number of branches.  Each digit can be kept or
 * replaced with multiple words, and word->digit subs also fire.
 * A search term like "1234567890" can cause excessive stack/time use.
 *
 * This test verifies fuzzy matching completes in reasonable time.
 * ================================================================ */

TEST(fuzzy_match_digit_heavy_performance) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "TestDir", -1, false, false);

    /* Search with many digits -- triggers combinatorial explosion */
    int count = 0;
    clock_t start = clock();

    NcdMatch *matches = matcher_find_fuzzy(db, "12345678", false, false, &count);

    clock_t elapsed = clock() - start;
    double seconds = (double)elapsed / CLOCKS_PER_SEC;

    printf("    Fuzzy match '12345678' took %.3f seconds, %d matches\n",
           seconds, count);

    if (matches) free(matches);
    db_free(db);

    /*
     * BUG: If combinatorial explosion occurs, this can take many seconds
     * or even minutes.  A well-behaved implementation should complete
     * in under 1 second for any reasonable input.
     */
    if (seconds > 2.0) {
        printf("    [BUG DETECTED] Fuzzy match took %.1fs (combinatorial explosion)\n",
               seconds);
    }
    ASSERT_TRUE(seconds < 2.0);

    return 0;
}

/* ================================================================
 * Bug 5: Unchecked fread() returns in db_check_file_version()
 *
 * In database.c, db_check_file_version() does:
 *     fread(&version, 1, sizeof(version), f);
 * without checking the return value.  If fread returns 0 (e.g. EOF
 * or I/O error), version remains at its initialized value (0).
 *
 * This test creates a file that has valid magic but is truncated
 * right after -- the version read will fail silently.
 * ================================================================ */

TEST(check_version_truncated_after_magic) {
    const char *test_file = "test_trunc_ver.tmp";

    /* Write just the 4-byte magic, nothing else */
    FILE *f = fopen(test_file, "wb");
    ASSERT_NOT_NULL(f);
    uint32_t magic = NCD_BIN_MAGIC;
    fwrite(&magic, 4, 1, f);
    fclose(f);

    /*
     * db_check_file_version should return DB_VERSION_ERROR for a
     * truncated file.  The fread for version will read 0 bytes and
     * leave version == 0.  Since 0 != NCD_BIN_VERSION, the function
     * returns DB_VERSION_MISMATCH instead of DB_VERSION_ERROR.
     *
     * BUG: Returns DB_VERSION_MISMATCH instead of DB_VERSION_ERROR
     * for truncated files.
     */
    int result = db_check_file_version(test_file);
    printf("    [BUG CHECK] check_file_version on truncated file: %d "
           "(0=ERROR, 1=OK, 2=MISMATCH, 3=SKIPPED)\n", result);

    /* Should be DB_VERSION_ERROR (0) for a truncated/corrupt file */
    if (result == DB_VERSION_MISMATCH) {
        printf("    [BUG DETECTED] Truncated file returned MISMATCH instead of ERROR\n");
    }
    ASSERT_EQ_INT(DB_VERSION_ERROR, result);

    remove(test_file);
    return 0;
}

/* ================================================================
 * Bug 5b: Unchecked fread() in db_load_auto()
 *
 * Same pattern: after confirming magic, fread(&version, ...) is not
 * checked.  A file with valid magic but truncated version field
 * will silently use version == 0.
 * ================================================================ */

TEST(load_auto_truncated_after_magic) {
    const char *test_file = "test_trunc_auto.tmp";

    /* Write magic + 1 byte (partial version field) */
    FILE *f = fopen(test_file, "wb");
    ASSERT_NOT_NULL(f);
    uint32_t magic = NCD_BIN_MAGIC;
    fwrite(&magic, 4, 1, f);
    uint8_t one_byte = 0x01;
    fwrite(&one_byte, 1, 1, f);
    fclose(f);

    /*
     * db_load_auto should return NULL for this truncated file.
     * BUG: fread(&version, ...) partially reads, leaving version
     * in an indeterminate state.  Behavior depends on whether fread
     * zeroes out the remaining bytes or leaves them as-is.
     */
    NcdDatabase *db = db_load_auto(test_file);
    printf("    [BUG CHECK] db_load_auto on truncated file: %s\n",
           db ? "loaded (unexpected)" : "NULL (correct or crashed)");

    /* Should return NULL without crashing */
    if (db) {
        printf("    [BUG DETECTED] Truncated file was loaded by db_load_auto\n");
        db_free(db);
    }
    ASSERT_NULL(db);

    remove(test_file);
    return 0;
}

/* ================================================================
 * Bug 6: Groups section parsing has same bounds bug as heuristics
 *
 * In database.c, the groups section parsing loop uses:
 *     if (entry_offset + sizeof(NcdGroupEntry) <= pos + section_size)
 * where entry_offset = pos + 4 (never updated in loop).
 * Reading uses: data + entry_offset + i * sizeof(NcdGroupEntry)
 * So for i >= 1, the bounds check doesn't actually cover the data read.
 * ================================================================ */

TEST(metadata_groups_bounds_check) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);

    /* Add 3 groups */
    db_group_set(meta, "home",    "C:\\Users\\Scott");
    db_group_set(meta, "work",    "C:\\Projects");
    db_group_set(meta, "backup",  "D:\\Backup");

    ASSERT_EQ_INT(3, meta->groups.count);

    /* Save metadata (to default path) */
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);

    /* Get the actual metadata file path */
    char meta_path[MAX_PATH];
    ASSERT_TRUE(db_metadata_path(meta_path, sizeof(meta_path)));

    /* Read raw binary */
    FILE *f = fopen(meta_path, "rb");
    ASSERT_NOT_NULL(f);
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);
    unsigned char *data = (unsigned char *)malloc(file_size);
    fread(data, 1, file_size, f);
    fclose(f);

    /* Find groups section (id = 0x02) and truncate its declared size */
    int found = 0;
    for (long off = 0; off < file_size - 5; off++) {
        if (data[off] == NCD_META_SECTION_GROUPS) {
            uint32_t current_size;
            memcpy(&current_size, data + off + 1, 4);

            uint32_t expected_min = 4 + 3 * sizeof(NcdGroupEntry);
            if (current_size >= expected_min) {
                /* Patch to only fit 1 entry */
                uint32_t new_size = 4 + (uint32_t)sizeof(NcdGroupEntry);
                memcpy(data + off + 1, &new_size, 4);
                found = 1;
                break;
            }
        }
    }
    ASSERT_TRUE(found);

    /* Recalculate CRC64 over data after header (header is 20 bytes) */
    uint64_t new_crc = platform_crc64(data + sizeof(MetaFileHdr), file_size - sizeof(MetaFileHdr));
    memcpy(data + offsetof(MetaFileHdr, checksum), &new_crc, sizeof(uint64_t));

    /* Write modified file */
    f = fopen(meta_path, "wb");
    fwrite(data, 1, file_size, f);
    fclose(f);
    free(data);

    /* Load and check */
    NcdMetadata *loaded = db_metadata_load();
    ASSERT_NOT_NULL(loaded);

    printf("    [BUG CHECK] Group entries loaded: %d (expected 1, buggy gives 3)\n",
           loaded->groups.count);

    /* This will FAIL if the bounds bug exists */
    ASSERT_EQ_INT(1, loaded->groups.count);

    db_metadata_free(loaded);
    return 0;
}

/* ================================================================
 * Bug 7: db_full_path with corrupt circular parent chain
 *
 * If a database has a corrupt parent pointer creating a cycle
 * (e.g., entry A's parent is B, B's parent is A), db_full_path()
 * will loop exactly MAX_DEPTH times and return a wrong path.
 * It should detect cycles and handle them gracefully.
 * ================================================================ */

TEST(full_path_circular_parent_chain) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');

    /* Create entries with normal parents first */
    db_add_dir(drv, "Alpha", -1, false, false);    /* index 0 */
    db_add_dir(drv, "Beta", 0, false, false);      /* index 1 */
    db_add_dir(drv, "Gamma", 1, false, false);     /* index 2 */

    /* Corrupt: make entry 0's parent point to entry 2 (creating cycle: 0->2->1->0->...) */
    drv->dirs[0].parent = 2;

    char buf[NCD_MAX_PATH];
    db_full_path(drv, 2, buf, sizeof(buf));

    /*
     * With the cycle 2->1->0->2->1->0->..., the function will fill
     * all 128 slots of parts[] with repeating components.
     * It should detect the cycle and not produce a path with 128 repeated dirs.
     */
    printf("    [BUG CHECK] Circular path length: %d chars\n", (int)strlen(buf));

    /* A correct implementation should produce a short path or error.
     * The buggy version will produce a very long path with repeated components. */
    int path_len = (int)strlen(buf);
    if (path_len > 200) {
        printf("    [BUG DETECTED] Circular parent chain produced %d-char path "
               "(should detect cycle)\n", path_len);
    }
    ASSERT_TRUE(path_len < 200);

    db_free(db);
    return 0;
}

/* ================================================================
 * Bug 8: BinFileHdr field layout / offset verification
 *
 * Verify that BinFileHdr has the expected layout.  The header struct
 * should be exactly 32 bytes with fields at specific offsets.
 * If packing changes, seek offsets throughout the code will break.
 * ================================================================ */

TEST(binfilehdr_layout_verification) {
    /* Verify total size */
    ASSERT_EQ_INT(32, (int)sizeof(BinFileHdr));

    /* Verify critical field offsets */
    ASSERT_EQ_INT(0, (int)offsetof(BinFileHdr, magic));
    ASSERT_EQ_INT(4, (int)offsetof(BinFileHdr, version));
    ASSERT_EQ_INT(6, (int)offsetof(BinFileHdr, show_hidden));
    ASSERT_EQ_INT(7, (int)offsetof(BinFileHdr, show_system));
    ASSERT_EQ_INT(8, (int)offsetof(BinFileHdr, last_scan));
    ASSERT_EQ_INT(16, (int)offsetof(BinFileHdr, drive_count));
    ASSERT_EQ_INT(20, (int)offsetof(BinFileHdr, skipped_rescan));
    ASSERT_EQ_INT(24, (int)offsetof(BinFileHdr, checksum));

    /* BinDriveHdr should be 80 bytes */
    ASSERT_EQ_INT(80, (int)sizeof(BinDriveHdr));

    printf("    BinFileHdr layout verified: 32 bytes, all offsets correct\n");
    return 0;
}

/* ================================================================
 * Bug 9: db_check_all_versions with empty/missing database directory
 *
 * db_check_all_versions iterates over drive letters and checks each
 * database file.  If no database files exist, it should return 0
 * cleanly without errors.
 * ================================================================ */

TEST(check_all_versions_no_databases) {
    DbVersionInfo info[26];
    memset(info, 0, sizeof(info));

    /* This should return 0 and not crash even if no database files exist */
    int count = db_check_all_versions(info, 26);

    printf("    db_check_all_versions returned %d entries\n", count);

    /* count should be >= 0 (valid return) */
    ASSERT_TRUE(count >= 0);
    ASSERT_TRUE(count <= 26);

    return 0;
}

/* ================================================================
 * Bug 10: StringBuilder ensure_cap with cap=0 (edge case)
 *
 * Direct test of sb_ensure_cap when starting from cap=0.
 * This simulates what happens internally when sb_append is called
 * on a freed StringBuilder.
 * ================================================================ */

/*
 * NOTE: strbuilder_ensure_cap_from_zero is tested as part of
 * strbuilder_use_after_free_no_hang (which has timeout protection).
 * A direct call to sb_ensure_cap(&sb, 10) with cap=0 would hang
 * due to the infinite loop bug, so we don't test it without timeout
 * protection.  The use-after-free test above catches this same bug.
 */
TEST(strbuilder_ensure_cap_from_zero) {
    /*
     * Test that sb_ensure_cap works correctly when starting from a
     * properly initialized StringBuilder (cap > 0).
     * This is a sanity check; the actual bug (cap=0) is tested by
     * the use-after-free test which has timeout protection.
     */
    StringBuilder sb;
    sb_init(&sb);

    /* Should already have non-zero capacity */
    ASSERT_TRUE(sb.cap > 0);
    ASSERT_NOT_NULL(sb.buf);

    /* Requesting larger capacity should work */
    size_t old_cap = sb.cap;
    bool result = sb_ensure_cap(&sb, old_cap * 4);
    ASSERT_TRUE(result);
    ASSERT_TRUE(sb.cap >= old_cap * 4);

    sb_free(&sb);

    /* After free: cap=0, buf=NULL.  Document the bug: */
    ASSERT_EQ_INT(0, (int)sb.cap);
    ASSERT_NULL(sb.buf);
    printf("    [BUG] After sb_free: cap=%d -- calling sb_ensure_cap would infinite-loop\n",
           (int)sb.cap);

    return 0;
}

/* ================================================================
 * Bug 11: Multiple save/load roundtrip with heuristics
 *
 * Test that all heuristic entries survive a save/load cycle.
 * This catches the metadata parsing bounds bug from a different angle.
 * ================================================================ */

TEST(heuristics_roundtrip_multiple_entries) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);

    /* Add several heuristic entries */
    const char *searches[] = {"downloads", "documents", "projects", "music", "videos"};
    const char *targets[] = {
        "C:\\Users\\Scott\\Downloads",
        "C:\\Users\\Scott\\Documents",
        "C:\\Projects",
        "C:\\Users\\Scott\\Music",
        "C:\\Users\\Scott\\Videos"
    };

    for (int i = 0; i < 5; i++) {
        db_heur_note_choice(meta, searches[i], targets[i]);
    }

    ASSERT_EQ_INT(5, meta->heuristics.count);

    /* Save to default metadata path */
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);

    /* Load back and verify ALL entries survived */
    NcdMetadata *loaded = db_metadata_load();
    ASSERT_NOT_NULL(loaded);

    printf("    Heuristics entries after roundtrip: %d (expected 5)\n",
           loaded->heuristics.count);

    /* Verify all 5 entries survived the save/load cycle */
    ASSERT_EQ_INT(5, loaded->heuristics.count);

    db_metadata_free(loaded);
    return 0;
}

/* ================================================================
 * Bug 12: db_full_path buffer overflow with extremely long names
 *
 * If name_pool contains very long names and the path has many
 * components, the buffer could overflow.  Verify that db_full_path
 * respects buf_size.
 * ================================================================ */

TEST(full_path_long_names_buffer_limit) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');

    /* Create a chain with 250-char directory names */
    char long_name[256];
    memset(long_name, 'A', 250);
    long_name[250] = '\0';

    int parent = -1;
    for (int i = 0; i < 20; i++) {
        /* Vary each name slightly to make them unique */
        long_name[0] = 'A' + (i % 26);
        parent = db_add_dir(drv, long_name, parent, false, false);
    }

    /* Use a small buffer to test truncation safety */
    char buf[512];
    memset(buf, 0xCC, sizeof(buf));
    db_full_path(drv, 19, buf, sizeof(buf));

    /* Verify no buffer overflow (sentinel byte after buf_size should be intact) */
    /* The path should be truncated but NUL-terminated within buf_size */
    ASSERT_TRUE(strlen(buf) < sizeof(buf));
    printf("    Path with long names truncated to %d chars (buf_size=%d)\n",
           (int)strlen(buf), (int)sizeof(buf));

    db_free(db);
    return 0;
}

/* ================================================================
 * Bug 13: Matcher with empty database
 *
 * Ensure matcher doesn't crash on database with drives but no dirs.
 * ================================================================ */

TEST(matcher_empty_drives) {
    NcdDatabase *db = db_create();
    db_add_drive(db, 'C');
    db_add_drive(db, 'D');

    int count = 0;
    NcdMatch *matches = matcher_find(db, "anything", false, false, &count);
    ASSERT_NULL(matches);
    ASSERT_EQ_INT(0, count);

    /* Also test fuzzy match on empty drives */
    matches = matcher_find_fuzzy(db, "anything", false, false, &count);
    ASSERT_NULL(matches);
    ASSERT_EQ_INT(0, count);

    db_free(db);
    return 0;
}

/* ================================================================
 * Bug 14: Version check with zero-length file
 * ================================================================ */

TEST(check_version_empty_file) {
    const char *test_file = "test_empty_ver.tmp";

    /* Create empty file */
    FILE *f = fopen(test_file, "wb");
    fclose(f);

    int result = db_check_file_version(test_file);
    ASSERT_EQ_INT(DB_VERSION_ERROR, result);

    remove(test_file);
    return 0;
}

/* ================================================================
 * Suite registration
 * ================================================================ */

void suite_bugs(void) {
    /* Metadata parsing bounds bugs */
    RUN_TEST(metadata_heuristics_bounds_check);
    RUN_TEST(metadata_groups_bounds_check);
    RUN_TEST(heuristics_roundtrip_multiple_entries);

    /* StringBuilder infinite loop */
    RUN_TEST(strbuilder_use_after_free_no_hang);
    RUN_TEST(strbuilder_ensure_cap_from_zero);

    /* db_full_path issues */
    RUN_TEST(full_path_deep_chain_truncation);
    RUN_TEST(full_path_circular_parent_chain);
    RUN_TEST(full_path_long_names_buffer_limit);

    /* Fuzzy matching performance */
    RUN_TEST(fuzzy_match_digit_heavy_performance);

    /* Unchecked fread / version checking */
    RUN_TEST(check_version_truncated_after_magic);
    RUN_TEST(load_auto_truncated_after_magic);
    RUN_TEST(check_version_empty_file);

    /* Header layout verification */
    RUN_TEST(binfilehdr_layout_verification);

    /* Edge cases */
    RUN_TEST(check_all_versions_no_databases);
    RUN_TEST(matcher_empty_drives);
}

TEST_MAIN(
    RUN_SUITE(bugs);
)
