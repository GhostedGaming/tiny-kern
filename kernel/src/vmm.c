#include <stdint.h>
#include <stddef.h>
#include <frame.h>
#include <hhdm.h>
#include <page.h>
#include <vmm.h>

static linked_list_t list;

void *vmm_map_region(uint64_t *pml4_phys, void *vaddr, uint64_t flags, int pages_needed) {
    if (!pml4_phys || !vaddr || pages_needed <= 0) {
        return NULL;
    }

    linked_list_node_t *node = (linked_list_node_t *)phys_to_virt(frame_alloc());
    if (!node) {
        return NULL;
    }
    node->base  = (uint64_t)vaddr;
    node->len   = 0;
    node->flags = flags;
    node->next  = NULL;
    node->prev  = NULL;

    for (int i = 0; i < pages_needed; i++) {
        void *page_vaddr = (void *)((uint64_t)vaddr + (uint64_t)i * PAGE_SIZE);
        uintptr_t frame = frame_alloc();
        if (!frame) {
            goto fail;
        }

        uint8_t ok = paging_map_page(pml4_phys, page_vaddr, frame, flags);
        if (!ok) {
            frame_free(frame);
            goto fail;
        }

        node->len++;
    }

    node->end = (uint64_t)vaddr + node->len * PAGE_SIZE;

    node->prev = list.tail;
    if (list.tail) {
        list.tail->next = node;
    } else {
        list.head = node;
    }
    list.tail = node;
    list.count++;

    return (void *)node->base;

fail:
    for (uint64_t j = 0; j < node->len; j++) {
        void *page_vaddr = (void *)((uint64_t)vaddr + j * PAGE_SIZE);
        paging_unmap_page(pml4_phys, page_vaddr);
    }
    frame_free((uintptr_t)virt_to_phys(node));
    return NULL;
}

void vmm_free_region(uint64_t *pml4, linked_list_node_t *node) {
    if (!pml4) {
        return;
    }

    if (!node) {
        return;
    }

    uint64_t base = node->base;
    uint64_t end = node->end;

    uint64_t i = base;

    while (i != end) {
        paging_unmap_page(pml4, (void *)i);
        i+=PAGE_SIZE;
    }

    if (node->prev) {
        node->prev = node->next;
    } else {
        list.head = node->prev;
    }

    if (node->next) {
        node->next = node->prev;
    } else {
        list.tail = node->prev;
    }

    list.count--;

    frame_free(virt_to_phys((void *)node));
}

uint8_t vmm_init() {
    list.count = 0;
    list.head  = NULL;
    list.tail  = NULL;
    return 0;
}