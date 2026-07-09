#include <stdint.h>
#include <limine.h>
#include <memory.h>
#include <frame.h>
#include <hhdm.h>
#include <page.h>

#define PAGE_SIZE 0x1000

#define PAGE_PRESENT 0x1
#define PAGE_WRITABLE (0x1 << 1)
#define PAGE_USER (0x1 << 2)
#define PAGE_WRITE_THROUGH (0x1 << 3)
#define PAGE_DISABLE_CACHE (0x1 << 4)
#define PAGE_ACCESSED (0x1 << 5)
#define PAGE_DIRTY (0x1 << 6)
#define PAGE_NXE (1ULL << 63)

#define PAT0 (0)
#define PAT1 (ENTRY_FLAG_WRITETHROUGH)
#define PAT2 (ENTRY_FLAG_DISABLECACHE)
#define PAT3 (ENTRY_FLAG_DISABLECACHE | ENTRY_FLAG_WRITETHROUGH)
#define PAT4(PAT_FLAG) (PAT_FLAG)
#define PAT5(PAT_FLAG) ((PAT_FLAG) | ENTRY_FLAG_WRITETHROUGH)
#define PAT6(PAT_FLAG) ((PAT_FLAG) | ENTRY_FLAG_DISABLECACHE)
#define PAT7(PAT_FLAG) ((PAT_FLAG) | ENTRY_FLAG_DISABLECACHE | ENTRY_FLAG_WRITETHROUGH)

#define GET_PAGE_OFFSET(addr) ((addr) & 0xFFF)

#define PAGE_ADDR_MASK 0x000FFFFFFFFFF000ULL

uint64_t *kernel_pml4 = NULL;

extern char __text_start[], __text_end[];
extern char __rodata_start[], __rodata_end[];
extern char __data_start[], __data_end[];

typedef uint64_t page_entry_t;

static void invalidate_page(void *addr, uint64_t len) {
    for (uint64_t i = 0; i < len; i += 0x1000) {
        asm volatile("invlpg (%0)" : : "r"(addr + i) : "memory");
    }
}

void reload_cr3(uint64_t pml_to_load) {
    asm volatile("mov %0, %%cr3" : : "r"(pml_to_load) : "memory");
}

uint64_t get_pml(uint8_t level, void *addr) {
    if (level > 4 || level < 1) {
        return 0;
    }

    if (!addr) {
        return 0;
    }

    switch (level){
        case 1: 
            return ((uint64_t)addr >> 12) & 0x1FF; 
        case 2:
            return ((uint64_t)addr >> 21) & 0x1FF;
        case 3:
            return ((uint64_t)addr >> 30) & 0x1FF;
        case 4:
            return ((uint64_t)addr >> 39) & 0x1FF;
        default:
            break;
    }

    return 0;
}

uint64_t *paging_create_pml4() {
    uintptr_t *pml4 = (uintptr_t *)frame_alloc();

    if (!pml4) {
        return NULL;
    }

    memset(phys_to_virt((uintptr_t)pml4), 0, PAGE_SIZE);

    uintptr_t *pml4_virt = phys_to_virt(*pml4);

    for (int i = 256; i < 511; i++) {
        pml4_virt[i] = (uintptr_t)phys_to_virt(kernel_pml4[i]);
    }

    return pml4;
}

uint8_t paging_map_page(uint64_t *pml4_phys, void *virt_addr, uintptr_t phys_addr, uint64_t flags) {
    if (!pml4_phys) {
        return 1;
    }

    uint64_t *pml4 = (uint64_t *)phys_to_virt((uintptr_t)pml4_phys);
    uint64_t pml4_index = get_pml(4, virt_addr);
    if (!(pml4[pml4_index] & PAGE_PRESENT)) {
        uintptr_t new_table = frame_alloc();
        if (!new_table) {
            return 1;
        }
        memset(phys_to_virt(new_table), 0, PAGE_SIZE);
        pml4[pml4_index] = new_table | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }

    uint64_t *pml3 = (uint64_t *)phys_to_virt(pml4[pml4_index] & PAGE_ADDR_MASK);
    uint64_t pml3_index = get_pml(3, virt_addr);
    if (!(pml3[pml3_index] & PAGE_PRESENT)) {
        uintptr_t new_table = frame_alloc();
        if (!new_table) {
            return 1;
        }
        memset(phys_to_virt(new_table), 0, PAGE_SIZE);
        pml3[pml3_index] = new_table | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }

    uint64_t *pml2 = (uint64_t *)phys_to_virt(pml3[pml3_index] & PAGE_ADDR_MASK);
    uint64_t pml2_index = get_pml(2, virt_addr);
    if (!(pml2[pml2_index] & PAGE_PRESENT)) {
        uintptr_t new_table = frame_alloc();
        if (!new_table) {
            return 1;
        }
        memset(phys_to_virt(new_table), 0, PAGE_SIZE);
        pml2[pml2_index] = new_table | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }

    uint64_t *pml1 = (uint64_t *)phys_to_virt(pml2[pml2_index] & PAGE_ADDR_MASK);
    uint64_t pml1_index = get_pml(1, virt_addr);
    pml1[pml1_index] = (phys_addr & ~0xFFFULL) | flags | PAGE_PRESENT;

    invalidate_page(virt_addr, PAGE_SIZE);

    return 0;
}

void paging_unmap_page(uint64_t *pml4_phys, void *virt_addr) {
    if (!pml4_phys) {
        return;
    }

    uint64_t *pml4 = (uint64_t *)phys_to_virt((uintptr_t)pml4_phys);
    uint64_t i4 = get_pml(4, virt_addr);
    if (!(pml4[i4] & PAGE_PRESENT)) return;

    uint64_t *pml3 = (uint64_t *)phys_to_virt(pml4[i4] & PAGE_ADDR_MASK);
    uint64_t i3 = get_pml(3, virt_addr);
    if (!(pml3[i3] & PAGE_PRESENT)) return;

    uint64_t *pml2 = (uint64_t *)phys_to_virt(pml3[i3] & PAGE_ADDR_MASK);
    uint64_t i2 = get_pml(2, virt_addr);
    if (!(pml2[i2] & PAGE_PRESENT)) return;

    uint64_t *pml1 = (uint64_t *)phys_to_virt(pml2[i2] & PAGE_ADDR_MASK);
    uint64_t i1 = get_pml(1, virt_addr);
    if (!(pml1[i1] & PAGE_PRESENT)) return;

    uintptr_t phys_frame = pml1[i1] & PAGE_ADDR_MASK;

    pml1[i1] = 0;
    invalidate_page(virt_addr, PAGE_SIZE);

    frame_free(phys_frame);
}

static uint8_t map_kernel_range(uint64_t *pml4, uintptr_t virt_start, uintptr_t virt_end,
                                 uintptr_t phys_base, uintptr_t virt_base, uint64_t flags) {
    uintptr_t start = virt_start & ~0xFFFULL;
    uintptr_t end = (virt_end + PAGE_SIZE - 1) & ~0xFFFULL;

    for (uintptr_t va = start; va < end; va += PAGE_SIZE) {
        uintptr_t pa = phys_base + (va - virt_base);
        if (paging_map_page(pml4, (void *)va, pa, flags)) {
            return 1;
        }
    }

    return 0;
}

uint8_t paging_init(struct limine_memmap_response *memmap, struct limine_executable_address_response *exec) {
    uintptr_t *pml4 = (uintptr_t *)frame_alloc();

    if (!pml4) {
        return 1;
    }

    memset(phys_to_virt((uintptr_t)pml4), 0, PAGE_SIZE);

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE 
            || entry->type == LIMINE_MEMMAP_FRAMEBUFFER 
            || entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) {
            for (uint64_t offset = 0; offset < entry->length; offset += PAGE_SIZE) {
                if (paging_map_page((uint64_t *)pml4,
                                     phys_to_virt(entry->base + offset),
                                     entry->base + offset,
                                     PAGE_WRITABLE | PAGE_NXE)) {
                    return 1;
                }
            }
        }
    }

    if (map_kernel_range((uint64_t *)pml4,
                          (uintptr_t)__text_start, (uintptr_t)__text_end,
                          exec->physical_base, exec->virtual_base,
                          0)) {
        return 1;
    }

    if (map_kernel_range((uint64_t *)pml4,
                          (uintptr_t)__rodata_start, (uintptr_t)__rodata_end,
                          exec->physical_base, exec->virtual_base,
                          PAGE_NXE)) {
        return 1;
    }

    if (map_kernel_range((uint64_t *)pml4,
                          (uintptr_t)__data_start, (uintptr_t)__data_end,
                          exec->physical_base, exec->virtual_base,
                          PAGE_WRITABLE | PAGE_NXE)) {
        return 1;
    }
    
    if (map_kernel_range((uint64_t *)pml4,
                      exec->virtual_base, (uintptr_t)__text_start,
                      exec->physical_base, exec->virtual_base,
                      PAGE_NXE)) {
        return 1;
    }

    reload_cr3((uint64_t)pml4);

    uint64_t *res = 0;

    asm volatile ("mov %%cr3, %%rax" ::: "rax");
    asm volatile ("mov %%rax, %0" : "=r"(res) ::);

    kernel_pml4 = res;
 
    return 0;
}