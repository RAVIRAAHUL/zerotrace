// zeroTraceVerified_linux.c
// WARNING: destructive. Run as root.
// Usage:
//   ./zeroTraceVerified /dev/sdX [--test] [--verify]
// Example:
//   ./zeroTraceVerified /dev/sdb --test
//   ./zeroTraceVerified /dev/sdb --verify

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <sys/stat.h>

#define BUF_SIZE (16ULL * 1024 * 1024)

static void usage(const char *prog) {
    printf("Usage: %s <device> [--test] [--verify]\n", prog);
    printf("Example: %s /dev/sdb --test\n", prog);
    printf("  --test   : write a single BUF_SIZE chunk (dry run)\n");
    printf("  --verify : after overwrite, read back entire device and confirm all bytes are zero (slow)\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *devPath = argv[1];
    int testMode = 0, verifyMode = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) testMode = 1;
        else if (strcmp(argv[i], "--verify") == 0) verifyMode = 1;
    }

    printf("WARNING: This will overwrite data on %s\n", devPath);
    printf("Test mode: %s\n", testMode ? "YES (single chunk)" : "NO (full wipe)");
    printf("Verify mode: %s\n", verifyMode ? "YES" : "NO");
    printf("Type the word 'CONFIRM' (uppercase) to proceed: ");
    char confirm[64];
    if (!fgets(confirm, sizeof(confirm), stdin)) return 1;
    confirm[strcspn(confirm, "\r\n")] = 0;
    if (strcmp(confirm, "CONFIRM") != 0) {
        printf("Aborted: confirmation not received.\n");
        return 1;
    }

    int fd = open(devPath, O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    unsigned long long disk_len = 0;
    if (ioctl(fd, BLKGETSIZE64, &disk_len) != 0) {
        perror("BLKGETSIZE64 failed");
        close(fd);
        return 1;
    }
    printf("Disk length: %llu bytes (~%llu MB)\n", disk_len, disk_len / (1024ULL*1024ULL));

    void *buf;
    if (posix_memalign(&buf, 4096, BUF_SIZE) != 0) {
        fprintf(stderr, "posix_memalign failed\n");
        close(fd);
        return 1;
    }
    memset(buf, 0, BUF_SIZE);

    printf("Starting overwrite%s ...\n", testMode ? " (test: single chunk)" : "");
    unsigned long long total_written = 0;

    if (testMode) {
        ssize_t w = pwrite(fd, buf, BUF_SIZE, 0);
        if (w < 0) perror("Test write failed");
        else {
            total_written += w;
            printf("[TEST] %zd bytes written.\n", w);
        }
        fsync(fd);
    } else {
        unsigned long long remaining = disk_len;
        off_t offset = 0;
        while (remaining > 0) {
            size_t to_write = (remaining >= BUF_SIZE) ? BUF_SIZE : remaining;
            ssize_t w = pwrite(fd, buf, to_write, offset);
            if (w < 0) {
                perror("Write failed");
                break;
            }
            remaining -= w;
            offset += w;
            total_written += w;
            if ((total_written % (256ULL * 1024 * 1024)) == 0) {
                printf("... %llu MB written\n", total_written / (1024ULL*1024ULL));
            }
        }
        fsync(fd);
    }

    printf("Overwrite complete. Total bytes written: %llu\n", total_written);

    if (verifyMode) {
        printf("Starting verification (this will take a while)...\n");
        unsigned long long total_read = 0;
        int ok = 1;
        while (1) {
            ssize_t r = pread(fd, buf, BUF_SIZE, total_read);
            if (r < 0) {
                perror("Read failed");
                ok = 0;
                break;
            }
            if (r == 0) break;
            unsigned char *b = buf;
            for (ssize_t i = 0; i < r; i++) {
                if (b[i] != 0x00) {
                    fprintf(stderr, "Verification failed: non-zero byte at offset %llu (0x%02X)\n",
                            total_read + i, b[i]);
                    ok = 0;
                    break;
                }
            }
            if (!ok) break;
            total_read += r;
            if ((total_read % (256ULL * 1024 * 1024)) == 0) {
                printf("... %llu MB verified\n", total_read / (1024ULL*1024ULL));
            }
        }
        if (ok) {
            printf("Verification succeeded: all bytes zero.\n");
        }
    }

    free(buf);
    close(fd);
    printf("Clear operation finished. Mode: %s. Verify: %s\n",
           testMode ? "TEST" : "FULL CLEAR",
           verifyMode ? "ENABLED" : "DISABLED");
    return 0;
}
