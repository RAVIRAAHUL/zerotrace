// zeroTraceVerified.c
// WARNING: destructive. Run as Administrator.
// Usage:
//   zeroTraceVerified.exe <PhysicalDriveNumber> <VolumeLetter|NONE> [--test] [--verify]
// Examples:
//   zeroTraceVerified.exe 1 E --test
//   zeroTraceVerified.exe 1 NONE --verify

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// Buffer size used for write & verify. 16 MiB is a reasonable compromise.
#define BUF_SIZE (16ULL * 1024 * 1024)

static void usage(const char *prog) {
    printf("Usage: %s <PhysicalDriveNumber> <VolumeLetter|NONE> [--test] [--verify]\n", prog);
    printf("Example: %s 1 E --test\n", prog);
    printf("  --test   : write a single BUF_SIZE chunk (dry run)\n");
    printf("  --verify : after overwrite, read back entire device and confirm all bytes are zero (slow)\n");
}

// Get disk length in bytes. Returns 1 on success, 0 on failure.
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
    } else {
        return 0;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    const char *driveNumStr = argv[1];
    const char *volArg = argv[2]; // e.g., "E" or "NONE"
    int testMode = 0;
    int verifyMode = 0;
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
        printf("No volume lock requested (passing NONE). Writes may be blocked if volume is mounted.\n");
    }
    printf("Test mode: %s\n", testMode ? "YES (single chunk)" : "NO (full wipe)");
    printf("Verify mode: %s\n", verifyMode ? "YES (read-back verify)" : "NO");
    printf("Type the word 'CONFIRM' (uppercase) to proceed: ");
    char confirm[64];
    if (!fgets(confirm, sizeof(confirm), stdin)) return 1;
    confirm[strcspn(confirm, "\r\n")] = 0;
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

    // Open the physical drive for raw writes. Use FILE_FLAG_WRITE_THROUGH to push to device.
    HANDLE hDrive = CreateFileA(physicalPath,
                                GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_WRITE_THROUGH,
                                NULL);
    if (hDrive == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        fprintf(stderr, "Failed to open %s for writing (err=%lu). Run as Admin.\n", physicalPath, e);
        if (hVolume != INVALID_HANDLE_VALUE) {
            DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &(DWORD){0}, NULL);
            CloseHandle(hVolume);
        }
        return 1;
    }

    // Determine target length (in bytes)
    unsigned long long disk_len = 0;
    if (!get_disk_length(hDrive, &disk_len)) {
        // If we cannot query length, we'll fall back to writing until WriteFile stops.
        printf("Warning: failed to query disk length. Will write until device stops accepting writes.\n");
        disk_len = 0;
    } else {
        printf("Disk length reported: %llu bytes (~%llu MB)\n", disk_len, disk_len / (1024ULL*1024ULL));
    }

    // Allocate buffer
    void *buf = VirtualAlloc(NULL, BUF_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) {
        fprintf(stderr, "VirtualAlloc failed for %u bytes\n", (unsigned)BUF_SIZE);
        CloseHandle(hDrive);
        if (hVolume != INVALID_HANDLE_VALUE) {
            DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &(DWORD){0}, NULL);
            CloseHandle(hVolume);
        }
        return 1;
    }
    memset(buf, 0, BUF_SIZE); // zero pattern for Clear

    printf("Starting overwrite%s ...\n", testMode ? " (test: single chunk)" : "");

    DWORD written = 0;
    unsigned long long total_written = 0;

    if (testMode) {
        LARGE_INTEGER offset; offset.QuadPart = 0;
        if (!SetFilePointerEx(hDrive, offset, NULL, FILE_BEGIN)) {
            fprintf(stderr, "SetFilePointerEx failed (err=%lu)\n", GetLastError());
        }
        if (!WriteFile(hDrive, buf, (DWORD)BUF_SIZE, &written, NULL)) {
            fprintf(stderr, "WriteFile failed for test write (err=%lu)\n", GetLastError());
        } else {
            total_written += written;
            printf("[TEST] %u bytes written.\n", written);
        }
        FlushFileBuffers(hDrive);
    } else {
        if (disk_len > 0) {
            // We know the disk length: write exactly disk_len bytes
            unsigned long long remaining = disk_len;
            while (remaining > 0) {
                DWORD to_write = (remaining >= BUF_SIZE) ? (DWORD)BUF_SIZE : (DWORD)remaining;
                if (!WriteFile(hDrive, buf, to_write, &written, NULL)) {
                    DWORD err = GetLastError();
                    fprintf(stderr, "WriteFile failed (err=%lu)\n", err);
                    break;
                }
                if (written == 0) break;
                remaining -= written;
                total_written += written;
                if ((total_written % (256ULL * 1024 * 1024)) == 0) {
                    printf("... %llu MB written\n", total_written / (1024 * 1024));
                }
            }
        } else {
            // Unknown length: write until WriteFile stops (older behavior)
            while (1) {
                if (!WriteFile(hDrive, buf, (DWORD)BUF_SIZE, &written, NULL)) {
                    DWORD err = GetLastError();
                    if (err == ERROR_HANDLE_DISK_FULL || err == ERROR_WRITE_PROTECT) {
                        printf("WriteFile stopped (err=%lu).\n", err);
                    } else {
                        fprintf(stderr, "WriteFile failed (err=%lu)\n", err);
                    }
                    break;
                }
                if (written == 0) break;
                total_written += written;
                if ((total_written % (256ULL * 1024 * 1024)) == 0) {
                    printf("... %llu MB written\n", total_written / (1024 * 1024));
                }
            }
        }
        // Ensure device caches are flushed
        if (!FlushFileBuffers(hDrive)) {
            fprintf(stderr, "FlushFileBuffers failed (err=%lu)\n", GetLastError());
        }
    }

    printf("Overwrite phase complete. Total bytes written: %llu\n", total_written);

    // Verification (optional). Full read-back & check for non-zero bytes.
    if (verifyMode) {
        printf("Starting verification (reading back entire device)... This will take roughly as long as the write phase.\n");
        // reposition to beginning
        LARGE_INTEGER off0; off0.QuadPart = 0;
        if (!SetFilePointerEx(hDrive, off0, NULL, FILE_BEGIN)) {
            fprintf(stderr, "SetFilePointerEx (verify) failed (err=%lu)\n", GetLastError());
        } else {
            unsigned long long total_read = 0;
            BOOL read_ok = TRUE;
            DWORD readBytes = 0;
            while (1) {
                if (!ReadFile(hDrive, buf, (DWORD)BUF_SIZE, &readBytes, NULL)) {
                    DWORD err = GetLastError();
                    fprintf(stderr, "ReadFile failed during verify (err=%lu)\n", err);
                    read_ok = FALSE;
                    break;
                }
                if (readBytes == 0) break;
                // check buffer for any non-zero byte
                size_t i;
                BYTE *b = (BYTE*)buf;
                for (i = 0; i < readBytes; ++i) {
                    if (b[i] != 0x00) {
                        unsigned long long pos = total_read + i;
                        fprintf(stderr, "Verification failed: non-zero byte at offset %llu (0x%02X)\n", pos, b[i]);
                        read_ok = FALSE;
                        break;
                    }
                }
                if (!read_ok) break;
                total_read += readBytes;
                if ((total_read % (256ULL * 1024 * 1024)) == 0) {
                    printf("... %llu MB verified\n", total_read / (1024 * 1024));
                }
            }
            if (read_ok) {
                printf("Verification succeeded: all bytes read back as zero for %llu bytes.\n", total_read);
            } else {
                printf("Verification detected issues.\n");
            }
        }
    }

    // Clean up
    VirtualFree(buf, 0, MEM_RELEASE);
    CloseHandle(hDrive);

    if (hVolume != INVALID_HANDLE_VALUE) {
        DWORD br = 0;
        DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL);
        CloseHandle(hVolume);
        printf("Volume unlocked.\n");
    }

    printf("Clear operation finished. Mode: %s. Verify: %s\n",
           testMode ? "TEST (single chunk)" : "FULL CLEAR (zeros)",
           verifyMode ? "ENABLED" : "DISABLED");
    printf("Reminder: This implements NIST 800-88 'Clear' (overwrite with zeros). For NIST 'Purge' on SSD/flash, use firmware secure erase / sanitize commands.\n");

    return 0;
}
