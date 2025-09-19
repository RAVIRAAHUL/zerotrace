// zeroTraceFast.c
// WARNING: destructive. Run as Administrator. Usage:
//   zeroTraceFast.exe <PhysicalDriveNumber> <VolumeLetter|NONE> [--test]
//
// Example:
//   zeroTraceFast.exe 1 E --test   -> writes 512 MiB zeros to PhysicalDrive1 after locking E:
//   zeroTraceFast.exe 1 NONE       -> attempts full wipe of PhysicalDrive1 (no lock)

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Missing macros for MinGW (normally in MSVC headers) ---
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
// -----------------------------------------------------------

// FSCTL codes
#ifndef FSCTL_LOCK_VOLUME
#define FSCTL_LOCK_VOLUME CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef FSCTL_UNLOCK_VOLUME
#define FSCTL_UNLOCK_VOLUME CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 7, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef FSCTL_DISMOUNT_VOLUME
#define FSCTL_DISMOUNT_VOLUME CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 8, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#define BUF_SIZE (512ULL * 1024 * 1024) // 512 MiB

static void usage(const char *prog) {
    printf("Usage: %s <PhysicalDriveNumber> <VolumeLetter|NONE> [--test]\n", prog);
    printf("Example: %s 1 E --test\n", prog);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    // Parse args
    const char *driveNumStr = argv[1];
    const char *volArg = argv[2]; // e.g., "E" or "NONE"
    int testMode = 0;
    if (argc >= 4 && strcmp(argv[3], "--test") == 0) testMode = 1;

    char physicalPath[64];
    snprintf(physicalPath, sizeof(physicalPath), "\\\\.\\PhysicalDrive%s", driveNumStr);

    printf("WARNING: This will overwrite data on %s\n", physicalPath);
    if (strcmp(volArg, "NONE") != 0) {
        printf("Volume to lock/dismount: %s:\n", volArg);
    } else {
        printf("No volume lock requested (passing NONE). Writes may be blocked if volume is mounted.\n");
    }
    printf("Test mode: %s\n", testMode ? "YES (512 MiB only)" : "NO (full wipe)");
    printf("Type the word 'CONFIRM' (uppercase) to proceed: ");
    char confirm[64];
    if (!fgets(confirm, sizeof(confirm), stdin)) return 1;
    confirm[strcspn(confirm, "\r\n")] = 0; // strip newline
    if (strcmp(confirm, "CONFIRM") != 0) {
        printf("Aborted: confirmation not received.\n");
        return 1;
    }

    // If a volume letter was given, try to lock & dismount the volume first:
    HANDLE hVolume = INVALID_HANDLE_VALUE;
    if (strcmp(volArg, "NONE") != 0) {
        char volPath[16];
        snprintf(volPath, sizeof(volPath), "\\\\.\\%s:", volArg);
        hVolume = CreateFileA(volPath,
                              GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL);
        if (hVolume == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            fprintf(stderr, "Failed to open volume %s (err=%lu).\n", volPath, e);
            return 1;
        }

        DWORD bytesReturned = 0;
        if (!DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
            DWORD e = GetLastError();
            fprintf(stderr, "FSCTL_LOCK_VOLUME failed (err=%lu).\n", e);
            CloseHandle(hVolume);
            return 1;
        }
        if (!DeviceIoControl(hVolume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
            DWORD e = GetLastError();
            fprintf(stderr, "FSCTL_DISMOUNT_VOLUME failed (err=%lu).\n", e);
            DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
            CloseHandle(hVolume);
            return 1;
        }
        printf("Volume %s: locked and dismounted successfully.\n", volArg);
    }

    // Open the physical drive for raw writes
    HANDLE hDrive = CreateFileA(physicalPath,
                                GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                0, // <-- removed FILE_FLAG_WRITE_THROUGH
                                NULL);
    if (hDrive == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        fprintf(stderr, "Failed to open %s for writing (err=%lu).\n", physicalPath, e);
        if (hVolume != INVALID_HANDLE_VALUE) {
            DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &(DWORD){0}, NULL);
            CloseHandle(hVolume);
        }
        return 1;
    }

    // Allocate buffer via VirtualAlloc (aligned)
    void *buf = VirtualAlloc(NULL, BUF_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) {
        fprintf(stderr, "VirtualAlloc failed for 512 MB buffer\n");
        CloseHandle(hDrive);
        if (hVolume != INVALID_HANDLE_VALUE) {
            DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &(DWORD){0}, NULL);
            CloseHandle(hVolume);
        }
        return 1;
    }
    memset(buf, 0, BUF_SIZE);

    printf("Starting overwrite%s ...\n", testMode ? " (test: 512 MiB)" : "");

    DWORD written = 0;
    unsigned long long total = 0;
    if (testMode) {
        LARGE_INTEGER offset;
        offset.QuadPart = 0;
        if (!SetFilePointerEx(hDrive, offset, NULL, FILE_BEGIN)) {
            fprintf(stderr, "SetFilePointerEx failed (err=%lu)\n", GetLastError());
        }
        if (!WriteFile(hDrive, buf, BUF_SIZE, &written, NULL)) {
            fprintf(stderr, "WriteFile failed for test write (err=%lu)\n", GetLastError());
        } else {
            total += written;
            printf("Test write done: %u bytes written.\n", written);
        }
    } else {
        while (1) {
            if (!WriteFile(hDrive, buf, BUF_SIZE, &written, NULL)) {
                DWORD err = GetLastError();
                if (err == ERROR_HANDLE_DISK_FULL || err == ERROR_WRITE_PROTECT) {
                    printf("WriteFile stopped (err=%lu).\n", err);
                } else {
                    fprintf(stderr, "WriteFile failed (err=%lu)\n", err);
                }
                break;
            }
            if (written == 0) break;
            total += written;
            if ((total % (1024ULL * 1024 * 1024)) == 0) {
                printf("... %llu GB written\n", total / (1024ULL * 1024 * 1024));
            }
        }
    }

    VirtualFree(buf, 0, MEM_RELEASE);
    CloseHandle(hDrive);

    if (hVolume != INVALID_HANDLE_VALUE) {
        DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &(DWORD){0}, NULL);
        CloseHandle(hVolume);
        printf("Volume unlocked.\n");
    }

    printf("Overwrite complete. Total bytes written: %llu\n", total);
    printf("Reminder: This implements NIST 800-88 'Clear'. For 'Purge', use Secure Erase or sanitize commands.\n");
    return 0;
}
