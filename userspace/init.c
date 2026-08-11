#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <abi/syscalls.h>

typedef long scw;
extern long __do_syscall_ret(unsigned long);
extern scw __do_syscall3(long, scw, scw, scw);

static void report(const char *name, int ok) {
    printf("init: %s %s\n", name, ok ? "OK" : "FAIL");
}

static void spawn(const char *path, const char *name) {
    pid_t pid = fork();
    if (pid == 0) {
        char *const argv[] = { (char *)name, NULL };
        char *const envp[] = { NULL };
        execve(path, argv, envp);
        printf("init: execve(%s) failed: %s\n", path, strerror(errno));
        _exit(1);
    } else if (pid < 0) {
        printf("init: fork for %s failed: %s\n", name, strerror(errno));
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("init: start (mlibc build)\n");

    pid_t pid = getpid();
    report("getpid", pid > 0);

    uid_t uid = getuid();
    gid_t gid = getgid();
    printf("init: uid=%u gid=%u\n", (unsigned)uid, (unsigned)gid);
    report("getuid/getgid", 1);

    struct utsname uts;
    if (uname(&uts) == 0) {
        printf("init: uname sysname=%s machine=%s\n", uts.sysname, uts.machine);
        report("uname", strcmp(uts.sysname, "TinyKern") == 0);
    } else {
        report("uname", 0);
    }

    char cwd[256];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("init: cwd=%s\n", cwd);
        report("getcwd", 1);
    } else {
        report("getcwd", 0);
    }

    char *p = malloc(256);
    if (p) {
        strcpy(p, "heap works");
        free(p);
        report("malloc/free", 1);
    } else {
        report("malloc/free", 0);
    }

    DIR *d = opendir("/ram/bin");
    if (d) {
        int seen = 0;
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] != '.') {
                seen++;
                printf("init: bin: %s\n", ent->d_name);
            }
        }
        closedir(d);
        report("opendir/readdir", seen > 0);
    } else {
        report("opendir/readdir", 0);
    }

    int fd = open("/ram/bin/syscall_test", O_RDONLY);
    if (fd >= 0) {
        struct stat st;
        report("open/fstat", fstat(fd, &st) == 0 && st.st_size > 0);
        close(fd);
    } else {
        report("open/fstat", 0);
    }

    spawn("/ram/bin/user_idle", "user_idle");
    spawn("/ram/bin/sig_test", "sig_test");
    spawn("/ram/bin/syscall_test", "syscall_test");
    spawn("/ram/bin/all_test", "all_test");

    spawn("/ram/bin/sh", "sh");

    spawn("/ram/bin/tcc_link", "tcc_link");

    printf("init: reaping children\n");
    for (;;) {
        int st = 0;
        int r = (int)__do_syscall_ret(
            (unsigned long)__do_syscall3(SYS_WAITPID, -1, (scw)&st, (scw)0));
        printf("init: reaped %d status=%d\n", r, st);
    }
}
