#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include <lib/errno.h>
#include <hhdm.h>
#include <frame.h>

#define PAGE_SIZE 0x1000

uint64_t *list;

uintptr_t frame_alloc() {
    if (!list) {
        return ENOMEM; 
    }

    uint64_t *phys_frame = list;
    uint64_t *virt_frame = (uint64_t *)phys_to_virt((uintptr_t)phys_frame);

    list = (uint64_t *)(*virt_frame);
    
    return (uintptr_t)phys_frame;
}

void frame_free(uintptr_t ptr) {
    if (!ptr) {
        return;
    }

    uint64_t **virt_frame = (uint64_t **)phys_to_virt(ptr);
    
    *virt_frame = list;
    
    list = (uint64_t *)ptr;
}

void frame_init(struct limine_memmap_response *memmap) {
    list = NULL;
    uint64_t num_entries = memmap->entry_count;
    
    for (uint64_t i = 0; i < num_entries; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        
        for (uint64_t j = 0; j < entry->length; j += PAGE_SIZE) {
            uintptr_t physical_frame_address = entry->base + j;
            frame_free(physical_frame_address);
        }
    }
}
