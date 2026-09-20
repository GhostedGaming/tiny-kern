#pragma once

#include <mlibc-config.h>

#include <abi-bits/pid_t.h>
#include <abi-bits/sigval.h>
#include <abi-bits/sigset_t.h>
#include <abi-bits/uid_t.h>
#include <bits/ansi/clock_t.h>
#include <bits/size_t.h>
#include <bits/types.h>

#if __MLIBC_POSIX_OPTION
#include <abi-bits/sigevent.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int si_signo;
	int si_errno;
	int si_code;
	int si_pid;
	void *si_addr;
} siginfo_t;
#define si_addr si_addr

typedef void (*__sighandler)(int);

#define SIG_ERR ((__sighandler)(void *)(-1))
#define SIG_DFL ((__sighandler)(void *)(0))
#define SIG_IGN ((__sighandler)(void *)(1))

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

#define SIGSTKFLT 25
#define SIGXCPU   26
#define SIGXFSZ   27
#define SIGVTALRM 28
#define SIGPROF   29
#define SIGWINCH  30
#define SIGPWR    31
#define SIGIO     32
#define SIGPOLL   SIGIO
#define SIGINFO   SIGPWR
#define SIGCLD    SIGCHLD
#define SIGUNUSED SIGSYS
#define SIGCANCEL 33
#define SIGTIMER  34

#define SIGRTMIN 35
#define SIGRTMAX 64

#define NSIG 65
#define _NSIG NSIG

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO   0x00000004
#define SA_RESTART   0x00000008
#define SA_NODEFER   0x00000010
#define SA_RESETHAND 0x00000020

#define SA_NOMASK SA_NODEFER
#define SA_ONESHOT SA_RESETHAND

#define MINSIGSTKSZ 2048
#define SIGSTKSZ 8192

#define SS_ONSTACK 1
#define SS_DISABLE 2

#define SIGEV_SIGNAL 0
#define SIGEV_NONE 1
#define SIGEV_THREAD 2

#define SEGV_MAPERR 1
#define SEGV_ACCERR 2

#define BUS_ADRALN 1
#define BUS_ADRERR 2
#define BUS_OBJERR 3

#define ILL_ILLOPC 1
#define ILL_ILLOPN 2
#define ILL_ILLADR 3
#define ILL_ILLTRP 4
#define ILL_PRVOPC 5
#define ILL_PRVREG 6
#define ILL_COPROC 7
#define ILL_BADSTK 8
#define ILL_BADIADDR 9

#define SI_USER 0
#define SI_KERNEL 128
#define SI_QUEUE (-1)
#define SI_TIMER (-2)
#define SI_MESGQ (-3)
#define SI_ASYNCIO (-4)
#define SI_SIGIO (-5)
#define SI_TKILL (-6)
#define SI_ASYNCNL (-60)

typedef struct __stack {
	void *ss_sp;
	int ss_flags;
	size_t ss_size;
} stack_t;

struct sigaction {
	union {
		void (*sa_handler)(int);
		void (*sa_sigaction)(int, siginfo_t *, void *);
	};
	sigset_t sa_mask;
	int sa_flags;
};

typedef struct {
	unsigned long gregs[16];
	unsigned long pc, pr, sr;
	unsigned long gbr, mach, macl;
	unsigned long fpregs[16];
	unsigned long xfpregs[16];
	unsigned int fpscr, fpul, ownedfp;
} mcontext_t;

typedef struct __ucontext {
	struct __ucontext *uc_link;
	stack_t uc_stack;
	mcontext_t uc_mcontext;
	sigset_t uc_sigmask;
} ucontext_t;

#ifdef __cplusplus
}
#endif
