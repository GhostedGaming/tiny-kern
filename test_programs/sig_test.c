#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile int got_usr1;
static volatile int got_usr2;

static void handler(int sig) {
    if (sig == SIGUSR1) {
        got_usr1++;
    } else if (sig == SIGUSR2) {
        got_usr2++;
    }
}

int main(void) {
    printf("sig_test: start\n");

    struct sigaction act;
    struct sigaction old;
    sigset_t mask, pend;

    memset(&act, 0, sizeof(act));
    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGUSR1, &act, &old) != 0) {
        printf("sig_test: FAIL sigaction: %s\n", strerror(errno));
        return 1;
    }

    pid_t pid = getpid();
    if (kill(pid, SIGUSR1) != 0) {
        printf("sig_test: FAIL raise: %s\n", strerror(errno));
        return 1;
    }
    if (got_usr1 != 1) {
        printf("sig_test: FAIL handler did not run\n");
        return 1;
    }
    printf("sig_test: raise handler OK\n");

    memset(&act, 0, sizeof(act));
    act.sa_handler = SIG_IGN;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGUSR2, &act, &old) != 0) {
        printf("sig_test: FAIL sigaction ignore: %s\n", strerror(errno));
        return 1;
    }
    if (kill(pid, SIGUSR2) != 0) {
        printf("sig_test: FAIL raise ignored: %s\n", strerror(errno));
        return 1;
    }
    printf("sig_test: ignore OK\n");

    memset(&act, 0, sizeof(act));
    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGUSR2, &act, &old) != 0) {
        printf("sig_test: FAIL sigaction reinstall: %s\n", strerror(errno));
        return 1;
    }

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) != 0) {
        printf("sig_test: FAIL sigprocmask block: %s\n", strerror(errno));
        return 1;
    }
    kill(pid, SIGUSR1);
    if (sigpending(&pend) != 0) {
        printf("sig_test: FAIL sigpending: %s\n", strerror(errno));
        return 1;
    }
    if (!sigismember(&pend, SIGUSR1)) {
        printf("sig_test: FAIL not pending\n");
        return 1;
    }
    printf("sig_test: blocked + pending OK\n");

    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0) {
        printf("sig_test: FAIL sigprocmask unblock: %s\n", strerror(errno));
        return 1;
    }
    if (got_usr1 != 2) {
        printf("sig_test: FAIL unblock did not deliver\n");
        return 1;
    }
    printf("sig_test: unblock delivered OK\n");

    if (kill(pid, SIGUSR1) != 0) {
        printf("sig_test: FAIL kill: %s\n", strerror(errno));
        return 1;
    }
    if (got_usr1 != 3) {
        printf("sig_test: FAIL kill not delivered\n");
        return 1;
    }
    printf("sig_test: kill self OK\n");

    printf("sig_test: triggering SIGSEGV, expect termination\n");
    *(volatile int *)0 = 1;
    for (;;);
}
