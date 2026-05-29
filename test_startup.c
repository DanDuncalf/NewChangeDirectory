#include <windows.h>
#include <stdio.h>

int main() {
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    
    QueryPerformanceCounter(&start);
    HANDLE h = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    QueryPerformanceCounter(&end);
    printf("CreateFile CONOUT$: %.2f ms\n", (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    
    QueryPerformanceCounter(&start);
    HANDLE p = CreateFileA("\\\\.\\pipe\\NCD_SYSTEM_CONTROL", GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    QueryPerformanceCounter(&end);
    printf("CreateFile pipe (not exist): %.2f ms, result=%p\n", (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart, p);
    if (p != INVALID_HANDLE_VALUE) CloseHandle(p);
    
    QueryPerformanceCounter(&start);
    DWORD attr = GetFileAttributesA("C:\\ProgramData\\NCD\\ncd.metadata");
    QueryPerformanceCounter(&end);
    printf("GetFileAttributes metadata: %.2f ms, attr=0x%lx\n", (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart, attr);
    
    QueryPerformanceCounter(&start);
    attr = GetFileAttributesA("C:\\ProgramData\\NCD\\ncd_E.database");
    QueryPerformanceCounter(&end);
    printf("GetFileAttributes E db: %.2f ms, attr=0x%lx\n", (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart, attr);
    
    return 0;
}
