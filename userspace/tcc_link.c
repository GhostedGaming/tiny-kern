#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <abi/syscalls.h>

typedef long scw;
extern long __do_syscall_ret(unsigned long);
extern scw __do_syscall3(long, scw, scw, scw);

static int run(const char *path, char *const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        char *const envp[] = { NULL };
        execve(path, argv, envp);
        printf("tcc_link: execve(%s) failed: %s\n", path, strerror(errno));
        _exit(127);
    }
    if (pid < 0) {
        printf("tcc_link: fork failed: %s\n", strerror(errno));
        return -1;
    }
    int st = 0;
    long r = __do_syscall_ret(
        (unsigned long)__do_syscall3(SYS_WAITPID, pid, (scw)&st, 0));
    printf("tcc_link: %s pid=%ld status=%d\n", path, r, st);
    return (int)st;
}

int main(void) {
    printf("tcc_link: linking edit.c with on-OS tcc\n");

    char *const tcc_argv[] = {
        "tcc",
        "-I", "usr/include",
        "-nostdlib",
        "-static",
        "-o", "/ram/bin/edit2",
        "edit.c",
        "usr/lib/crt1.o",
        "usr/lib/crti.o",
        "usr/lib/crtn.o",
        "usr/lib/tcc/libtcc1.a",
        "usr/lib/libssp_nonshared.a",
        "usr/lib/libssp.a",
        "usr/lib/libpthread.a",
        "usr/lib/libutil.a",
        "usr/lib/libm.a",
        "usr/lib/libc.a",
        NULL
    };

    int rc = run("/ram/bin/tcc", tcc_argv);
    printf("tcc_link: tcc rc=%d\n", rc);

    if (rc == 0) {
        printf("tcc_link: link OK, running edit2\n");
        char *const edit_argv[] = { "edit2", NULL };
        int rc2 = run("/ram/bin/edit2", edit_argv);
        printf("tcc_link: edit2 rc=%d\n", rc2);
    }

    char *const tls_argv[] = {
        "tcc",
        "-I", "usr/include",
        "-nostdlib",
        "-static",
        "-o", "/ram/bin/tls2",
        "tls_test.c",
        "usr/lib/crt1.o",
        "usr/lib/crti.o",
        "usr/lib/crtn.o",
        "usr/lib/tcc/libtcc1.a",
        "usr/lib/libssp_nonshared.a",
        "usr/lib/libssp.a",
        "usr/lib/libpthread.a",
        "usr/lib/libutil.a",
        "usr/lib/libm.a",
        "usr/lib/libc.a",
        NULL
    };
    rc = run("/ram/bin/tcc", tls_argv);
    printf("tcc_link: tls tcc rc=%d\n", rc);
    if (rc == 0) {
        char *const tls_run_argv[] = { "tls2", NULL };
        int rc2 = run("/ram/bin/tls2", tls_run_argv);
        printf("tcc_link: tls2 rc=%d\n", rc2);
    }

    return 0;
}
