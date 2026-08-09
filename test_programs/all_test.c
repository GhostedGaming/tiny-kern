#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

static int failed;

static void check(const char *name, int ok) {
    printf("all_test: %s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok)
        failed = 1;
}

int main(void) {
    printf("all_test: start\n");

    struct stat st;
    if (stat("/ram/bins/init", &st) == 0) {
        check("stat", (st.st_mode & S_IFMT) == S_IFREG && st.st_size > 0);
    } else {
        check("stat", 0);
    }

    if (lstat("/ram/bins", &st) == 0) {
        check("lstat dir", (st.st_mode & S_IFMT) == S_IFDIR);
    } else {
        check("lstat dir", 0);
    }

    if (fstatat(AT_FDCWD, "/ram/bins/init", &st, 0) == 0) {
        check("fstatat", 1);
    } else {
        check("fstatat", 0);
    }

    int fd = open("/ram/bins/init", O_RDONLY);
    if (fd < 0) {
        check("open", 0);
    } else {
        check("open", 1);
        if (fstat(fd, &st) == 0) {
            check("fstat", st.st_size > 0);
        } else {
            check("fstat", 0);
        }
        char rbuf[32];
        struct iovec rv[2];
        rv[0].iov_base = rbuf;
        rv[0].iov_len = 16;
        rv[1].iov_base = rbuf + 16;
        rv[1].iov_len = 16;
        ssize_t n = readv(fd, rv, 2);
        check("readv", n == 32);
        close(fd);
    }

    struct iovec wv[2];
    const char *part1 = "all_test: writev part1\n";
    const char *part2 = "all_test: writev part2\n";
    wv[0].iov_base = (void *)part1;
    wv[0].iov_len = strlen(part1);
    wv[1].iov_base = (void *)part2;
    wv[1].iov_len = strlen(part2);
    ssize_t wn = writev(1, wv, 2);
    check("writev", wn == (ssize_t)(strlen(part1) + strlen(part2)));

    fd = open("/ram/bins", O_RDONLY);
    if (fd < 0) {
        check("open dir", 0);
    } else {
        check("open dir", 1);
        DIR *d = fdopendir(fd);
        if (d) {
            int total = 0;
            int found = 0;
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                total++;
                if (strcmp(ent->d_name, "init") == 0)
                    found = 1;
            }
            closedir(d);
            check("getdents", found && total >= 5);
        } else {
            check("getdents", 0);
        }
    }

    check("uid/gid", getuid() == 0 && getgid() == 0 && geteuid() == 0 && getegid() == 0);

    mode_t old_umask = umask(0);
    mode_t m = umask(0077);
    mode_t r = umask(0077);
    umask(old_umask);
    check("umask", m == 0 && r == 0077);

    struct utsname un;
    if (uname(&un) == 0) {
        check("uname", un.sysname[0] == 'T' && un.machine[0] == 'x');
    } else {
        check("uname", 0);
    }

    struct timespec ts1, ts2;
    if (clock_gettime(CLOCK_MONOTONIC, &ts1) == 0) {
        clock_gettime(CLOCK_MONOTONIC, &ts2);
        check("clock monotonic",
              ts2.tv_sec > ts1.tv_sec ||
              (ts2.tv_sec == ts1.tv_sec && ts2.tv_nsec > ts1.tv_nsec));
    } else {
        check("clock monotonic", 0);
    }

    void *p = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        check("mmap", 0);
    } else {
        check("mmap", 1);
        *(volatile long *)p = 0x1234;
        check("mmap write/read", *(volatile long *)p == 0x1234);
        check("mmap zero", *(volatile long *)((char *)p + 0x3000) == 0);
        if (mprotect(p, 0x4000, PROT_READ) == 0) {
            check("mprotect", 1);
        } else {
            check("mprotect", 0);
        }
        if (munmap(p, 0x4000) == 0) {
            check("munmap", 1);
        } else {
            check("munmap", 0);
        }
    }

    printf("all_test: %s\n", failed ? "FAILED" : "ALL OK");
    return failed;
}
