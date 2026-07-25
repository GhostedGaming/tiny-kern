#include "logging/print.h"
#include <stddef.h>
#include <mm/vmm.h>
#include <mm/page.h>
#include <mm/heap.h>

#define KHEAP_START 0xFFFF800000000000UL
#define KHEAP_MAX_SIZE (64UL * 1024 * 1024)

static uintptr_t heap_cursor = KHEAP_START;

void *kmalloc(uintptr_t size) {
    if (size == 0) {
        return NULL;
    }

    int pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    if (heap_cursor + (uint64_t)pages_needed * PAGE_SIZE > KHEAP_START + KHEAP_MAX_SIZE) {
        return NULL;
    }

    void *vaddr = (void *)heap_cursor;

    void *region = vmm_map_region(kernel_pml4, vaddr, PAGE_WRITABLE, pages_needed);
    if (!region) {
        return NULL;
    }

    heap_cursor += (uint64_t)pages_needed * PAGE_SIZE;
    print("Allocated\nPages: %d\nRegion: %X\nHeap cursor: %X\n", pages_needed, region, heap_cursor);
    return region;
}

void kfree(void *addr) {
    if (!addr) {
        return;
    }

    linked_list_node_t *node = vmm_find_region((uint64_t)addr);
    if (!node) {
        return;
    }

    vmm_free_region(kernel_pml4, node);
    print("Region freed\nNode: %X", node);
}