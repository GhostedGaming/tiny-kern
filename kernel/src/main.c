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
#include <mm/memory.h>
#include <storage/ahci.h>
#include <storage/drive_map.h>
#include <fs/devfs.h>
#include <fs/vfs.h>
#include <fs/ustar.h>
#include <multitasking/thread.h>
#include <multitasking/proc.h>
#include <multitasking/sched.h>
#include <binary_loaders/elf.h>
#include <input/input.h>
#include <tty.h>

extern void putchar(tty_t *tty, char c);
extern void jump_to_user(uint64_t entry, uint64_t stack);

#define USER_STACK_TOP 0x0000700000000000UL

static uint64_t g_init_entry;

static void enable_sse() {
    uint64_t cr0, cr4;

    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);
    cr0 |= (1ULL << 1);
    cr0 &= ~(1ULL << 3);
    asm volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");

    uint32_t eax, ebx, ecx, edx;
    asm volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                            : "a"(1), "c"(0));

    asm volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);
    if (ecx & (1u << 26)) {
        cr4 |= (1ULL << 18);
    }
    asm volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");

    uint32_t mxcsr = 0x1F80;
    asm volatile ("ldmxcsr %0" : : "m"(mxcsr));
}

static void init_thread_entry() {
    jump_to_user(g_init_entry, USER_STACK_TOP - 8);
    print("jump_to_user returned\n");
    for (;;) asm volatile ("hlt");
}

static void run_init() {
    int fd = open("/ram/bins/init", O_RDONLY);
    if (fd < 0) {
        print("open(/ram/bins/init) failed errno=%d\n", errno);
        return;
    }

    struct pcb *p = proc_create(init_thread_entry);
    if (!p) {
        print("proc_create failed\n");
        close(fd);
        return;
    }

    uint64_t entry = elf64_parse(fd, p->addr_space, NULL);
    close(fd);
    if (!entry) {
        print("elf64_parse failed\n");
        return;
    }

    g_init_entry = entry;
    print("loaded /ram/bins/init entry=0x%lX pid=%lu\n", entry, p->pid);

    schedule();
    print("scheduler returned\n");
}

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

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
	.id = LIMINE_MODULE_REQUEST_ID,
	.revision = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

static void hcf() {
	for (;;) {
		asm ("hlt");
	}
}

void kmain() {
	if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
		hcf();
	}

	if (framebuffer_request.response == NULL
		|| framebuffer_request.response->framebuffer_count < 1) {
		hcf();
	}

	gdt_init();
	syscall_setup();
	hhdm_init(hhdm_request.response->offset);
	frame_init(memmap_request.response);
	paging_init(memmap_request.response, executable_request.response);
	vmm_init();
	idt_init();
	acpi_parse_tables();
	apic_init();
	ahci_init();
	drive_map_init();
    vfs_init();    
    devfs_init();
    tty_init(putchar);
    input_init();

    enable_sse();

    if (module_request.response != NULL
        && module_request.response->module_count > 0) {
        struct limine_file *mod = module_request.response->modules[0];
        void *img = mod->address;
        uint8_t rc = ustar_mount("/ram", img, mod->size);
        print("mounted %s -> status %u\n", mod->path, rc);
        run_init();
    } else {
        print("no Limine modules loaded\n");
    }

	asm volatile ("sti");

	hcf();
}