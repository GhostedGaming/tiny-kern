#pragma once

#include <stdint.h>
#include <limine.h>

#define PAGE_SIZE 0x1000

#define PAGE_PRESENT 0x1
#define PAGE_WRITABLE (0x1 << 1)
#define PAGE_USER (0x1 << 2)
#define PAGE_WRITE_THROUGH (0x1 << 3)
#define PAGE_DISABLE_CACHE (0x1 << 4)
#define PAGE_ACCESSED (0x1 << 5)
#define PAGE_DIRTY (0x1 << 6)
#define PAGE_NXE (1ULL << 63)

extern uint64_t *kernel_pml4;

void reload_cr3(uint64_t pml_to_load);
uint64_t *paging_create_pml4();
uint8_t paging_map_page(uint64_t *pml4_phys, void *virt_addr, uintptr_t phys_addr, uint64_t flags);
void paging_unmap_page(uint64_t *pml4_phys, void *virt_addr);
uint8_t paging_init(struct limine_memmap_response *memmap, struct limine_executable_address_response *exec);
