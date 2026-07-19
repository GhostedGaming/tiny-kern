#include "multitasking/thread.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <gdt.h>
#include <mm/hhdm.h>
#include <mm/frame.h>
#include <mm/page.h>
#include <mm/vmm.h>
#include <idt.h>
#include <logging/print.h>
#include <acpi.h>
#include <apic.h>
#include <storage/ahci.h>

// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests")))
volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request executable_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

// Halt and catch fire function.
static void hcf(void) {
    for (;;) {
        asm ("hlt");
    }
}

void test() {
    for (;;) {
        print("Hello world! 1\n");
    }
}

void test1() {
    for (;;) {
        print("Hello world! 2\n");
    }
}

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
void kmain(void) {
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    gdt_init();
    hhdm_init(hhdm_request.response->offset);
    frame_init(memmap_request.response);
    paging_init(memmap_request.response, executable_request.response);
    vmm_init();
    idt_init();
    print_init();
    acpi_parse_tables();
    apic_init();
    ahci_init();

    asm volatile ("sti");
    create_thread(test);
    create_thread(test1);

    // We're done, just hang...
    hcf();
}
