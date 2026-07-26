#include <stddef.h>
#include <stdint.h>
#include <logging/print.h>
#include <sync/spinlock.h>
#include <mm/page.h>
#include <mm/frame.h>
#include <mm/heap.h>

#define HHDM_OFFSET 0xFFFF800000000000UL 

typedef struct kmalloc_header {
    size_t size;
    int is_free;
    struct kmalloc_header *next;
} kmalloc_header_t;

static kmalloc_header_t *free_list_head = NULL;
static spinlock_t heap_lock = 0;

#define FRAME_TO_HHDM(phys) ((void*)((uintptr_t)(phys) + HHDM_OFFSET))

void *kmalloc(uintptr_t size) {
    if (size == 0) {
        return NULL;
    }

    size = (size + 15) & ~15UL;

    size_t total_needed = size + sizeof(kmalloc_header_t);
    if (total_needed > PAGE_SIZE) {
        print("Requested size %d bytes is too large for single-frame backend\n", size);
        return NULL;
    }

    uint64_t flags = spinlock_acquire_irqsave(&heap_lock);

    kmalloc_header_t *curr = free_list_head;

    while (curr) {
        if (curr->is_free && curr->size >= size) {
            print("Reusing block at %X, size %d\n", curr, curr->size);
            if (curr->size >= size + sizeof(kmalloc_header_t) + 16) {
                kmalloc_header_t *new_block = (kmalloc_header_t*)((uintptr_t)curr + sizeof(kmalloc_header_t) + size);
                new_block->size = curr->size - size - sizeof(kmalloc_header_t);
                new_block->is_free = 1;
                new_block->next = curr->next;

                curr->size = size;
                curr->next = new_block;
                print("Split reused block, new free block at %X, size %d\n", new_block, new_block->size);
            }
            curr->is_free = 0;
            void *payload = (void*)((uintptr_t)curr + sizeof(kmalloc_header_t));
            print("returning %X\n", payload);
            spinlock_release_irqrestore(&heap_lock, flags);
            return payload;
        }
        curr = curr->next;
    }

    uintptr_t phys_frame = frame_alloc(); 
    if (!phys_frame) {
        print("frame_alloc failed, out of physical memory\n");
        spinlock_release_irqrestore(&heap_lock, flags);
        return NULL;
    }

    print("Allocated fresh frame %X\n", phys_frame);

    kmalloc_header_t *new_chunk = (kmalloc_header_t*)FRAME_TO_HHDM(phys_frame);
    new_chunk->size = PAGE_SIZE - sizeof(kmalloc_header_t);
    new_chunk->is_free = 0;
    new_chunk->next = NULL;

    new_chunk->next = free_list_head;
    free_list_head = new_chunk;

    print("Created new chunk at %X, size %d\n", new_chunk, new_chunk->size);

    if (new_chunk->size >= size + sizeof(kmalloc_header_t) + 16) {
        kmalloc_header_t *split_block = (kmalloc_header_t*)((uintptr_t)new_chunk + sizeof(kmalloc_header_t) + size);
        split_block->size = new_chunk->size - size - sizeof(kmalloc_header_t);
        split_block->is_free = 1;
        split_block->next = new_chunk->next;

        new_chunk->size = size;
        new_chunk->next = split_block;
        print("Split fresh chunk, remaining free block at %X, size %d\n", split_block, split_block->size);
    }

    void *payload = (void*)((uintptr_t)new_chunk + sizeof(kmalloc_header_t));
    print("Returning %X\n", payload);
    spinlock_release_irqrestore(&heap_lock, flags);
    return payload;
}

void kfree(void *addr) {
    if (!addr) {
        return;
    }

    uint64_t flags = spinlock_acquire_irqsave(&heap_lock);

    kmalloc_header_t *header = (kmalloc_header_t*)((uintptr_t)addr - sizeof(kmalloc_header_t));
    header->is_free = 1;
    print("Marking block at %X free, size %d\n", header, header->size);

    kmalloc_header_t *curr = free_list_head;
    while (curr) {
        if (curr->is_free && curr->next && curr->next->is_free) {
            uintptr_t expected_next = (uintptr_t)curr + sizeof(kmalloc_header_t) + curr->size;
            
            if (expected_next == (uintptr_t)curr->next) {
                print("Coalescing block %X with %X\n", curr, curr->next);
                curr->size += sizeof(kmalloc_header_t) + curr->next->size;
                curr->next = curr->next->next;
                continue; 
            }
        }
        curr = curr->next;
    }

    spinlock_release_irqrestore(&heap_lock, flags);
}
