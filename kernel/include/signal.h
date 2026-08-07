#pragma once

#include <stdint.h>
#include <stddef.h>

#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGIOT   SIGABRT
#define SIGEMT   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGBUS   10
#define SIGSEGV  11
#define SIGSYS   12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGURG   16
#define SIGSTOP  17
#define SIGTSTP  18
#define SIGCONT  19
#define SIGCHLD  20
#define SIGTTIN  21
#define SIGTTOU  22
#define SIGUSR1  23
#define SIGUSR2  24
#define NSIG     32

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)

typedef uint64_t sigset_t;

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO   0x00000004
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

#define SI_USER 0
#define SI_KERNEL 0x80

typedef struct siginfo {
    int si_signo;
    int si_errno;
    int si_code;
    int si_pid;
    uintptr_t si_addr;
} siginfo_t;

typedef struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t *, void *);
    };
    sigset_t sa_mask;
    int sa_flags;
} sigaction_t;

typedef struct sigstate {
    sigaction_t actions[NSIG];
    sigset_t pending;
    sigset_t blocked;
} sigstate_t;

typedef enum {
    SIG_ACTION_TERMINATE,
    SIG_ACTION_IGNORE,
    SIG_ACTION_CORE,
    SIG_ACTION_STOP,
    SIG_ACTION_CONTINUE,
} sig_default_action_t;

typedef struct user_context {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} user_context_t;

typedef struct sigframe {
    user_context_t ctx;
    sigset_t old_mask;
    siginfo_t info;
} sigframe_t;

typedef void (*sig_handler_t)(int);

#define SYS_RT_SIGACTION  13
#define SYS_RT_SIGPROCMASK 14
#define SYS_RT_SIGRETURN  15
#define SYS_KILL          62
#define SYS_RT_SIGPENDING 127
#define SYS_RT_SIGSUSPEND 130

struct pcb;

void (*signal(int sig, void (*handler)(int)))(int);
int sigaction(int sig, const sigaction_t *act, sigaction_t *oldact);
int kill(int pid, int sig);
int raise(int sig);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigpending(sigset_t *set);
int sigsuspend(const sigset_t *mask);
int sigreturn(user_context_t *ctx);
void default_sig_handler(int sig);
void sig_deliver_current(user_context_t *ctx);
void sig_deliver(struct pcb *p, int sig, user_context_t *ctx);

static inline int sigemptyset(sigset_t *set) {
    *set = 0;
    return 0;
}

static inline int sigfillset(sigset_t *set) {
    *set = ~(sigset_t)0;
    return 0;
}

static inline int sigaddset(sigset_t *set, int sig) {
    if (sig <= 0 || sig >= NSIG) return -1;
    *set |= (sigset_t)1 << (sig - 1);
    return 0;
}

static inline int sigdelset(sigset_t *set, int sig) {
    if (sig <= 0 || sig >= NSIG) return -1;
    *set &= ~((sigset_t)1 << (sig - 1));
    return 0;
}

static inline int sigismember(const sigset_t *set, int sig) {
    if (sig <= 0 || sig >= NSIG) return -1;
    return (int)((*set >> (sig - 1)) & 1);
}
