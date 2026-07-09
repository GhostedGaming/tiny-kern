#pragma once

#include <stdint.h>
#include <limine.h>

void reload_cr3(uint64_t pml_to_load);
uint8_t paging_map_page(uint64_t *pml4_phys, void *virt_addr, uintptr_t phys_addr, uint64_t flags);
void paging_unmap_page(uint64_t *pml4_phys, void *virt_addr);
uint8_t paging_init(struct limine_memmap_response *memmap, struct limine_executable_address_response *exec);