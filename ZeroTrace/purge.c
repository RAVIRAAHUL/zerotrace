#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef CTL_CODE
#define CTL_CODE(DeviceType, Function, Method, Access) (                 \
    ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
#endif

#ifndef FILE_DEVICE_FILE_SYSTEM
#define FILE_DEVICE_FILE_SYSTEM 0x00000009
#endif

#ifndef METHOD_BUFFERED
#define METHOD_BUFFERED 0
#endif

#ifndef FILE_ANY_ACCESS
#define FILE_ANY_ACCESS 0
#endif

#ifndef FSCTL_LOCK_VOLUME
#define FSCTL_LOCK_VOLUME     CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSCTL_UNLOCK_VOLUME   CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 7, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSCTL_DISMOUNT_VOLUME CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 8, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#define BUF_SIZE (512 * 1024) // 512 KB buffer

void overwrite_pass(HANDLE hDrive, BYTE pattern, int pass) {
    BYTE *buf = (BYTE *)malloc(BUF_SIZE);
    DWORD written;
    LARGE_INTEGER pos;
    DWORD err;

    if (!buf) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }

    if (pattern == 0xAA) { // Random pass
        srand((unsigned)time(NULL));
        for (size_t i = 0; i < BUF_SIZE; i++) buf[i] = rand() % 256;
    } else {
        memset(buf, pattern, BUF_SIZE);
    }

    pos.QuadPart = 0;
    if (!SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN)) {
        fprintf(stderr, "[PASS %d] SetFilePointerEx failed (err=%lu)\n", pass, GetLastError());
        free(buf);
        return;
    }

    size_t total = 0;
    while (1) {
        if (!WriteFile(hDrive, buf, BUF_SIZE, &written, NULL)) {
            err = GetLastError();
            if (err == ERROR_HANDLE_EOF) break;
            fprintf(stderr, "[PASS %d] WriteFile failed (err=%lu)\n", pass, err);
            break;
        }
        if (written == 0) break;
        total += written;
    }

    printf("[PASS %d] Pattern %s complete (%zu MB)\n",
           pass,
           (pattern == 0x00 ? "0x00" :
            pattern == 0xFF ? "0xFF" : "Random"),
           total / (1024 * 1024));

    free(buf);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <PhysicalDriveNumber> <VolumeLetter|NONE>\n", argv[0]);
        return 1;
    }

    int driveNum = atoi(argv[1]);
    char volLetter = argv[2][0];
    char drivePath[64];
    sprintf(drivePath, "\\\\.\\PhysicalDrive%d", driveNum);

    printf("NIST 800-88 Purge starting on %s\n", drivePath);

    // Lock & dismount volume
    if (strcmp(argv[2], "NONE") != 0) {
        char volPath[64];
        sprintf(volPath, "\\\\.\\%c:", volLetter);

        HANDLE hVol = CreateFileA(volPath, GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                  OPEN_EXISTING, 0, NULL);

        if (hVol == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Failed to open volume %s (err=%lu)\n", volPath, GetLastError());
            return 1;
        }

        DWORD bytes;
        if (!DeviceIoControl(hVol, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytes, NULL))
            fprintf(stderr, "FSCTL_LOCK_VOLUME failed (err=%lu)\n", GetLastError());

        if (!DeviceIoControl(hVol, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytes, NULL))
            fprintf(stderr, "FSCTL_DISMOUNT_VOLUME failed (err=%lu)\n", GetLastError());

        CloseHandle(hVol);
    }

    // Open the raw physical drive
    HANDLE hDrive = CreateFileA(drivePath,
                                GENERIC_READ | GENERIC_WRITE,
                                0, // no sharing → exclusive access
                                NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_WRITE_THROUGH,
                                NULL);

    if (hDrive == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to open %s (err=%lu)\n", drivePath, GetLastError());
        return 1;
    }

    printf("Type CONFIRM to proceed: ");
    char confirm[16];
    scanf("%15s", confirm);
    if (strcmp(confirm, "CONFIRM") != 0) {
        printf("Aborted.\n");
        CloseHandle(hDrive);
        return 0;
    }

    overwrite_pass(hDrive, 0x00, 1);
    overwrite_pass(hDrive, 0xFF, 2);
    overwrite_pass(hDrive, 0xAA, 3);

    CloseHandle(hDrive);

    printf("Purge operation completed.\n");
    return 0;
}
