#include <stdio.h>
#include <string.h>
#include "../src/database.h"
#include "../src/ncd.h"

int main() {
    // Test 1
    {
        NcdDatabase *db = db_create();
        DriveData *drv = db_add_drive(db, 'C');
        int parent = -1;
        for (int i = 0; i < 100; i++) {
            char name[32];
            snprintf(name, sizeof(name), "level_%04d", i);
            parent = db_add_dir(drv, name, parent, 0, 0);
        }
        char path[MAX_PATH];
        db_full_path(drv, parent, path, sizeof(path));
        printf("Test1: strlen(path)=%zu, MAX_PATH=%d\n", strlen(path), (int)MAX_PATH);
        printf("Test1: path=%s...\n", path);
        db_free(db);
    }
    
    // Test 2
    {
        NcdDatabase *db = db_create();
        DriveData *drv = db_add_drive(db, 'C');
        int p1 = db_add_dir(drv, "common_name", -1, 0, 0);
        int p2 = db_add_dir(drv, "another_parent", -1, 0, 0);
        int c1 = db_add_dir(drv, "common_name", p1, 0, 0);
        int c2 = db_add_dir(drv, "common_name", p2, 0, 0);
        printf("p1=%d, p2=%d, c1=%d, c2=%d\n", p1, p2, c1, c2);
        char path[MAX_PATH];
        db_full_path(drv, 2, path, sizeof(path));
        printf("Test2: path for index 2 = [%s]\n", path);
        db_full_path(drv, 3, path, sizeof(path));
        printf("Test2: path for index 3 = [%s]\n", path);
        db_free(db);
    }
    return 0;
}
