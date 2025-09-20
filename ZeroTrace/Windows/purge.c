// purge.c
// WARNING: destructive. Run as Administrator or SYSTEM.
// Usage:
// purge.exe <PhysicalDriveNumber> <VolumeLetter|NONE> [--test] [--verify]
// Example:
// purge.exe 1 E --test
// purge.exe 1 NONE --verify

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CTL_CODE
#define CTL_CODE(DeviceType, Function, Method, Access) ( \
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
#define FSCTL_LOCK_VOLUME CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSCTL_UNLOCK_VOLUME CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 7, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSCTL_DISMOUNT_VOLUME CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 8, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

// 16 MiB buffer
#define BUF_SIZE (16ULL * 1024 * 1024)

static void usage(const char *prog) {
    printf("Usage: %s <PhysicalDriveNumber> <VolumeLetter|NONE> [--test] [--verify]\n", prog);
    printf("Example: %s 1 E --test\n", prog);
    printf(" --test : write a single 16MB chunk (dry run)\n");
    printf(" --verify : after overwrite, read back entire device and confirm all bytes are zero\n");
}

static int get_disk_length(HANDLE hDrive, unsigned long long *out_len) {
    typedef struct {
        LARGE_INTEGER Length;
    } GET_LENGTH_INFORMATION;
    GET_LENGTH_INFORMATION info = {0};
    DWORD returned = 0;
    if (DeviceIoControl(hDrive, IOCTL_DISK_GET_LENGTH_INFO,
                        NULL, 0,
                        &info, sizeof(info),
                        &returned, NULL)) {
        *out_len = (unsigned long long)info.Length.QuadPart;
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    const char *driveNumStr = argv[1];
    const char *volArg = argv[2];
    int testMode = 0, verifyMode = 0;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) testMode = 1;
        else if (strcmp(argv[i], "--verify") == 0) verifyMode = 1;
    }

    char physicalPath[64];
    snprintf(physicalPath, sizeof(physicalPath), "\\\\.\\PhysicalDrive%s", driveNumStr);

    printf("WARNING: This will overwrite data on %s\n", physicalPath);
    if (strcmp(volArg, "NONE") != 0) {
        printf("Volume to lock/dismount: %s:\n", volArg);
    } else {
        printf("No volume lock requested. Writes may fail if volume is mounted.\n");
    }
    printf("Test mode: %s\n", testMode ? "YES" : "NO");
    printf("Verify mode: %s\n", verifyMode ? "YES" : "NO");
    printf("Type CONFIRM to proceed: ");

    char confirm[32];
    if (!fgets(confirm, sizeof(confirm), stdin)) return 1;
    confirm[strcspn(confirm, "\r\n")] = 0;
    if (strcmp(confirm, "CONFIRM") != 0) {
        printf("Aborted.\n");
        return 0;
    }

    // Try lock & dismount
    HANDLE hVolume = INVALID_HANDLE_VALUE;
    if (strcmp(volArg, "NONE") != 0) {
        char volPath[16];
        snprintf(volPath, sizeof(volPath), "\\\\.\\%s:", volArg);
        hVolume = CreateFileA(volPath, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, 0, NULL);
        if (hVolume != INVALID_HANDLE_VALUE) {
            DWORD br = 0;
            if (DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL) &&
                DeviceIoControl(hVolume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &br, NULL)) {
                printf("Volume %s locked & dismounted.\n", volArg);
            } else {
                fprintf(stderr, "Warning: failed to lock/dismount volume (err=%lu)\n", GetLastError());
            }
        }
    }

    HANDLE hDrive = CreateFileA(physicalPath,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING,
                                FILE_FLAG_WRITE_THROUGH, NULL);
    if (hDrive == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to open %s (err=%lu)\n", physicalPath, GetLastError());
        return 1;
    }

    unsigned long long disk_len = 0;
    get_disk_length(hDrive, &disk_len);
    if (disk_len) {
        printf("Disk length: %llu MB\n", disk_len / (1024ULL*1024ULL));
    } else {
        printf("Disk length unknown. Will write until failure.\n");
    }

    void *buf = VirtualAlloc(NULL, BUF_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    memset(buf, 0, BUF_SIZE);

    DWORD written;
    unsigned long long total = 0;

    if (testMode) {
        LARGE_INTEGER pos = {0};
        SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN);
        if (WriteFile(hDrive, buf, (DWORD)BUF_SIZE, &written, NULL)) {
            printf("[TEST] Wrote %u bytes.\n", written);
        } else {
            fprintf(stderr, "Test WriteFile failed (err=%lu)\n", GetLastError());
        }
    } else {
        unsigned long long remaining = disk_len;
        while (!disk_len || remaining > 0) {
            DWORD to_write = (disk_len && remaining < BUF_SIZE) ? (DWORD)remaining : (DWORD)BUF_SIZE;
            if (!WriteFile(hDrive, buf, to_write, &written, NULL)) {
                fprintf(stderr, "WriteFile failed (err=%lu)\n", GetLastError());
                break;
            }
            if (!written) break;
            total += written;
            if (disk_len) remaining -= written;
            if (total % (256ULL*1024*1024) == 0) {
                printf("... %llu MB written\n", total / (1024*1024));
            }
        }
        FlushFileBuffers(hDrive);
        printf("Overwrite done. Total %llu MB written.\n", total / (1024*1024));
    }

    if (verifyMode) {
        printf("Starting verify...\n");
        LARGE_INTEGER pos = {0};
        SetFilePointerEx(hDrive, pos, NULL, FILE_BEGIN);
        DWORD readBytes;
        unsigned long long totalRead = 0;
        BOOL ok = TRUE;
        while (ReadFile(hDrive, buf, (DWORD)BUF_SIZE, &readBytes, NULL) && readBytes) {
            BYTE *b = (BYTE*)buf;
            for (DWORD i = 0; i < readBytes; i++) {
                if (b[i] != 0x00) {
                    printf("Verify fail at offset %llu (0x%02X)\n", totalRead + i, b[i]);
                    ok = FALSE; break;
                }
            }
            if (!ok) break;
            totalRead += readBytes;
        }
        if (ok) printf("Verification succeeded (%llu MB).\n", totalRead / (1024*1024));
    }

    VirtualFree(buf, 0, MEM_RELEASE);
    CloseHandle(hDrive);
    if (hVolume != INVALID_HANDLE_VALUE) {
        DWORD br;
        DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL);
        CloseHandle(hVolume);
    }

    printf("Clear operation finished.\n");
    return 0;
}
