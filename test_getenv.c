#include <stdio.h>
#include <stdlib.h>

int main() {
    const char *test_mode = getenv("NCD_TEST_MODE");
    if (test_mode && test_mode[0]) {
        printf("NCD_TEST_MODE is set: %s\n", test_mode);
    } else {
        printf("NCD_TEST_MODE is NOT set (test_mode=%p)\n", test_mode);
    }
    return 0;
}
