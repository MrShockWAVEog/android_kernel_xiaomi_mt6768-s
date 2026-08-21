#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/swap.h>

#define PAGE_SIZE 4096
#define SWAP_SIZE (512ULL * 1024 * 1024)

union swap_header {
    struct {
        char reserved[PAGE_SIZE - 10];
        char magic[10];
    } magic;
    struct {
        char     bootbits[1024];
        uint32_t version;
        uint32_t last_page;
        uint32_t nr_badpages;
        uint8_t  swp_uuid[16];
        uint8_t  volume_name[16];
        uint32_t padding[117];
        uint32_t badpages[1];
    } info;
};

int make_swap_header(const char *device_path) {
    int fd = open(device_path, O_RDWR);
    if (fd < 0) return -1;
    union swap_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.info.version = 1;
    hdr.info.last_page = (uint32_t)((SWAP_SIZE / PAGE_SIZE) - 1);
    hdr.info.nr_badpages = 0;
    memcpy(hdr.magic.magic, "SWAPSPACE2", 10);
    pwrite(fd, &hdr, PAGE_SIZE, 0);
    fsync(fd);
    close(fd);
    return 0;
}

void print_file_content(const char *path, const char *title) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("\n--- [%s] ---\n%s", title, buf);
    }
    close(fd);
}

void *monitor_thread(void *arg) {
    (void)arg;
    while (1) {
        sleep(2);

        int fd = open("/sys/kernel/mm/lru_gen/aging", O_WRONLY);
        if (fd >= 0) {
            write(fd, "+1\n", 3);
            close(fd);
        }

        print_file_content("/sys/block/zram0/mm_stat", "ZRAM (orig_data, compr_data, mem_used)");
        print_file_content("/proc/meminfo", "Meminfo (Swap / Free Durumu)");
        printf("\n\n");
    }
    return NULL;
}

void setup_system(void) {
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);

    int fd = open("/proc/sys/kernel/printk", O_WRONLY);
    if (fd >= 0) { write(fd, "8 8 8 8\n", 8); close(fd); }

    // MGLRU Tiering + Swapping + Aging (0x0007)
    fd = open("/sys/kernel/mm/lru_gen/enabled", O_WRONLY);
    if (fd >= 0) { write(fd, "7\n", 2); close(fd); }

    fd = open("/proc/sys/vm/swappiness", O_WRONLY);
    if (fd >= 0) { write(fd, "200\n", 4); close(fd); }

    fd = open("/proc/sys/vm/watermark_scale_factor", O_WRONLY);
    if (fd >= 0) { write(fd, "200\n", 4); close(fd); }

    fd = open("/proc/sys/vm/overcommit_memory", O_WRONLY);
    if (fd >= 0) { write(fd, "1\n", 2); close(fd); }

    fd = open("/sys/block/zram0/disksize", O_WRONLY);
    if (fd >= 0) { write(fd, "536870912\n", 10); close(fd); }

    if (make_swap_header("/dev/zram0") == 0) {
        swapon("/dev/zram0", 0);
        printf("[+] ZRAM Swap (512MB)!\n");
    }
}

static inline uint64_t xorshift64(uint64_t *s) {
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

void cold_page_hog(int id) {
    uint64_t rng = 0x1337BEEFULL + id;
    size_t chunk_mb = 16;
    size_t total_mb = 0;

    printf("[Worker %d] Started...\n", id);

    while (1) {
        size_t sz = chunk_mb * 1024 * 1024;
        char *ptr = (char *)mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            usleep(100000);
            continue;
        }

        for (size_t i = 0; i < sz; i += 4096) {
            ptr[i] = (char)xorshift64(&rng);
        }

        total_mb += chunk_mb;
        usleep(10000);
    }
}

int main(void) {
    setup_system();

    pthread_t th;
    pthread_create(&th, NULL, monitor_thread, NULL);

    printf("\n\n");
    printf("App started!!\n");

    for (int i = 0; i < 4; i++) {
        if (fork() == 0) {
            cold_page_hog(i);
            _exit(0);
        }
    }

    while (1) {
        int status;
        pid_t p = wait(&status);
        if (p > 0) {
            printf("\n[!] Worker PID %d closed (OOM/Crash).\n", p);
            if (fork() == 0) {
                cold_page_hog(99);
                _exit(0);
            }
        }
    }
    return 0;
}
