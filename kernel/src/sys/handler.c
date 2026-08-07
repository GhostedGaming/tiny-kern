#include <stdint.h>
#include <logging/print.h>
#include <fs/vfs.h>
#include <multitasking/sched.h>
#include <multitasking/proc.h>
#include <signal.h>
#include <mm/mmap.h>
#include <mm/memory.h>
#include <apic.h>

extern void _exit(uint64_t exit_code);
extern int fork(void);
extern int execve(const char *path, char *const argv[], char *const envp[]);

#define UNDEFINED_SYSCALL 10000000

#define SYS_READ          0
#define SYS_WRITE         1
#define SYS_OPEN          2
#define SYS_CLOSE         3
#define SYS_STAT          4
#define SYS_FSTAT         5
#define SYS_LSTAT         6
#define SYS_LSEEK         8
#define SYS_MMAP          9
#define SYS_MPROTECT      10
#define SYS_MUNMAP        11
#define SYS_BRK           12
#define SYS_READV         19
#define SYS_WRITEV        20
#define SYS_DUP           32
#define SYS_DUP2          33
#define SYS_GETPID        39
#define SYS_FORK          57
#define SYS_EXECVE        59
#define SYS_EXIT          60
#define SYS_UNAME         63
#define SYS_GETCWD        79
#define SYS_CHDIR         80
#define SYS_UMASK         95
#define SYS_GETUID        102
#define SYS_GETGID        104
#define SYS_GETEUID       107
#define SYS_GETEGID       108
#define SYS_GETPPID       110
#define SYS_MOUNT         165
#define SYS_GETDENTS64    217
#define SYS_CLOCK_GETTIME 228
#define SYS_EXIT_GROUP    231
#define SYS_NEWFSTATAT    262

#define EFAULT_ERRNO 14
#define EINVAL_ERRNO 22

static uint64_t ret_errno(long ret) {
    if (ret < 0) {
        int e = errno;
        if (e > 0) {
            return (uint64_t)(-(int64_t)e);
        }
    }
    return (uint64_t)ret;
}

static uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t sys_clock_gettime(int clk, struct timespec *ts) {
    if (!ts) return (uint64_t)-EFAULT_ERRNO;
    if (clk != CLOCK_REALTIME && clk != CLOCK_MONOTONIC) return (uint64_t)-EINVAL_ERRNO;
    uint64_t tsc = rdtsc();
    ts->tv_sec = (int64_t)(tsc / 1000000000ULL);
    ts->tv_nsec = (int64_t)(tsc % 1000000000ULL);
    return 0;
}

static uint64_t sys_uname(struct utsname *u) {
    if (!u) return (uint64_t)-EFAULT_ERRNO;
    strcpy(u->sysname, "tiny-kern");
    strcpy(u->nodename, "tiny-kern");
    strcpy(u->release, "0.0.1");
    strcpy(u->version, "tiny-kern");
    strcpy(u->machine, "x86_64");
    strcpy(u->domainname, "");
    return 0;
}

uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, user_context_t *ctx) {
    uint64_t result = UNDEFINED_SYSCALL;

    switch (num) {
        case SYS_READ:
            result = ret_errno(read((int)arg1, (void *)arg2, (size_t)arg3));
            break;

        case SYS_WRITE:
            result = ret_errno(write((int)arg1, (const void *)arg2, (size_t)arg3));
            break;

        case SYS_OPEN:
            result = ret_errno(open((const char *)arg1, (int)arg2, (uint32_t)arg3));
            break;

        case SYS_CLOSE:
            result = ret_errno(close((int)arg1));
            break;

        case SYS_STAT:
            result = ret_errno(stat((const char *)arg1, (struct stat *)arg2));
            break;

        case SYS_FSTAT:
            result = ret_errno(fstat((int)arg1, (struct stat *)arg2));
            break;

        case SYS_LSTAT:
            result = ret_errno(lstat((const char *)arg1, (struct stat *)arg2));
            break;

        case SYS_NEWFSTATAT:
            result = ret_errno(fstatat((int)arg1, (const char *)arg2, (struct stat *)arg3, (int)arg4));
            break;

        case SYS_LSEEK:
            result = ret_errno(lseek((int)arg1, (off_t)arg2, (int)arg3));
            break;

        case SYS_MMAP:
            result = (uint64_t)sys_mmap((uintptr_t)arg1, (size_t)arg2, (int)arg3,
                                        (int)arg4, (int)arg5, 0);
            break;

        case SYS_MPROTECT:
            result = (uint64_t)sys_mprotect((uintptr_t)arg1, (size_t)arg2, (int)arg3);
            break;

        case SYS_MUNMAP:
            result = (uint64_t)sys_munmap((uintptr_t)arg1, (size_t)arg2);
            break;

        case SYS_BRK: {
            struct pcb *p = sched_current_proc();
            if (!p) {
                result = (uint64_t)-1;
            } else {
                result = (uint64_t)proc_sbrk(p, (intptr_t)arg1);
            }
            break;
        }

        case SYS_RT_SIGACTION:
            result = (uint64_t)sigaction((int)arg1, (const sigaction_t *)arg2, (sigaction_t *)arg3);
            break;

        case SYS_RT_SIGPROCMASK:
            result = (uint64_t)sigprocmask((int)arg1, (const sigset_t *)arg2, (sigset_t *)arg3);
            break;

        case SYS_RT_SIGPENDING:
            result = (uint64_t)sigpending((sigset_t *)arg1);
            break;

        case SYS_RT_SIGSUSPEND:
            result = (uint64_t)sigsuspend((const sigset_t *)arg1);
            break;

        case SYS_RT_SIGRETURN:
            sigreturn(ctx);
            break;

        case SYS_READV:
            result = ret_errno(readv((int)arg1, (const struct iovec *)arg2, (int)arg3));
            break;

        case SYS_WRITEV:
            result = ret_errno(writev((int)arg1, (const struct iovec *)arg2, (int)arg3));
            break;

        case SYS_DUP:
            result = ret_errno(dup((int)arg1));
            break;

        case SYS_DUP2:
            result = ret_errno(dup2((int)arg1, (int)arg2));
            break;

        case SYS_GETPID: {
            struct pcb *p = sched_current_proc();
            result = p ? (uint64_t)p->pid : (uint64_t)-1;
            break;
        }

        case SYS_GETPPID:
            result = 0;
            break;

        case SYS_FORK:
            result = (uint64_t)fork();
            break;

        case SYS_EXECVE:
            result = (uint64_t)execve((const char *)arg1, (char *const *)arg2, (char *const *)arg3);
            break;

        case SYS_EXIT:
        case SYS_EXIT_GROUP:
            _exit(arg1);
            break;

        case SYS_KILL:
            result = (uint64_t)kill((int)arg1, (int)arg2);
            break;

        case SYS_UNAME:
            result = sys_uname((struct utsname *)arg1);
            break;

        case SYS_GETCWD: {
            char *r = vfs_getcwd((char *)arg1, (size_t)arg2);
            result = r ? (uint64_t)arg1 : (uint64_t)(-(int64_t)errno);
            break;
        }

        case SYS_CHDIR:
            result = ret_errno(vfs_chdir((const char *)arg1));
            break;

        case SYS_UMASK: {
            struct pcb *p = sched_current_proc();
            uint32_t old = p ? p->umask : 0;
            if (p) {
                p->umask = (uint32_t)arg1 & 0777;
            }
            result = old;
            break;
        }

        case SYS_GETUID:
        case SYS_GETEUID:
            result = 0;
            break;

        case SYS_GETGID:
        case SYS_GETEGID:
            result = 0;
            break;

        case SYS_MOUNT:
            result = vfs_mount((char *)arg1, (uint8_t)arg2) == 0 ? 0 : (uint64_t)-1;
            break;

        case SYS_GETDENTS64:
            result = ret_errno(getdents64((int)arg1, (void *)arg2, (size_t)arg3));
            break;

        case SYS_CLOCK_GETTIME:
            result = sys_clock_gettime((int)arg1, (struct timespec *)arg2);
            break;
    }

    if (num != SYS_RT_SIGRETURN) {
        ctx->rax = result;
    }
    sig_deliver_current(ctx);

    return result;
}
