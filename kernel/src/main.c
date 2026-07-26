#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <gdt.h>
#include <idt.h>
#include <acpi.h>
#include <apic.h>
#include <logging/print.h>
#include <mm/heap.h>
#include <mm/hhdm.h>
#include <mm/frame.h>
#include <mm/page.h>
#include <mm/vmm.h>
#include <storage/ahci.h>
#include <storage/disk_writer.h>
#include <multitasking/thread.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

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

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

static void hcf(void) {
	for (;;) {
		asm ("hlt");
	}
}

void test() {
	int i = 0;
	for (;;) {
		void *buf = kmalloc(1024);
		if (!buf) {
			print("Out of memory at iteration %d\n", i);
			for (;;);
		}

		print("Hello world! 1\n");
		disk_writer(0, 0, i, 1, "Hello world!");
		disk_reader(0, 0, i, 1, buf);
		print("Buf contents: %s\n", (char *)buf);

		kfree(buf);
		i++;
	}
}

void test1() {
	int i = 20;
	for (;;) {
		void *buf = kmalloc(1024);
		if (!buf) {
			print("Out of memory at iteration %d\n", i);
			for (;;);
		}

		print("Hello world! 2\n");
		disk_writer(0, 0, i, 1, "Hello world!");
		disk_reader(0, 0, i, 1, buf);
		print("Buf contents: %s\n", (char *)buf);

		kfree(buf);
		i++;
	}
}

void kmain(void) {
	if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
		hcf();
	}

	if (framebuffer_request.response == NULL
		|| framebuffer_request.response->framebuffer_count < 1) {
		hcf();
	}

	gdt_init();
	hhdm_init(hhdm_request.response->offset);
	frame_init(memmap_request.response);
	paging_init(memmap_request.response, executable_request.response);
	print_init();
	vmm_init();
	idt_init();
	acpi_parse_tables();
	apic_init();
	ahci_init();

	asm volatile ("sti");

	create_thread(test);
	create_thread(test1);

	hcf();
}