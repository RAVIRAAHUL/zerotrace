// smartPurgeTrace.c
// WARNING: destructive. Run as Administrator. Usage:
//   smartPurgeTrace.exe <PhysicalDriveNumber> <VolumeLetter|NONE>
//
// Example:
//   smartPurgeTrace.exe 1 D
//
// Performs selective multi-pass overwrite (Purge-like).
// Skips empty blocks, overwrites only those with non-zero data.

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE (64*1024*1024) // 64 MB buffer
#define PASSES 3

// Print usage instructions
void usage(const char *prog) {
    printf("Usage: %s <PhysicalDriveNumber> <VolumeLetter|NONE>\n", prog);
    printf("Example: %s 1 D\n", prog);
}

// Simple pseudo-random fill
void fill_random(BYTE *buf, size_t size) {
    for (size_t i=0; i<size; i++) buf[i] = (BYTE)(rand() % 256);
}

// Check if buffer has any non-zero byte
int is_nonzero(BYTE *buf, size_t size) {
    for (size_t i=0; i<size; i++) if (buf[i] != 0) return 1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 1; }

    const char *driveNumStr = argv[1];
    const char *volArg = argv[2];

    char physicalPath[64];
    snprintf(physicalPath, sizeof(physicalPath), "\\\\.\\PhysicalDrive%s", driveNumStr);

    printf("WARNING: This will overwrite data on %s\n", physicalPath);
    if (strcmp(volArg,"NONE") != 0) printf("Volume to lock/dismount: %s:\n", volArg);

    printf("Type the word 'CONFIRM' (uppercase) to proceed: ");
    char confirm[64];
    if (!fgets(confirm,sizeof(confirm),stdin)) return 1;
    confirm[strcspn(confirm,"\r\n")] = 0;
    if (strcmp(confirm,"CONFIRM") != 0) {
        printf("Aborted.\n"); return 1;
    }

    HANDLE hDrive = CreateFileA(
        physicalPath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_WRITE_THROUGH,
        NULL
    );
    if (hDrive == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        fprintf(stderr,"Failed to open %s (err=%lu). Run as Admin.\n", physicalPath,e);
        return 1;
    }

    // Allocate buffer
    void *buf = VirtualAlloc(NULL, BUF_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) {
        fprintf(stderr,"VirtualAlloc failed\n");
        CloseHandle(hDrive);
        return 1;
    }

    // Get drive size using IOCTL
    GET_LENGTH_INFORMATION lengthInfo;
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDrive, IOCTL_DISK_GET_LENGTH_INFO,
                         NULL, 0,
                         &lengthInfo, sizeof(lengthInfo),
                         &bytesReturned, NULL)) {
        fprintf(stderr,"IOCTL_DISK_GET_LENGTH_INFO failed (err=%lu)\n", GetLastError());
        VirtualFree(buf,0,MEM_RELEASE);
        CloseHandle(hDrive);
        return 1;
    }
    unsigned long long driveSizeBytes = lengthInfo.Length.QuadPart;
    unsigned long long totalBlocks = (driveSizeBytes + BUF_SIZE - 1) / BUF_SIZE;
    printf("Drive size: %llu bytes, total blocks: %llu\n", driveSizeBytes, totalBlocks);

    BYTE patterns[PASSES] = {0x00, 0xFF, 0xAA};

    LARGE_INTEGER offset = {0};
    for (unsigned long long blk=0; blk<totalBlocks; blk++) {
        // Read block
        SetFilePointerEx(hDrive, offset, NULL, FILE_BEGIN);
        DWORD readBytes = 0;
        if (!ReadFile(hDrive, buf, BUF_SIZE, &readBytes, NULL)) {
            fprintf(stderr,"ReadFile failed at block %llu\n", blk);
            break;
        }

        if (!is_nonzero((BYTE*)buf, readBytes)) {
            // Skip block if empty
            offset.QuadPart += readBytes;
            continue;
        }

        printf("[PURGE] Block %llu/%llu contains data. Overwriting...\n", blk+1, totalBlocks);

        // Multi-pass overwrite
        for (int pass=0; pass<PASSES; pass++) {
            if (patterns[pass] == 0xAA) fill_random((BYTE*)buf, readBytes);
            else memset(buf, patterns[pass], readBytes);

            SetFilePointerEx(hDrive, offset, NULL, FILE_BEGIN);
            DWORD written = 0;
            if (!WriteFile(hDrive, buf, readBytes, &written, NULL) || written != readBytes) {
                fprintf(stderr,"WriteFile failed at block %llu, pass %d\n", blk, pass+1);
            }
            FlushFileBuffers(hDrive);
        }

        offset.QuadPart += readBytes;
    }

    VirtualFree(buf, 0, MEM_RELEASE);
    CloseHandle(hDrive);

    printf("Selective multi-pass purge complete. Recovery extremely unlikely.\n");
    return 0;
}
