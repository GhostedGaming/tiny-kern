#include <stdint.h>
#include <logging/print.h>
#include <fs/vfs.h>

extern void _exit(uint64_t exit_code);

#define UNDEFINED_SYSCALL 10000000

#define SYS_EXIT 0
#define SYS_WRITE 1

uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    switch (num) {
        case SYS_EXIT: {
            (void)arg2; 
            (void)arg3;
            (void)arg4;
            (void)arg5;
            _exit(arg1);
            return 0;
        }

        case SYS_WRITE: {
            (void)arg4;
            (void)arg5;
            return vfs_write(arg1, (void *)arg2, arg3);
        }
    }

    return UNDEFINED_SYSCALL;
}