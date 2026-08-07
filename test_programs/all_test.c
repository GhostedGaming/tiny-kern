#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_STAT 4
#define SYS_FSTAT 5
#define SYS_LSTAT 6
#define SYS_MMAP 9
#define SYS_MPROTECT 10
#define SYS_MUNMAP 11
#define SYS_READV 19
#define SYS_WRITEV 20
#define SYS_GETPID 39
#define SYS_UNAME 63
#define SYS_GETCWD 79
#define SYS_CHDIR 80
#define SYS_UMASK 95
#define SYS_GETUID 102
#define SYS_GETGID 104
#define SYS_GETEUID 107
#define SYS_GETEGID 108
#define SYS_GETDENTS64 217
#define SYS_CLOCK_GETTIME 228
#define SYS_EXIT_GROUP 231
#define SYS_NEWFSTATAT 262

#define O_RDONLY 0

#define PROT_READ 0x1
#define PROT_WRITE 0x2

#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20

#define CLOCK_MONOTONIC 1

#define S_IFMT 0170000
#define S_IFREG 0100000

static inline long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    asm volatile ("syscall"
                  : "=a"(ret)
                  : "a"(num), "D"(a1), "S"(a2), "d"(a3)
                  : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall4(long num, long a1, long a2, long a3, long a4) {
    long ret;
    asm volatile ("syscall"
                  : "=a"(ret)
                  : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(a4)
                  : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall6(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8 asm("r8") = a5;
    register long r9 asm("r9") = a6;
    asm volatile ("syscall"
                  : "=a"(ret)
                  : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                  : "rcx", "r11", "memory");
    return ret;
}

struct timespec {
    long tv_sec;
    long tv_nsec;
};

struct stat {
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned long st_nlink;
    unsigned int st_mode;
    unsigned int st_uid;
    unsigned int st_gid;
    int __pad0;
    unsigned long st_rdev;
    long st_size;
    long st_blksize;
    long st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    long __unused[3];
};

struct iovec {
    void *iov_base;
    long iov_len;
};

struct linux_dirent64 {
    unsigned long d_ino;
    long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

static void write_str(const char *s) {
    long n = 0;
    while (s[n]) n++;
    syscall3(SYS_WRITE, 1, (long)s, n);
}

static int fail(const char *s) {
    write_str(s);
    syscall6(SYS_EXIT_GROUP, 1, 0, 0, 0, 0, 0);
    for (;;);
}

void _start() {
    write_str("all_test: start\n");

    struct stat st;
    if (syscall3(SYS_STAT, (long)"/ram/bins/init", (long)&st, 0) != 0)
        fail("all_test: FAIL stat\n");
    if ((st.st_mode & S_IFMT) != S_IFREG)
        fail("all_test: FAIL stat mode\n");
    if (st.st_size <= 0)
        fail("all_test: FAIL stat size\n");
    write_str("all_test: stat OK\n");

    if (syscall3(SYS_LSTAT, (long)"/ram/bins", (long)&st, 0) != 0)
        fail("all_test: FAIL lstat\n");
    if ((st.st_mode & S_IFMT) != (040000))
        fail("all_test: FAIL lstat dir mode\n");
    write_str("all_test: lstat OK\n");

    if (syscall4(SYS_NEWFSTATAT, -100, (long)"/ram/bins/init", (long)&st, 0) != 0)
        fail("all_test: FAIL fstatat\n");
    write_str("all_test: fstatat OK\n");

    long fd = syscall3(SYS_OPEN, (long)"/ram/bins/init", O_RDONLY, 0);
    if (fd < 0)
        fail("all_test: FAIL open\n");
    if (syscall3(SYS_FSTAT, fd, (long)&st, 0) != 0)
        fail("all_test: FAIL fstat\n");
    write_str("all_test: fstat OK\n");

    char rbuf[64];
    struct iovec rv[2];
    rv[0].iov_base = rbuf;
    rv[0].iov_len = 16;
    rv[1].iov_base = rbuf + 16;
    rv[1].iov_len = 16;
    long n = syscall6(SYS_READV, fd, (long)rv, 2, 0, 0, 0);
    if (n != 32)
        fail("all_test: FAIL readv\n");
    write_str("all_test: readv OK\n");

    struct iovec wv[2];
    const char *part1 = "all_test: writev part1\n";
    const char *part2 = "all_test: writev part2\n";
    wv[0].iov_base = (void *)part1;
    wv[0].iov_len = 24;
    wv[1].iov_base = (void *)part2;
    wv[1].iov_len = 24;
    n = syscall6(SYS_WRITEV, 1, (long)wv, 2, 0, 0, 0);
    if (n != 48)
        fail("all_test: FAIL writev\n");
    syscall3(SYS_CLOSE, fd, 0, 0);

    fd = syscall3(SYS_OPEN, (long)"/ram/bins", O_RDONLY, 0);
    if (fd < 0)
        fail("all_test: FAIL open dir\n");
    char dbuf[512];
    long found = 0;
    long total = 0;
    for (;;) {
        long cnt = syscall3(SYS_GETDENTS64, fd, (long)dbuf, sizeof(dbuf));
        if (cnt <= 0) break;
        long off = 0;
        while (off < cnt) {
            struct linux_dirent64 *de = (struct linux_dirent64 *)(dbuf + off);
            total++;
            if (de->d_name[0] == 'i' && de->d_name[1] == 'n' &&
                de->d_name[2] == 'i' && de->d_name[3] == 't' &&
                de->d_name[4] == 0)
                found = 1;
            off += de->d_reclen;
        }
        if (cnt < (long)sizeof(dbuf)) break;
    }
    syscall3(SYS_CLOSE, fd, 0, 0);
    if (!found || total < 5)
        fail("all_test: FAIL getdents64\n");
    write_str("all_test: getdents64 OK\n");

    long uid = syscall3(SYS_GETUID, 0, 0, 0);
    long gid = syscall3(SYS_GETGID, 0, 0, 0);
    long euid = syscall3(SYS_GETEUID, 0, 0, 0);
    long egid = syscall3(SYS_GETEGID, 0, 0, 0);
    if (uid != 0 || gid != 0 || euid != 0 || egid != 0)
        fail("all_test: FAIL uid/gid\n");
    write_str("all_test: uid/gid OK\n");

    long old_umask = syscall3(SYS_UMASK, 0, 0, 0);
    if (syscall3(SYS_UMASK, old_umask, 0, 0) != 0)
        fail("all_test: FAIL umask restore-orig\n");
    if (syscall3(SYS_UMASK, 0077, 0, 0) != old_umask)
        fail("all_test: FAIL umask\n");
    if (syscall3(SYS_UMASK, old_umask, 0, 0) != 0077)
        fail("all_test: FAIL umask restore\n");
    write_str("all_test: umask OK\n");

    struct utsname un;
    if (syscall3(SYS_UNAME, (long)&un, 0, 0) != 0)
        fail("all_test: FAIL uname\n");
    if (un.sysname[0] != 't')
        fail("all_test: FAIL uname sysname\n");
    if (un.machine[0] != 'x')
        fail("all_test: FAIL uname machine\n");
    write_str("all_test: uname OK\n");

    struct timespec ts1, ts2;
    if (syscall3(SYS_CLOCK_GETTIME, CLOCK_MONOTONIC, (long)&ts1, 0) != 0)
        fail("all_test: FAIL clock_gettime\n");
    long spins = 0;
    do {
        syscall3(SYS_CLOCK_GETTIME, CLOCK_MONOTONIC, (long)&ts2, 0);
        spins++;
    } while (ts2.tv_sec == ts1.tv_sec && ts2.tv_nsec == ts1.tv_nsec && spins < 100000);
    if (ts2.tv_sec < ts1.tv_sec || (ts2.tv_sec == ts1.tv_sec && ts2.tv_nsec <= ts1.tv_nsec))
        fail("all_test: FAIL clock monotonic\n");
    write_str("all_test: clock_gettime OK\n");

    long p = syscall6(SYS_MMAP, 0, 0x4000, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p <= 0 || (p & 0xFFF) != 0)
        fail("all_test: FAIL mmap\n");
    *(volatile long *)p = 0x1234;
    if (*(volatile long *)p != 0x1234)
        fail("all_test: FAIL mmap write/read\n");
    if (*(volatile long *)(p + 0x3000) != 0)
        fail("all_test: FAIL mmap zero\n");
    write_str("all_test: mmap OK\n");

    if (syscall6(SYS_MPROTECT, p, 0x4000, PROT_READ, 0, 0, 0) != 0)
        fail("all_test: FAIL mprotect\n");
    write_str("all_test: mprotect OK\n");

    if (syscall6(SYS_MUNMAP, p, 0x4000, 0, 0, 0, 0) != 0)
        fail("all_test: FAIL munmap\n");
    long p2 = syscall6(SYS_MMAP, 0, 0x1000, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p2 <= 0)
        fail("all_test: FAIL mmap after munmap\n");
    write_str("all_test: munmap OK\n");

    write_str("all_test: ALL OK\n");
    syscall6(SYS_EXIT_GROUP, 0, 0, 0, 0, 0, 0);
    for (;;);
}
