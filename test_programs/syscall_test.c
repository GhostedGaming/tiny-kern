#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

static int failed;

static void check(const char *name, int ok) {
    printf("syscall_test: %s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok)
        failed = 1;
}

int main(void) {
    printf("syscall_test: start\n");

    pid_t pid = getpid();
    check("getpid", pid > 0);

    char cwd[256];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        check("getcwd", 0);
    } else {
        printf("syscall_test: cwd=%s\n", cwd);
        check("getcwd", 1);
    }

    struct utsname uts;
    if (uname(&uts) == 0) {
        printf("syscall_test: uname sysname=%s release=%s\n", uts.sysname, uts.release);
        check("uname", 1);
    } else {
        check("uname", 0);
    }

    uid_t uid = getuid();
    gid_t gid = getgid();
    uid_t euid = geteuid();
    gid_t egid = getegid();
    printf("syscall_test: pid=%d uid=%d gid=%d euid=%d egid=%d\n",
           (int)pid, (int)uid, (int)gid, (int)euid, (int)egid);
    check("uid/gid", uid == 0 && gid == 0 && euid == 0 && egid == 0);

    printf("syscall_test: %s\n", failed ? "FAILED" : "ALL OK");
    return failed;
}
