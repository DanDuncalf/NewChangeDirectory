/* bench_matcher.c -- Benchmark matcher performance */
#include "../src/matcher.h"
#include "../src/database.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

/* Generate synthetic database with given characteristics */
NcdDatabase *generate_synthetic_db(int drives, int dirs_per_drive, int avg_name_len) {
    NcdDatabase *db = db_create();
    const char *name_parts[] = {"src", "bin", "lib", "test", "doc", 
                                "main", "util", "core", "app", "web"};
    
    for (int d = 0; d < drives; d++) {
        DriveData *drv = db_add_drive(db, 'A' + d);
        
        for (int i = 0; i < dirs_per_drive; i++) {
            char name[64];
            snprintf(name, sizeof(name), "%s%d",
                    name_parts[i % 10], i / 10);
            
            /* Vary parent to create tree structure */
            int parent = (i < 10) ? -1 : (rand() % (i / 10 + 1)) * 10;
            
            db_add_dir(drv, name, parent, false, false);
        }
    }
    
    return db;
}

void benchmark_search(NcdDatabase *db, const char *search, int iterations) {
    clock_t start = clock();
    
    for (int i = 0; i < iterations; i++) {
        int count;
        NcdMatch *m = matcher_find(db, search, false, false, &count);
        if (m) free(m);
    }
    
    clock_t end = clock();
    double ms = (double)(end - start) * 1000.0 / CLOCKS_PER_SEC;
    
    printf("  Search '%s': %.3f ms (%.3f ms per query)\n",
           search, ms, ms / iterations);
}

int main(void) {
    printf("=== Matcher Performance Benchmarks ===\n\n");
    
    /* Small database */
    printf("Small DB (1 drive, 1000 dirs):\n");
    NcdDatabase *small = generate_synthetic_db(1, 1000, 8);
    benchmark_search(small, "src", 1000);
    benchmark_search(small, "src1", 1000);
    db_free(small);
    
    /* Medium database */
    printf("\nMedium DB (4 drives, 10000 dirs each):\n");
    NcdDatabase *medium = generate_synthetic_db(4, 10000, 8);
    benchmark_search(medium, "src", 100);
    benchmark_search(medium, "bin50", 100);
    db_free(medium);
    
    /* Large database */
    printf("\nLarge DB (8 drives, 50000 dirs each):\n");
    NcdDatabase *large = generate_synthetic_db(8, 50000, 8);
    benchmark_search(large, "core", 10);
    benchmark_search(large, "app100", 10);
    db_free(large);
    
    printf("\nBenchmarks complete.\n");
    return 0;
}
