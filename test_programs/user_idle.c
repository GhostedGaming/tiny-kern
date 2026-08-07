static inline long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    asm volatile ("int $0x80"
                  : "=a"(ret)
                  : "D"(num), "S"(a1), "d"(a2), "c"(a3)
                  : "memory");
    return ret;
}

void _start() {
    syscall3(1, 1, (long)"User idle thread", 16);
    for (;;) {
        asm volatile ("pause");
    }
}
