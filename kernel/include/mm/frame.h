#pragma once

#include <limine.h>

uintptr_t frame_alloc();
void frame_free(uintptr_t ptr);
void frame_init(struct limine_memmap_response *memmap);