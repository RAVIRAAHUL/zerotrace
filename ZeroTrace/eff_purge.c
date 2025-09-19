// purgeTrace.c
// WARNING: Destructive. Run as Administrator. Usage:
//   purgeTrace.exe <PhysicalDriveNumber> <VolumeLetter|NONE> [--test|--purge]
//
// Example:
//   purgeTrace.exe 1 D --purge

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE (64*1024*1024) // 64 MiB
#define PASSES 3

void usage(const char *prog) {
    printf("Usage: %s <PhysicalDriveNumber> <VolumeLetter|NONE> [--test|--purge]\n", prog);
    printf("Example: %s 1 D --purge\n", prog);
}

void fill_random(BYTE *buf, size_t size) {
    for (size_t i = 0; i < size; i++) buf[i] = (BYTE)(rand() % 256);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        usage(argv[0]);
        return 1;
    }

    const char *driveNumStr = argv[1];
    const char *volArg = argv[2];
    int testMode = 0, purgeMode = 0;

    if (strcmp(argv[3], "--test") == 0) testMode = 1;
    else if (strcmp(argv[3], "--purge") == 0) purgeMode = 1;
    else {
        usage(argv[0]);
        return 1;
    }

    char physicalPath[64];
    snprintf(physicalPath, sizeof(physicalPath), "\\\\.\\PhysicalDrive%s", driveNumStr);

    printf("WARNING: This will overwrite data on %s\n", physicalPath);
    if (strcmp(volArg, "NONE") != 0) printf("Volume to lock/dismount: %s:\n", volArg);

    printf("Mode: %s\n", testMode ? "TEST (1 MiB)" : (purgeMode ? "PURGE (multi-pass)" : "OVERWRITE"));

    printf("Type the word 'CONFIRM' (uppercase) to proceed: ");
    char confirm[64];
    if (!fgets(confirm, sizeof(confirm), stdin)) return 1;
    confirm[strcspn(confirm, "\r\n")] = 0;
    if (strcmp(confirm, "CONFIRM") != 0) {
        printf("Aborted: confirmation not received.\n");
        return 1;
    }

    HANDLE hDrive = CreateFileA(physicalPath,
                                GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_WRITE_THROUGH,
                                NULL);
    if (hDrive == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        fprintf(stderr, "Failed to open %s for writing (err=%lu). Run as Admin.\n", physicalPath, e);
        return 1;
    }

    void *buf = VirtualAlloc(NULL, BUF_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) {
        fprintf(stderr, "VirtualAlloc failed\n");
        CloseHandle(hDrive);
        return 1;
    }

    if (testMode) {
        printf("[TEST] Writing 1 MiB zeros...\n");
        memset(buf, 0, BUF_SIZE);
        DWORD written = 0;
        LARGE_INTEGER offset = {0};
        SetFilePointerEx(hDrive, offset, NULL, FILE_BEGIN);
        if (!WriteFile(hDrive, buf, BUF_SIZE, &written, NULL)) {
            fprintf(stderr, "WriteFile failed (err=%lu)\n", GetLastError());
        } else printf("[TEST] %u bytes written.\n", written);
        FlushFileBuffers(hDrive);
    } else if (purgeMode) {
        BYTE patterns[PASSES] = {0x00, 0xFF, 0xAA};
        unsigned long long total = 0;

        for (int pass = 0; pass < PASSES; pass++) {
            printf("[PURGE] Pass %d: writing pattern 0x%02X\n", pass + 1, patterns[pass]);
            if (patterns[pass] == 0xAA) fill_random((BYTE *)buf, BUF_SIZE);
            else memset(buf, patterns[pass], BUF_SIZE);

            LARGE_INTEGER offset = {0};
            SetFilePointerEx(hDrive, offset, NULL, FILE_BEGIN);

            DWORD written;
            total = 0;
            while (WriteFile(hDrive, buf, BUF_SIZE, &written, NULL) && written > 0) {
                total += written;
                if ((total % (256ULL * 1024 * 1024)) == 0)
                    printf("... %llu MB written\n", total / (1024 * 1024));
            }
            FlushFileBuffers(hDrive);
        }
        printf("[PURGE] Multi-pass overwrite complete. Total bytes written: %llu\n", total);
    }

    VirtualFree(buf, 0, MEM_RELEASE);
    CloseHandle(hDrive);

    printf("Done. Recovery from typical forensic tools is now extremely unlikely.\n");
    return 0;
}
