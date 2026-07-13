#include <stdint.h>
#include <stdbool.h>
#include <logging/print.h>
#include <apic.h>
#include <idt.h>

#define IDT_MAX_DESCRIPTORS 256

extern void* isr_stub_table[];
extern void apic_stub();

typedef struct {
    uint16_t    isr_low;
    uint16_t    kernel_cs;
    uint8_t     ist;
    uint8_t     attributes;
    uint16_t    isr_mid;
    uint32_t    isr_high;
    uint32_t    reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t    limit;
    uint64_t    base;
} __attribute__((packed)) idtr_t;

__attribute__((aligned(0x10)))
static idt_entry_t idt[IDT_MAX_DESCRIPTORS];

static idtr_t idtr;

static bool vectors[IDT_MAX_DESCRIPTORS];

void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags) {
    idt_entry_t* descriptor = &idt[vector];

    descriptor->isr_low        = (uint64_t)isr & 0xFFFF;
    descriptor->kernel_cs      = 0x08;
    descriptor->ist            = 0;
    descriptor->attributes     = flags;
    descriptor->isr_mid        = ((uint64_t)isr >> 16) & 0xFFFF;
    descriptor->isr_high       = ((uint64_t)isr >> 32) & 0xFFFFFFFF;
    descriptor->reserved       = 0;
}

void timer_handler() {
    print(".");
    apic_eoi();
}

void idt_init() {
    idtr.base = (uintptr_t)&idt[0];
    idtr.limit = (uint16_t)(sizeof(idt_entry_t) * IDT_MAX_DESCRIPTORS - 1);

    for (uint8_t vector = 0; vector < 32; vector++) {
        idt_set_descriptor(vector, isr_stub_table[vector], 0x8E);
        vectors[vector] = true;
    }

    idt_set_descriptor(0x20, apic_stub, 0x8E);
    vectors[0x20] = true;

    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

__attribute__((noreturn))
void exception_handler(uint64_t vector, uint64_t error_code, uint64_t rip) {
    print("EXCEPTION\n");
    __asm__ volatile ("cli; hlt");
    for (;;) {}
}