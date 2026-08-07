#include <stdint.h>
#include <logging/print.h>
#include <fs/vfs.h>
#include <multitasking/sched.h>
#include <multitasking/proc.h>
#include <signal.h>

extern void _exit(uint64_t exit_code);
extern int fork(void);
extern int execve(const char *path, char *const argv[], char *const envp[]);

#define UNDEFINED_SYSCALL 10000000

#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_FORK 2
#define SYS_READ 3
#define SYS_OPEN 4
#define SYS_CLOSE 5
#define SYS_LSEEK 6
#define SYS_DUP 7
#define SYS_DUP2 8
#define SYS_BRK 9
#define SYS_GETPID 10
#define SYS_EXECVE 11
#define SYS_MOUNT 12

uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, user_context_t *ctx) {
    (void)arg4;
    (void)arg5;

    uint64_t result = UNDEFINED_SYSCALL;

    switch (num) {
        case SYS_EXIT:
            _exit(arg1);
            break;

        case SYS_WRITE:
            result = (uint64_t)write((int)arg1, (const void *)arg2, (size_t)arg3);
            break;

        case SYS_FORK:
            result = (uint64_t)fork();
            break;

        case SYS_READ:
            result = (uint64_t)read((int)arg1, (void *)arg2, (size_t)arg3);
            break;

        case SYS_OPEN:
            result = (uint64_t)open((const char *)arg1, (int)arg2, (uint32_t)arg3);
            break;

        case SYS_CLOSE:
            result = (uint64_t)close((int)arg1);
            break;

        case SYS_LSEEK:
            result = (uint64_t)lseek((int)arg1, (off_t)arg2, (int)arg3);
            break;

        case SYS_DUP:
            result = (uint64_t)dup((int)arg1);
            break;

        case SYS_DUP2:
            result = (uint64_t)dup2((int)arg1, (int)arg2);
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

        case SYS_GETPID: {
            struct pcb *p = sched_current_proc();
            result = p ? (uint64_t)p->pid : (uint64_t)-1;
            break;
        }

        case SYS_EXECVE:
            result = (uint64_t)execve((const char *)arg1, (char *const *)arg2, (char *const *)arg3);
            break;

        case SYS_MOUNT:
            result = vfs_mount((char *)arg1, (uint8_t)arg2) == VFS_OK ? 0 : -1;
            break;

        case SYS_SIGACTION:
            result = (uint64_t)sigaction((int)arg1, (const sigaction_t *)arg2, (sigaction_t *)arg3);
            break;

        case SYS_SIGPROCMASK:
            result = (uint64_t)sigprocmask((int)arg1, (const sigset_t *)arg2, (sigset_t *)arg3);
            break;

        case SYS_SIGPENDING:
            result = (uint64_t)sigpending((sigset_t *)arg1);
            break;

        case SYS_SIGSUSPEND:
            result = (uint64_t)sigsuspend((const sigset_t *)arg1);
            break;

        case SYS_SIGRETURN:
            sigreturn(ctx);
            break;

        case SYS_KILL:
            result = (uint64_t)kill((int)arg1, (int)arg2);
            break;

        case SYS_RAISE:
            result = (uint64_t)raise((int)arg1);
            break;
    }

    if (num != SYS_SIGRETURN) {
        ctx->rax = result;
    }
    sig_deliver_current(ctx);

    return result;
}
