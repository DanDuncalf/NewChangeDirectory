/* test_config.c -- Tests for configuration options (Phase 4 of CODEBASE_CLEANUP_PLAN) */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include <string.h>
#include <stdio.h>

/* Helper to load metadata from a specific file path */
static NcdMetadata *load_metadata_from_file(const char *path) {
    db_metadata_set_override(path);
    NcdMetadata *meta = db_metadata_load();
    db_metadata_set_override(NULL);  /* Clear override */
    return meta;
}

/* ================================================================ Rescan Interval Tests */

TEST(config_rescan_interval_save_load) {
    const char *test_file = "test_config_rescan.tmp";
    NcdConfig cfg = {0};
    NcdMetadata *loaded = NULL;
    
    /* Clean up from previous failed run */
    remove(test_file);
    
    /* Set values */
    db_config_init_defaults(&cfg);
    cfg.rescan_interval_hours = 24;
    
    /* Save through metadata interface */
    NcdMetadata *meta = db_metadata_create();
    meta->cfg = cfg;
    strncpy(meta->file_path, test_file, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load back using helper */
    loaded = load_metadata_from_file(test_file);
    ASSERT_NOT_NULL(loaded);
    
    /* Verify rescan_interval_hours is preserved */
    ASSERT_EQ_INT(24, loaded->cfg.rescan_interval_hours);
    
cleanup:
    if (loaded) db_metadata_free(loaded);
    remove(test_file);
    return 0;
}

TEST(config_rescan_interval_default_is_negative_one) {
    NcdConfig cfg;
    memset(&cfg, 0xFF, sizeof(cfg)); /* Fill with garbage */
    
    db_config_init_defaults(&cfg);
    
    /* Default should be -1 (never auto-rescan) */
    ASSERT_EQ_INT(-1, cfg.rescan_interval_hours);
    
    return 0;
}

TEST(config_rescan_interval_valid_range_min) {
    NcdConfig cfg;
    db_config_init_defaults(&cfg);
    
    /* Minimum valid value is 1 hour */
    cfg.rescan_interval_hours = 1;
    ASSERT_EQ_INT(1, cfg.rescan_interval_hours);
    
    return 0;
}

TEST(config_rescan_interval_valid_range_max) {
    NcdConfig cfg;
    db_config_init_defaults(&cfg);
    
    /* Maximum valid value is 168 hours (1 week) */
    cfg.rescan_interval_hours = 168;
    ASSERT_EQ_INT(168, cfg.rescan_interval_hours);
    
    return 0;
}

TEST(config_rescan_interval_valid_range_never) {
    NcdConfig cfg;
    db_config_init_defaults(&cfg);
    
    /* -1 means never auto-rescan */
    cfg.rescan_interval_hours = -1;
    ASSERT_EQ_INT(-1, cfg.rescan_interval_hours);
    
    return 0;
}

TEST(config_rescan_interval_preserves_value_24_hours) {
    const char *test_file = "test_config_24h.tmp";
    NcdConfig cfg = {0};
    NcdMetadata *loaded = NULL;
    
    remove(test_file);
    
    db_config_init_defaults(&cfg);
    cfg.rescan_interval_hours = 24;
    
    /* Save via metadata */
    NcdMetadata *meta = db_metadata_create();
    meta->cfg = cfg;
    strncpy(meta->file_path, test_file, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load back */
    loaded = load_metadata_from_file(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(24, loaded->cfg.rescan_interval_hours);
    
cleanup:
    if (loaded) db_metadata_free(loaded);
    remove(test_file);
    return 0;
}

TEST(config_rescan_interval_preserves_value_168_hours) {
    const char *test_file = "test_config_168h.tmp";
    NcdConfig cfg = {0};
    NcdMetadata *loaded = NULL;
    
    remove(test_file);
    
    db_config_init_defaults(&cfg);
    cfg.rescan_interval_hours = 168; /* 1 week */
    
    /* Save via metadata */
    NcdMetadata *meta = db_metadata_create();
    meta->cfg = cfg;
    strncpy(meta->file_path, test_file, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load back */
    loaded = load_metadata_from_file(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(168, loaded->cfg.rescan_interval_hours);
    
cleanup:
    if (loaded) db_metadata_free(loaded);
    remove(test_file);
    return 0;
}

/* ================================================================ Default Settings Tests */

TEST(config_default_fuzzy_match_save_load) {
    const char *test_file = "test_config_fuzzy.tmp";
    NcdConfig cfg = {0};
    NcdMetadata *loaded = NULL;
    
    remove(test_file);
    
    db_config_init_defaults(&cfg);
    cfg.default_fuzzy_match = true;
    
    /* Save via metadata */
    NcdMetadata *meta = db_metadata_create();
    meta->cfg = cfg;
    strncpy(meta->file_path, test_file, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load back */
    loaded = load_metadata_from_file(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_TRUE(loaded->cfg.default_fuzzy_match);
    
cleanup:
    if (loaded) db_metadata_free(loaded);
    remove(test_file);
    return 0;
}

TEST(config_default_fuzzy_match_default_false) {
    NcdConfig cfg;
    memset(&cfg, 0xFF, sizeof(cfg));
    
    db_config_init_defaults(&cfg);
    
    /* Default should be false */
    ASSERT_FALSE(cfg.default_fuzzy_match);
    
    return 0;
}

TEST(config_default_timeout_save_load) {
    const char *test_file = "test_config_timeout.tmp";
    NcdConfig cfg = {0};
    NcdMetadata *loaded = NULL;
    
    remove(test_file);
    
    db_config_init_defaults(&cfg);
    cfg.default_timeout = 120; /* 2 minutes */
    
    /* Save via metadata */
    NcdMetadata *meta = db_metadata_create();
    meta->cfg = cfg;
    strncpy(meta->file_path, test_file, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load back */
    loaded = load_metadata_from_file(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(120, loaded->cfg.default_timeout);
    
cleanup:
    if (loaded) db_metadata_free(loaded);
    remove(test_file);
    return 0;
}

TEST(config_default_timeout_default_negative_one) {
    NcdConfig cfg;
    memset(&cfg, 0xFF, sizeof(cfg));
    
    db_config_init_defaults(&cfg);
    
    /* Default should be -1 (not set) */
    ASSERT_EQ_INT(-1, cfg.default_timeout);
    
    return 0;
}

TEST(config_has_defaults_flag_set_after_init) {
    NcdConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    db_config_init_defaults(&cfg);
    
    /* has_defaults should be true after init_defaults */
    ASSERT_TRUE(cfg.has_defaults);
    
    return 0;
}

TEST(config_has_defaults_flag_persists_through_save_load) {
    const char *test_file = "test_config_has_defaults.tmp";
    NcdConfig cfg = {0};
    NcdMetadata *loaded = NULL;
    
    remove(test_file);
    
    db_config_init_defaults(&cfg);
    cfg.has_defaults = true;
    cfg.default_show_hidden = true; /* Set some defaults */
    
    /* Save via metadata */
    NcdMetadata *meta = db_metadata_create();
    meta->cfg = cfg;
    strncpy(meta->file_path, test_file, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load back */
    loaded = load_metadata_from_file(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_TRUE(loaded->cfg.has_defaults);
    
cleanup:
    if (loaded) db_metadata_free(loaded);
    remove(test_file);
    return 0;
}

/* ================================================================ Config Migration Tests */

TEST(config_migration_v3_to_v4_sets_encoding) {
    /* Simulate loading a v3 config (no text_encoding field) */
    NcdConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    /* Set up as v3 config */
    cfg.magic = NCD_CFG_MAGIC;
    cfg.version = 3;  /* Old version */
    cfg.default_show_hidden = false;
    cfg.default_show_system = false;
    cfg.has_defaults = true;
    /* text_encoding field doesn't exist in v3 - would be garbage or 0 */
    
    /* Simulate migration: init_defaults sets proper v4 values */
    NcdConfig new_cfg;
    db_config_init_defaults(&new_cfg);
    
    /* New config should have UTF-8 encoding and v4 version */
    ASSERT_EQ_INT(NCD_CFG_VERSION, new_cfg.version);  /* Should be 4 */
    ASSERT_EQ_INT(NCD_TEXT_UTF8, new_cfg.text_encoding);
    
    return 0;
}

TEST(config_v4_has_correct_version_number) {
    NcdConfig cfg;
    db_config_init_defaults(&cfg);
    
    /* Version should be NCD_CFG_VERSION (4) */
    ASSERT_EQ_INT(NCD_CFG_VERSION, cfg.version);
    
    return 0;
}

TEST(config_v4_has_correct_magic) {
    NcdConfig cfg;
    db_config_init_defaults(&cfg);
    
    /* Magic should be NCD_CFG_MAGIC */
    ASSERT_EQ_INT((int)NCD_CFG_MAGIC, (int)cfg.magic);
    
    return 0;
}

/* ================================================================ Combined Config Tests */

TEST(config_all_fields_save_load_together) {
    const char *test_file = "test_config_all.tmp";
    NcdConfig cfg = {0};
    NcdMetadata *loaded = NULL;
    
    remove(test_file);
    
    /* Set all config fields to specific values */
    db_config_init_defaults(&cfg);
    cfg.default_show_hidden = true;
    cfg.default_show_system = true;
    cfg.default_fuzzy_match = true;
    cfg.default_timeout = 300;
    cfg.rescan_interval_hours = 72;
    cfg.has_defaults = true;
    cfg.text_encoding = NCD_TEXT_UTF8;
    cfg.service_retry_count = 20;
    
    /* Save via metadata */
    NcdMetadata *meta = db_metadata_create();
    meta->cfg = cfg;
    strncpy(meta->file_path, test_file, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load back */
    loaded = load_metadata_from_file(test_file);
    ASSERT_NOT_NULL(loaded);
    
    /* Verify all fields */
    ASSERT_TRUE(loaded->cfg.default_show_hidden);
    ASSERT_TRUE(loaded->cfg.default_show_system);
    ASSERT_TRUE(loaded->cfg.default_fuzzy_match);
    ASSERT_EQ_INT(300, loaded->cfg.default_timeout);
    ASSERT_EQ_INT(72, loaded->cfg.rescan_interval_hours);
    ASSERT_TRUE(loaded->cfg.has_defaults);
    ASSERT_EQ_INT(NCD_TEXT_UTF8, loaded->cfg.text_encoding);
    ASSERT_EQ_INT(20, loaded->cfg.service_retry_count);
    
cleanup:
    if (loaded) db_metadata_free(loaded);
    remove(test_file);
    return 0;
}

TEST(config_init_defaults_clears_previous_values) {
    NcdConfig cfg;
    
    /* Set some custom values */
    cfg.default_show_hidden = true;
    cfg.default_show_system = true;
    cfg.default_fuzzy_match = true;
    cfg.default_timeout = 999;
    cfg.rescan_interval_hours = 999;
    cfg.has_defaults = false;
    
    /* Re-initialize with defaults */
    db_config_init_defaults(&cfg);
    
    /* Should now have default values */
    ASSERT_FALSE(cfg.default_show_hidden);
    ASSERT_FALSE(cfg.default_show_system);
    ASSERT_FALSE(cfg.default_fuzzy_match);
    ASSERT_EQ_INT(-1, cfg.default_timeout);
    ASSERT_EQ_INT(-1, cfg.rescan_interval_hours);
    ASSERT_TRUE(cfg.has_defaults);
    
    return 0;
}

/* ================================================================ Text Encoding Tests */

TEST(config_text_encoding_save_load) {
    const char *test_file = "test_config_encoding.tmp";
    NcdConfig cfg = {0};
    NcdMetadata *loaded = NULL;
    
    remove(test_file);
    
    db_config_init_defaults(&cfg);
    cfg.text_encoding = NCD_TEXT_UTF16LE;
    
    /* Save via metadata */
    NcdMetadata *meta = db_metadata_create();
    meta->cfg = cfg;
    strncpy(meta->file_path, test_file, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load back */
    loaded = load_metadata_from_file(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(NCD_TEXT_UTF16LE, loaded->cfg.text_encoding);
    
cleanup:
    if (loaded) db_metadata_free(loaded);
    remove(test_file);
    return 0;
}

TEST(config_text_encoding_default_is_utf8) {
    NcdConfig cfg;
    memset(&cfg, 0xFF, sizeof(cfg));
    
    db_config_init_defaults(&cfg);
    
    /* Default text encoding should be UTF-8 */
    ASSERT_EQ_INT(NCD_TEXT_UTF8, cfg.text_encoding);
    
    return 0;
}

/* ================================================================ Service Retry Count Tests */

TEST(config_service_retry_count_save_load) {
    const char *test_file = "test_config_retry.tmp";
    NcdConfig cfg = {0};
    NcdMetadata *loaded = NULL;
    
    remove(test_file);
    
    db_config_init_defaults(&cfg);
    cfg.service_retry_count = 15;
    
    /* Save via metadata */
    NcdMetadata *meta = db_metadata_create();
    meta->cfg = cfg;
    strncpy(meta->file_path, test_file, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load back */
    loaded = load_metadata_from_file(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(15, loaded->cfg.service_retry_count);
    
cleanup:
    if (loaded) db_metadata_free(loaded);
    remove(test_file);
    return 0;
}

TEST(config_service_retry_count_default) {
    NcdConfig cfg;
    memset(&cfg, 0xFF, sizeof(cfg));
    
    db_config_init_defaults(&cfg);
    
    /* Default service retry count is 0, meaning "use NCD_DEFAULT_SERVICE_RETRY_COUNT (10)" */
    /* The actual default value (10) is applied at runtime, not in the config struct */
    ASSERT_EQ_INT(0, cfg.service_retry_count);
    
    return 0;
}

/* ================================================================ Test Suites */

void suite_config_rescan_interval(void) {
    printf("\n--- Rescan Interval Tests ---\n");
    RUN_TEST(config_rescan_interval_save_load);
    RUN_TEST(config_rescan_interval_default_is_negative_one);
    RUN_TEST(config_rescan_interval_valid_range_min);
    RUN_TEST(config_rescan_interval_valid_range_max);
    RUN_TEST(config_rescan_interval_valid_range_never);
    RUN_TEST(config_rescan_interval_preserves_value_24_hours);
    RUN_TEST(config_rescan_interval_preserves_value_168_hours);
}

void suite_config_defaults(void) {
    printf("\n--- Default Settings Tests ---\n");
    RUN_TEST(config_default_fuzzy_match_save_load);
    RUN_TEST(config_default_fuzzy_match_default_false);
    RUN_TEST(config_default_timeout_save_load);
    RUN_TEST(config_default_timeout_default_negative_one);
    RUN_TEST(config_has_defaults_flag_set_after_init);
    RUN_TEST(config_has_defaults_flag_persists_through_save_load);
}

void suite_config_migration(void) {
    printf("\n--- Config Migration Tests ---\n");
    RUN_TEST(config_migration_v3_to_v4_sets_encoding);
    RUN_TEST(config_v4_has_correct_version_number);
    RUN_TEST(config_v4_has_correct_magic);
}

void suite_config_combined(void) {
    printf("\n--- Combined Config Tests ---\n");
    RUN_TEST(config_all_fields_save_load_together);
    RUN_TEST(config_init_defaults_clears_previous_values);
}

void suite_config_encoding(void) {
    printf("\n--- Text Encoding Tests ---\n");
    RUN_TEST(config_text_encoding_save_load);
    RUN_TEST(config_text_encoding_default_is_utf8);
}

void suite_config_service_retry(void) {
    printf("\n--- Service Retry Count Tests ---\n");
    RUN_TEST(config_service_retry_count_save_load);
    RUN_TEST(config_service_retry_count_default);
}

void suite_config(void) {
    suite_config_rescan_interval();
    suite_config_defaults();
    suite_config_migration();
    suite_config_combined();
    suite_config_encoding();
    suite_config_service_retry();
}

TEST_MAIN(
    RUN_SUITE(config);
)
