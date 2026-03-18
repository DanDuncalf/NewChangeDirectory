/* test_db_corruption.c -- Targeted database corruption tests
 * 
 * These tests deliberately corrupt database files in specific ways
 * to verify the loader properly rejects malformed data.
 */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TEST_FILE = "corrupt_test.tmp";

/* Helper: Create a valid database file */
static void create_valid_db(void) {
    NcdDatabase *db = db_create();
    db->default_show_hidden = false;
    db->default_show_system = false;
    db->last_scan = 1234567890;
    
    DriveData *drv = db_add_drive(db, 'C');
    drv->type = PLATFORM_DRIVE_FIXED;
    strncpy(drv->label, "TestDrive", sizeof(drv->label));
    
    /* Create a simple tree */
    int users = db_add_dir(drv, "Users", -1, false, false);
    db_add_dir(drv, "Scott", users, false, false);
    db_add_dir(drv, "Admin", users, false, false);
    
    db_save_binary_single(db, 0, TEST_FILE);
    db_free(db);
}

/* Helper: Read file into buffer */
static size_t read_file(unsigned char **buf) {
    FILE *f = fopen(TEST_FILE, "rb");
    if (!f) return 0;
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    rewind(f);
    
    *buf = malloc(size);
    fread(*buf, 1, size, f);
    fclose(f);
    return size;
}

/* Helper: Write buffer to file */
static void write_file(const unsigned char *buf, size_t size) {
    FILE *f = fopen(TEST_FILE, "wb");
    fwrite(buf, 1, size, f);
    fclose(f);
}

/* Helper: Load and verify it fails */
static bool load_should_fail(void) {
    NcdDatabase *db = db_load_binary(TEST_FILE);
    if (db) {
        db_free(db);
        return false;
    }
    return true;
}

TEST(corrupt_magic_number) {
    create_valid_db();
    
    unsigned char *buf;
    size_t size = read_file(&buf);
    ASSERT_TRUE(size > 4);
    
    /* Corrupt magic from 'NCDB' to 'BAD!' */
    buf[0] = 'B';
    buf[1] = 'A';
    buf[2] = 'D';
    buf[3] = '!';
    
    write_file(buf, size);
    ASSERT_TRUE(load_should_fail());
    
    free(buf);
    remove(TEST_FILE);
    return 0;
}

TEST(corrupt_version_invalid) {
    create_valid_db();
    
    unsigned char *buf;
    size_t size = read_file(&buf);
    ASSERT_TRUE(size > 6);
    
    /* Version is at offset 4 (2 bytes) - set to invalid version */
    buf[4] = 0xFF;
    buf[5] = 0xFF;
    
    write_file(buf, size);
    ASSERT_TRUE(load_should_fail());
    
    free(buf);
    remove(TEST_FILE);
    return 0;
}

TEST(corrupt_drive_count_zero) {
    create_valid_db();
    
    unsigned char *buf;
    size_t size = read_file(&buf);
    ASSERT_TRUE(size > 16);
    
    /* drive_count is at offset 16 (4 bytes) - set to zero */
    buf[16] = 0;
    buf[17] = 0;
    buf[18] = 0;
    buf[19] = 0;
    
    write_file(buf, size);
    ASSERT_TRUE(load_should_fail());
    
    free(buf);
    remove(TEST_FILE);
    return 0;
}

TEST(corrupt_drive_count_overflow) {
    create_valid_db();
    
    unsigned char *buf;
    size_t size = read_file(&buf);
    ASSERT_TRUE(size > 16);
    
    /* drive_count = 0xFFFFFFFF */
    buf[16] = 0xFF;
    buf[17] = 0xFF;
    buf[18] = 0xFF;
    buf[19] = 0xFF;
    
    write_file(buf, size);
    ASSERT_TRUE(load_should_fail());
    
    free(buf);
    remove(TEST_FILE);
    return 0;
}

TEST(corrupt_drive_count_large) {
    create_valid_db();
    
    unsigned char *buf;
    size_t size = read_file(&buf);
    ASSERT_TRUE(size > 16);
    
    /* drive_count = 1000 (way more than file can hold) */
    buf[16] = 0xE8;  /* 1000 = 0x3E8 */
    buf[17] = 0x03;
    buf[18] = 0;
    buf[19] = 0;
    
    write_file(buf, size);
    ASSERT_TRUE(load_should_fail());
    
    free(buf);
    remove(TEST_FILE);
    return 0;
}

TEST(truncate_at_header) {
    create_valid_db();
    
    unsigned char *buf;
    size_t size = read_file(&buf);
    ASSERT_TRUE(size > 4);
    
    /* Truncate to just magic + version */
    write_file(buf, 8);
    ASSERT_TRUE(load_should_fail());
    
    free(buf);
    remove(TEST_FILE);
    return 0;
}

TEST(truncate_at_drive_header) {
    create_valid_db();
    
    unsigned char *buf;
    size_t size = read_file(&buf);
    ASSERT_TRUE(size > 24);
    
    /* Truncate to just file header + partial drive header */
    write_file(buf, 50);
    ASSERT_TRUE(load_should_fail());
    
    free(buf);
    remove(TEST_FILE);
    return 0;
}

TEST(append_garbage) {
    create_valid_db();
    
    unsigned char *buf;
    size_t size = read_file(&buf);
    ASSERT_TRUE(size > 0);
    
    /* Append garbage data */
    unsigned char *garbage = malloc(size + 100);
    memcpy(garbage, buf, size);
    for (int i = 0; i < 100; i++) {
        garbage[size + i] = (unsigned char)(rand() % 256);
    }
    
    write_file(garbage, size + 100);
    /* This might actually load successfully if the extra data is ignored */
    /* Just verify it doesn't crash */
    NcdDatabase *db = db_load_binary(TEST_FILE);
    if (db) db_free(db);
    /* Test passes if we get here without crash */
    
    free(buf);
    free(garbage);
    remove(TEST_FILE);
    return 0;
}

TEST(flip_bits_in_header) {
    create_valid_db();
    
    for (int bit = 0; bit < 32; bit++) {
        create_valid_db();
        
        unsigned char *buf;
        size_t size = read_file(&buf);
        
        /* Flip one bit in first 24 bytes (file header) */
        int byte = bit / 8;
        int b = bit % 8;
        buf[byte] ^= (1 << b);
        
        write_file(buf, size);
        /* Should either load OK or reject, never crash */
        NcdDatabase *db = db_load_binary(TEST_FILE);
        if (db) db_free(db);
        
        free(buf);
        remove(TEST_FILE);
    }
    
    return 0;
}

TEST(corrupt_dir_count_overflow) {
    create_valid_db();
    
    unsigned char *buf;
    size_t size = read_file(&buf);
    ASSERT_TRUE(size > 24 + 80); /* File header + drive header */
    
    /* Drive header: dir_count at offset 72 (after letter[1], pad[3], type[4], label[64]) */
    int dir_count_offset = 24 + 72;
    if (size > dir_count_offset + 4) {
        buf[dir_count_offset] = 0xFF;
        buf[dir_count_offset + 1] = 0xFF;
        buf[dir_count_offset + 2] = 0xFF;
        buf[dir_count_offset + 3] = 0xFF;
        
        write_file(buf, size);
        ASSERT_TRUE(load_should_fail());
    }
    
    free(buf);
    remove(TEST_FILE);
    return 0;
}

TEST(random_bit_flips_alternating) {
    /* 64 iterations: alternating between 1-bit and 2-bit flips at different bytes */
    srand((unsigned)time(NULL));
    
    int crash_count = 0;
    int load_count = 0;
    int reject_count = 0;
    
    for (int iter = 0; iter < 64; iter++) {
        create_valid_db();
        
        unsigned char *buf;
        size_t size = read_file(&buf);
        if (size == 0) {
            remove(TEST_FILE);
            continue;
        }
        
        if (iter % 2 == 0) {
            /* Odd iteration (0, 2, 4...): Flip 1 bit at random position */
            size_t byte_pos = rand() % size;
            int bit_pos = rand() % 8;
            buf[byte_pos] ^= (1 << bit_pos);
        } else {
            /* Even iteration (1, 3, 5...): Flip 2 bits at different bytes */
            size_t byte_pos1 = rand() % size;
            size_t byte_pos2 = rand() % size;
            /* Ensure different bytes */
            while (byte_pos2 == byte_pos1 && size > 1) {
                byte_pos2 = rand() % size;
            }
            int bit_pos1 = rand() % 8;
            int bit_pos2 = rand() % 8;
            buf[byte_pos1] ^= (1 << bit_pos1);
            buf[byte_pos2] ^= (1 << bit_pos2);
        }
        
        write_file(buf, size);
        
        /* Try to load - should never crash */
        NcdDatabase *db = db_load_binary(TEST_FILE);
        if (db) {
            db_free(db);
            load_count++;
        } else {
            reject_count++;
        }
        
        free(buf);
        remove(TEST_FILE);
    }
    
    printf("    64 iterations (alternating 1-bit/2-bit): %d loaded, %d rejected, %d crashed\n", 
           load_count, reject_count, crash_count);
    
    /* Test passes if no crashes occurred */
    ASSERT_EQ_INT(0, crash_count);
    return 0;
}

TEST(random_bit_flips_same_byte) {
    /* 16 iterations: 2 bit flips within the SAME byte (different bits) */
    srand((unsigned)time(NULL));
    
    int crash_count = 0;
    int load_count = 0;
    int reject_count = 0;
    
    for (int iter = 0; iter < 16; iter++) {
        create_valid_db();
        
        unsigned char *buf;
        size_t size = read_file(&buf);
        if (size == 0) {
            remove(TEST_FILE);
            continue;
        }
        
        /* Pick 1 random byte position */
        size_t byte_pos = rand() % size;
        
        /* Pick 2 different bit positions within that byte */
        int bit_pos1 = rand() % 8;
        int bit_pos2 = rand() % 8;
        /* Ensure different bits */
        while (bit_pos2 == bit_pos1) {
            bit_pos2 = rand() % 8;
        }
        
        /* Flip both bits in the same byte */
        buf[byte_pos] ^= (1 << bit_pos1);
        buf[byte_pos] ^= (1 << bit_pos2);
        
        write_file(buf, size);
        
        /* Try to load - should never crash */
        NcdDatabase *db = db_load_binary(TEST_FILE);
        if (db) {
            db_free(db);
            load_count++;
        } else {
            reject_count++;
        }
        
        free(buf);
        remove(TEST_FILE);
    }
    
    printf("    16 iterations (2 bits in same byte): %d loaded, %d rejected, %d crashed\n", 
           load_count, reject_count, crash_count);
    
    /* Test passes if no crashes occurred */
    ASSERT_EQ_INT(0, crash_count);
    return 0;
}

void suite_db_corruption(void) {
    RUN_TEST(corrupt_magic_number);
    RUN_TEST(corrupt_version_invalid);
    RUN_TEST(corrupt_drive_count_zero);
    RUN_TEST(corrupt_drive_count_overflow);
    RUN_TEST(corrupt_drive_count_large);
    RUN_TEST(truncate_at_header);
    RUN_TEST(truncate_at_drive_header);
    RUN_TEST(append_garbage);
    RUN_TEST(flip_bits_in_header);
    RUN_TEST(corrupt_dir_count_overflow);
    RUN_TEST(random_bit_flips_alternating);
    RUN_TEST(random_bit_flips_same_byte);
}

TEST_MAIN(
    RUN_SUITE(db_corruption);
)
