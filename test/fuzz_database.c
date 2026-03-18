/* fuzz_database.c -- Fuzz test for database loading */
#include "../src/database.h"
#include "../src/ncd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Generate random corrupted binary file */
void fuzz_binary_load(void) {
    const char *test_file = "fuzz_test.tmp";
    
    for (int iteration = 0; iteration < 10000; iteration++) {
        /* Generate random data */
        size_t size = rand() % 4096;
        unsigned char *data = malloc(size);
        
        for (size_t i = 0; i < size; i++) {
            data[i] = rand() % 256;
        }
        
        /* Occasionally start with valid magic */
        if (rand() % 10 == 0 && size >= 4) {
            data[0] = 'N';
            data[1] = 'C';
            data[2] = 'D';
            data[3] = 'B';
        }
        
        /* Write and attempt load */
        FILE *f = fopen(test_file, "wb");
        fwrite(data, 1, size, f);
        fclose(f);
        
        /* Should not crash */
        NcdDatabase *db = db_load_binary(test_file);
        if (db) {
            db_free(db);
        }
        
        free(data);
        
        if (iteration % 1000 == 0) {
            printf("Fuzz iteration %d complete\n", iteration);
        }
    }
    
    remove(test_file);
    printf("Fuzz test passed: 10000 iterations without crash\n");
}

/* Fuzz JSON parser with random/malformed JSON */
void fuzz_json_load(void) {
    const char *test_file = "fuzz_json.tmp";
    const char *json_fragments[] = {
        "{", "}", "[", "]", "\"", ":", ",", "null", "true", "false",
        "123", "-456", "1e10", "0xABC", "\\", "\\n", "\\t", "\\u",
        "{\"version\":", "\"drives\":[]", "\"dirs\":null",
    };
    
    for (int iteration = 0; iteration < 5000; iteration++) {
        /* Build random JSON-like content */
        FILE *f = fopen(test_file, "w");
        
        int fragments_count = rand() % 20;
        for (int i = 0; i < fragments_count; i++) {
            const char *frag = json_fragments[rand() % (sizeof(json_fragments)/sizeof(char*))];
            fwrite(frag, 1, strlen(frag), f);
        }
        
        fclose(f);
        
        /* Should not crash */
        NcdDatabase *db = db_load(test_file);
        if (db) {
            db_free(db);
        }
    }
    
    remove(test_file);
    printf("JSON fuzz test passed\n");
}

int main(void) {
    srand((unsigned)time(NULL));
    
    printf("Starting fuzz tests...\n");
    fuzz_binary_load();
    fuzz_json_load();
    printf("All fuzz tests passed!\n");
    return 0;
}
