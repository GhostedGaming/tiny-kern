#include "hhdm.h"
#include <stdint.h>
#include <stddef.h>
#include <frame.h>
#include <page.h>

typedef enum {
    red = 0,
    BLACK = 1,
} color_t;

typedef struct linked_list_node {
    uint64_t base;
    uint64_t end;
    
} linked_list_node_t;

typedef struct linked_list {

} linked_list_t;

void *vmm_map_region(uint64_t *pml4, uint64_t flags, int pages_needed) {
    if (!pml4) {
        return NULL;
    }

    if (pages_needed <= 0) {
        return NULL;
    }

}