#include <stdint.h>
#include <mm/page.h>
#include <mm/hhdm.h>
#include <acpi.h>
#include <apic.h>

#define IS_BSP 0x100
#define APIC_ENABLE 0x800

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

uint8_t apic_init() {
    // Map the LAPIC MMIO page
    paging_map_page(kernel_pml4, phys_to_virt(lapic_addr), lapic_addr, PAGE_PRESENT | PAGE_WRITABLE | PAGE_DISABLE_CACHE);

    volatile uint32_t *lapic = (volatile uint32_t *)phys_to_virt(lapic_addr);

    // Step 1: make sure xAPIC is globally enabled via IA32_APIC_BASE MSR (0x1B)
    uint64_t apic_base = read_msr(0x1B);
    apic_base |= APIC_ENABLE; // bit 11, global enable
    write_msr(0x1B, apic_base);

    // Step 2: software-enable via the SVR, offset 0xF0, register index 0xF0/4 = 0x3C in a uint32_t array
    lapic[0xF0 / 4] = lapic[0xF0 / 4] | 0x100 | 0xFF; // bit 8 = software enable, low byte = spurious vector
    return 0;
}
