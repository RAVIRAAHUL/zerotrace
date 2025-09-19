Quick “one-sentence” you can tell the jury

“This program opens a physical drive, locks the filesystem if present, writes zeros to every logical block the host can reach, flushes the device cache, and optionally reads back the data to verify that every byte is zero — a standard ‘Clear’ per NIST 800-88, audited by readback verification.”

Header & macros — set up environment and compatibility
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


Purpose: include Windows APIs and C runtime functions we need (file I/O, device controls, memory, strings).

What to say: “We use standard Windows system APIs to talk safely to the operating system and the drive.”

#ifndef CTL_CODE
#define CTL_CODE(...) ...
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


Purpose: define control-code helpers (for MinGW compatibility). These macros let us build IOCTL codes if the toolchain headers don’t provide them.

What to say: “Compatibility glue so the program compiles across common Windows C toolchains.”

#ifndef FSCTL_LOCK_VOLUME
#define FSCTL_LOCK_VOLUME CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSCTL_UNLOCK_VOLUME CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 7, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define FSCTL_DISMOUNT_VOLUME CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 8, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif


Purpose: define the device control codes we use to lock, dismount and unlock a volume.

What to say: “These are the exact system calls we’ll ask Windows to perform to safely take the filesystem offline.”

#define BUF_SIZE (16ULL * 1024 * 1024)


Purpose: use a 16 MiB buffer for read/write operations. Big enough to be efficient, small enough to avoid OOM on normal systems.

What to say: “Writes occur in 16-megabyte chunks for reasonable speed without hogging RAM.”

Usage & helper
static void usage(const char *prog) { ... }


Purpose: prints how to run the program.

What to say: “This message tells an operator how to run the tool and explains test/verify options.”

static int get_disk_length(HANDLE hDrive, unsigned long long *out_len) { ... }


Purpose: uses DeviceIoControl(..., IOCTL_DISK_GET_LENGTH_INFO) to ask the OS for the drive’s logical size in bytes.

Why it matters: writing exactly the reported length avoids guessing and avoids relying on writing until an error occurs.

What to say: “We ask the OS for the exact logical length of the device and then plan writes to cover that exact range.”

main — argument parsing and modes
if (argc < 3) { usage(argv[0]); return 1; }
const char *driveNumStr = argv[1];
const char *volArg = argv[2];
int testMode = 0, verifyMode = 0;
for (int i = 3; i < argc; i++) { ... }


Purpose: require at least two args (physical drive number and volume letter or NONE); parse optional --test and --verify.

What to say: “The operator specifies which physical drive to wipe and whether to do a dry-run or a read-back verification.”

snprintf(physicalPath, sizeof(physicalPath), "\\\\.\\PhysicalDrive%s", driveNumStr);


Purpose: build the device path Windows expects for raw access, e.g. \\.\PhysicalDrive1.

What to say: “We open the physical device directly rather than a filesystem file.”

Confirmation prompt (safety)
printf("Type the word 'CONFIRM' ...");
if (!fgets(confirm, sizeof(confirm), stdin)) return 1;
confirm[strcspn(confirm, "\r\n")] = 0;
if (strcmp(confirm, "CONFIRM") != 0) { printf("Aborted..."); return 1; }


Purpose: prevents accidental runs; requires explicit operator confirmation.

What to say: “Operator must type CONFIRM — this is an explicit, documented safety step.”

Lock & dismount the volume (if provided)
if (strcmp(volArg, "NONE") != 0) {
    snprintf(volPath, sizeof(volPath), "\\\\.\\%s:", volArg);
    hVolume = CreateFileA(volPath, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hVolume == INVALID_HANDLE_VALUE) { ... }
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) { ... }
    if (!DeviceIoControl(hVolume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) { ... }
    printf("Volume %s: locked and dismounted successfully.\n", volArg);
}


Purpose & effect:

CreateFileA opens the logical volume (e.g., \\.\E:) so we can send IOCTLs.

FSCTL_LOCK_VOLUME prevents other processes/OS from writing while we proceed.

FSCTL_DISMOUNT_VOLUME tells the filesystem driver to unmount the filesystem so cached data won’t interfere.

What to say: “We politely ask the OS to lock and dismount the file system so we have exclusive, consistent access to the underlying device. If we can’t get exclusive access, the tool aborts rather than risk incomplete wipe.”

Open the physical drive (raw writes)
HANDLE hDrive = CreateFileA(physicalPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, NULL);
if (hDrive == INVALID_HANDLE_VALUE) { DWORD e = GetLastError(); ... }


Purpose: open the raw physical device with write-through semantics (each write pushed through OS cache to the device).

FILE_FLAG_WRITE_THROUGH explanation: it requests that writes bypass volatile caches where possible and be pushed toward persistent device buffers immediately (more conservative; helps with auditability).

What to say: “We open the drive for raw read/write and request that writes be sent through to the device for integrity.”

Query the disk length
unsigned long long disk_len = 0;
if (!get_disk_length(hDrive, &disk_len)) { printf("Warning: failed to query disk length..."); disk_len = 0; }
else printf("Disk length reported: %llu bytes (~%llu MB)\n", disk_len, disk_len/(1024ULL*1024ULL));


Purpose: prefer to write exactly the OS-reported logical range. If querying fails, the program falls back to writing until the device refuses more writes.

What to say: “When available, we write the exact logical size the OS gives us — this avoids overshooting or guessing.”

Allocate & zero the buffer
void *buf = VirtualAlloc(NULL, BUF_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
if (!buf) { fprintf(stderr, "VirtualAlloc failed"); CloseHandle(hDrive); ... }
memset(buf, 0, BUF_SIZE);


Purpose: allocate a contiguous 16 MiB buffer in virtual memory and fill it with zero bytes (the clear pattern).

What to say: “We prepare a 16MB chunk of zeros and repeatedly write that to the device.”

Overwrite logic — TEST mode
if (testMode) {
    LARGE_INTEGER offset; offset.QuadPart = 0;
    SetFilePointerEx(hDrive, offset, NULL, FILE_BEGIN);
    WriteFile(hDrive, buf, (DWORD)BUF_SIZE, &written, NULL);
    FlushFileBuffers(hDrive);
}


Steps:

Move the file pointer to the beginning.

Write one 16 MiB chunk.

Flush device buffers to push writes out.

What to say: “Test mode writes a single 16MB block to validate the write path and permissions without wiping the whole drive.”

Overwrite logic — FULL mode (known length)
if (disk_len > 0) {
    unsigned long long remaining = disk_len;
    while (remaining > 0) {
        DWORD to_write = (remaining >= BUF_SIZE) ? (DWORD)BUF_SIZE : (DWORD)remaining;
        if (!WriteFile(hDrive, buf, to_write, &written, NULL)) { handle_error; break; }
        if (written == 0) break;
        remaining -= written;
        total_written += written;
        if ((total_written % (256ULL * 1024 * 1024)) == 0) {
            printf("... %llu MB written\n", total_written / (1024 * 1024));
        }
    }
} else { // fallback if no length
    while (1) {
        if (!WriteFile(hDrive, buf, (DWORD)BUF_SIZE, &written, NULL)) { check errors and break; }
        if (written == 0) break;
        total_written += written;
        ...progress...
    }
}
if (!FlushFileBuffers(hDrive)) fprintf(stderr, "FlushFileBuffers failed ...");


Purpose: if we know the disk length, write exactly that many bytes; otherwise, write until the device refuses more writes.

Progress: print a message every 256 MiB.

FlushFileBuffers: ensures the OS pushes any cached data to the device (increases confidence that writes are persisted).

What to say: “We write zeros across the entire reported logical capacity, reporting progress and flushing caches at the end to ensure persistence.”

Verification (if --verify given)
if (verifyMode) {
    SetFilePointerEx(hDrive, off0, NULL, FILE_BEGIN);
    unsigned long long total_read = 0;
    BOOL read_ok = TRUE;
    DWORD readBytes = 0;
    while (1) {
        if (!ReadFile(hDrive, buf, (DWORD)BUF_SIZE, &readBytes, NULL)) { read_ok = FALSE; break; }
        if (readBytes == 0) break;
        for (i=0; i<readBytes; ++i) { if (b[i] != 0x00) { report failure; read_ok=FALSE; break; } }
        total_read += readBytes;
        ...progress...
    }
    if (read_ok) printf("Verification succeeded ...\n");
    else printf("Verification detected issues.\n");
}


Purpose: read back the entire device and check that every byte equals 0x00.

Safety: reads from the same path opened earlier; if reads fail, that’s reported.

What to say: “The verification pass reads every block and checks for any non-zero byte. A non-zero byte indicates either a failed write, a hidden/remapped sector unaffected by host writes (common on SSDs), or a device/reporting error.”

Cleanup & unlock volume
VirtualFree(buf, 0, MEM_RELEASE);
CloseHandle(hDrive);
if (hVolume != INVALID_HANDLE_VALUE) {
    DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &br, NULL);
    CloseHandle(hVolume);
    printf("Volume unlocked.\n");
}
printf("Clear operation finished. Mode: %s. Verify: %s\n", ...);


Purpose: release memory and handles, and unlock the volume if it was previously locked.

What to say: “We clean up all handles and resources and report a final audit message summarizing the operation.”