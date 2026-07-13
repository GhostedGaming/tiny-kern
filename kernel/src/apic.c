#include <stdint.h>
#include <mm/page.h>
#include <mm/hhdm.h>
#include <portio.h>
#include <acpi.h>
#include <apic.h>

#define IS_BSP 0x100
#define APIC_ENABLE 0x800

#define APIC_SVR         0xF0
#define APIC_LVT_TIMER   0x320
#define APIC_TIMER_DIV   0x3E0
#define APIC_TIMER_INIT  0x380
#define APIC_TIMER_CUR   0x390

uint64_t apic_count = 0x100000;

static inline uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a" (low), "=d"(high) : "c" (msr));
    return ((uint64_t)high << 32) | low;
}

static inline void write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile ("wrmsr" : : "c" (msr), "a" (low), "d" (high));
}

uint32_t apic_read(uint32_t reg) {
    volatile uint32_t *lapic = (volatile uint32_t *)phys_to_virt(lapic_addr);
    return lapic[reg / 4];
}

void apic_write(uint32_t reg, uint32_t value) {
    volatile uint32_t *lapic = (volatile uint32_t *)phys_to_virt(lapic_addr);
    lapic[reg / 4] = value;
}

void apic_eoi() {
    apic_write(0xB0, 0x0);
}

uint8_t apic_init() {
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    paging_map_page(kernel_pml4, phys_to_virt(lapic_addr), lapic_addr, PAGE_PRESENT | PAGE_WRITABLE | PAGE_DISABLE_CACHE);

    uint64_t apic_base = read_msr(0x1B);
    apic_base |= APIC_ENABLE;
    write_msr(0x1B, apic_base);

    apic_write(APIC_SVR, apic_read(APIC_SVR) | 0x100 | 0xFF);

    apic_write(APIC_TIMER_DIV, 0x3);
    apic_write(APIC_LVT_TIMER, 0x20 | 0x20000);
    apic_write(APIC_TIMER_INIT, apic_count);

    return 0;
}