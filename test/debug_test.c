#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define NCD_UTF16LE_BOM_LEN 2
#define NCD_TEXT_UTF16LE 2

int main() {
    FILE *f = fopen("debug_bom.tmp", "wb");
    if (!f) {
        printf("Failed to open file\n");
        return 1;
    }
    
    uint8_t bom[2] = {0xFF, 0xFE};
    fwrite(bom, 1, 2, f);
    
    uint8_t header[32] = {0};
    header[0] = 0x4E;
    header[1] = 0x43;
    header[2] = 0x44;
    header[3] = 0x42;
    header[4] = 3;
    header[5] = 0;
    header[20] = 1;
    header[22] = NCD_TEXT_UTF16LE;
    fwrite(header, 1, 32, f);
    
    uint8_t drive_header[80] = {0};
    drive_header[0] = 'C';
    drive_header[68] = 1;
    drive_header[72] = 5;
    fwrite(drive_header, 1, 80, f);
    
    uint8_t data[20] = {0};
    data[4] = 0;
    data[5] = 0;
    data[6] = 0;
    data[7] = 0xFF;
    data[8] = 0;
    strcpy((char*)&data[12], "Test");
    fwrite(data, 1, 20, f);
    
    fclose(f);
    
    f = fopen("debug_bom.tmp", "rb");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    
    printf("File size: %ld\n", sz);
    printf("Expected: %d\n", 2 + 32 + 80 + 20);
    
    remove("debug_bom.tmp");
    return 0;
}
