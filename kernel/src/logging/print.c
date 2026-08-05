#include <stdint.h>
#include <stdarg.h>
#include <portio.h>
#include <sync/spinlock.h>
#include <logging/format.h>
#include <logging/print.h>

#define DEBUG_PORT 0xE9

static spinlock_t print_lock = 0;

static void putchar(char c) {
    if (c == '\n')
        outb(DEBUG_PORT, '\r');
    outb(DEBUG_PORT, c);
}

static void print_string(const char *s) {
    while (*s) {
        putchar(*s++);
    }
}

void print_impl(const char *caller, const char *fmt, ...) {
    uint64_t flags = spinlock_acquire_irqsave(&print_lock);

    print_string(caller);
    print_string("(): ");

    va_list list;
    va_start(list, fmt);
    format(putchar, fmt, list);
    va_end(list);

    spinlock_release_irqrestore(&print_lock, flags);
}
