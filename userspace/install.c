#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <abi/syscalls.h>

typedef long scw;
extern long __do_syscall_ret(unsigned long);
extern scw __do_syscall2(long, scw, scw);

#define SRC_DIR "/ram/bin"

static int syscall_mkfs(const char *dev) {
    return (int)__do_syscall_ret(
        (unsigned long)__do_syscall2(SYS_MKFS, (scw)dev, 0));
}

static int syscall_mount(const char *dev, const char *target) {
    return (int)__do_syscall_ret(
        (unsigned long)__do_syscall2(SYS_MOUNT, (scw)dev, (scw)target));
}

static int is_fat16(const char *dev) {
    int fd = open(dev, O_RDONLY);
    if (fd < 0) {
        printf("install: open %s: %s\n", dev, strerror(errno));
        return 0;
    }
    unsigned char boot[512];
    ssize_t n = read(fd, boot, sizeof(boot));
    close(fd);
    if (n != (ssize_t)sizeof(boot)) {
        printf("install: read %s: short read\n", dev);
        return 0;
    }
    if (boot[510] != 0x55 || boot[511] != 0xAA) {
        return 0;
    }
    return memcmp(boot + 0x36, "FAT16   ", 8) == 0;
}

static int copy_file(const char *src, const char *dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) {
        printf("install: open %s: %s\n", src, strerror(errno));
        return -1;
    }
    int out = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0755);
    if (out < 0) {
        printf("install: open %s: %s\n", dst, strerror(errno));
        close(in);
        return -1;
    }

    char buf[4096];
    ssize_t r;
    while ((r = read(in, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < r) {
            ssize_t w = write(out, buf + off, (size_t)(r - off));
            if (w < 0) {
                printf("install: write %s: %s\n", dst, strerror(errno));
                close(in);
                close(out);
                return -1;
            }
            off += w;
        }
    }
    if (r < 0) {
        printf("install: read %s: %s\n", src, strerror(errno));
    }
    close(in);
    close(out);
    return 0;
}

static int copy_dir(const char *src, const char *dst) {
    DIR *d = opendir(src);
    if (!d) {
        printf("install: opendir %s: %s\n", src, strerror(errno));
        return -1;
    }

    int copied = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char spath[256];
        char dpath[256];
        snprintf(spath, sizeof(spath), "%s/%s", src, ent->d_name);
        snprintf(dpath, sizeof(dpath), "%s/%s", dst, ent->d_name);

        struct stat st;
        if (stat(spath, &st) != 0) {
            printf("install: stat %s: %s\n", spath, strerror(errno));
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (copy_dir(spath, dpath) == 0) {
                copied++;
            }
            continue;
        }
        if (copy_file(spath, dpath) == 0) {
            copied++;
        }
    }
    closedir(d);
    return copied;
}

int main(int argc, char *argv[]) {
    const char *dev = (argc > 1) ? argv[1] : NULL;

    if (dev == NULL) {
        printf("install: no target device specified\n");
        return 1;
    }

    printf("install: target %s\n", dev);

    if (!is_fat16(dev)) {
        printf("install: %s is not a FAT16 volume, formatting\n", dev);
        if (syscall_mkfs(dev) != 0) {
            printf("install: mkfs %s: %s\n", dev, strerror(errno));
            return 1;
        }
        printf("install: formatted %s as FAT16\n", dev);
    } else {
        printf("install: %s already FAT16, keeping data\n", dev);
    }

    if (syscall_mount(dev, "bins") != 0) {
        if (errno == EBUSY) {
            printf("install: /bins already mounted\n");
        } else {
            printf("install: mount %s on /bins: %s\n", dev, strerror(errno));
            return 1;
        }
    } else {
        printf("install: mounted %s on /bins\n", dev);
    }

    int n = copy_dir(SRC_DIR, "/bins");
    if (n < 0) {
        printf("install: copy failed\n");
        return 1;
    }
    printf("install: copied %d files from %s to /bins\n", n, SRC_DIR);
    printf("install: done, /bins is persistent across reboots\n");
    return 0;
}
