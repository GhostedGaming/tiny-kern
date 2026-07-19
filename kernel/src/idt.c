#include "multitasking/thread.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <logging/print.h>
#include <apic.h>
#include <idt.h>
#include <limine.h>

#define IDT_MAX_DESCRIPTORS 256

extern void* isr_stub_table[];
extern void apic_stub();
extern volatile struct limine_framebuffer_request framebuffer_request;

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

static const char* exception_names[32] = {
    "Divide by Zero", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
    "Page Fault", "Reserved", "x87 Floating-Point Exception",
    "Alignment Check", "Machine Check", "SIMD Floating-Point Exception",
    "Virtualization Exception", "Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Hypervisor Injection Exception", "VMM Communication Exception",
    "Security Exception", "Reserved"
};

void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags) {
    idt_entry_t* descriptor = &idt[vector];

    descriptor->isr_low        = (uint64_t)isr & 0xFFFF;
    descriptor->kernel_cs      = 0x08;
    descriptor->ist            = 0;
    descriptor->attributes     = flags;
    descriptor->isr_mid        = ((uint64_t)isr >> 16) & 0xFFFF;
    descriptor->isr_high       = ((uint64_t)isr >> 32) & 0xFFFFFFFF;
    descriptor->reserved       = 0;

    vectors[vector] = true;
}

void timer_handler() {
    print(".");
    apic_eoi();
    schedule();
}

void idt_init() {
    idtr.base = (uintptr_t)&idt[0];
    idtr.limit = (uint16_t)(sizeof(idt_entry_t) * IDT_MAX_DESCRIPTORS - 1);

    for (uint8_t vector = 0; vector < 32; vector++) {
        idt_set_descriptor(vector, isr_stub_table[vector], 0x8E);
    }

    idt_set_descriptor(0x20, apic_stub, 0x8E);

    __asm__ volatile ("lidt %0" : : "m"(idtr));
}

static void fill_screen_red() {
    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count < 1) {
        return;
    }

    struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];
    volatile uint32_t* pixels = (volatile uint32_t*)fb->address;
    uint32_t red = 0x00AA0000;

    for (size_t y = 0; y < fb->height; y++) {
        for (size_t x = 0; x < fb->width; x++) {
            pixels[y * (fb->pitch / 4) + x] = red;
        }
    }
}

__attribute__((noreturn))
void exception_handler(uint64_t vector, uint64_t error_code, uint64_t rip) {
    uint64_t cr2, cr3, cr4, rflags, cs, ss;

    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile ("pushfq; pop %0" : "=r"(rflags));
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    __asm__ volatile ("mov %%ss, %0" : "=r"(ss));

    fill_screen_red();

    const char* name = (vector < 32) ? exception_names[vector] : "Unknown";

    print("\n");
    print("================================================================\n");
    print("                        KERNEL PANIC\n");
    print("================================================================\n");
    print("Exception : %s (vector %llu)\n", name, vector);
    print("Error Code: 0x%016llx\n", error_code);
    print("RIP       : 0x%016llx\n", rip);
    print("CR2       : 0x%016llx\n", cr2);
    print("CR3       : 0x%016llx\n", cr3);
    print("CR4       : 0x%016llx\n", cr4);
    print("RFLAGS    : 0x%016llx\n", rflags);
    print("CS        : 0x%04llx\n", cs);
    print("SS        : 0x%04llx\n", ss);
    print("----------------------------------------------------------------\n");
    print("System halted.\n");
    print("================================================================\n");

    __asm__ volatile ("cli; hlt");
    for (;;) {}
}