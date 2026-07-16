#include "storage/ahci.h"
#include <stdint.h>
#include <stddef.h>
#include <mm/frame.h>
#include <mm/memory.h>

#define SIG "tiny_fs"
#define BLOCK_SIZE     4096
#define MAX_NAME_LEN   64

struct fs_meta_data_block {
    char     sig[8];
    uint64_t disk_size;
    uint32_t block_size;
    uint64_t block_count;
    uint64_t bitmap_sector;
    uint64_t bitmap_blocks;
    uint64_t node_sector;
    uint64_t node_blocks;
} __attribute__((packed));

struct node {
    char     name[MAX_NAME_LEN];
    uint8_t  used;
    uint8_t  reserved[7];
    uint64_t inode_sector;
} __attribute__((packed));

struct node_table {
    uint64_t nodes;
    uint64_t capacity;
    struct node node[];
} __attribute__((packed));

#define FILE_PTR_META_SIZE ( \
    MAX_NAME_LEN \
    + 4 + 4 + 4 + 4 \
    + 8 + 8 + 8 + 8 )

#define FILE_PTR_DATA_SECTORS \
    ((BLOCK_SIZE - FILE_PTR_META_SIZE - 8) / 8)

struct file_ptr_block {
    char     file_name[MAX_NAME_LEN];
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t nlink;
    uint64_t file_size;
    int64_t  atime;
    int64_t  mtime;
    int64_t  ctime;
    uint64_t data_sectors[FILE_PTR_DATA_SECTORS];
    uint64_t next_ptr_block;
} __attribute__((packed));

static struct fs_meta_data_block *g_data = NULL;

static uint8_t          **g_bitmap          = NULL;
static uint64_t           g_bitmap_blocks   = 0;
static struct node_table  *g_node_table     = NULL;
static uint32_t            g_sectors_per_block = 0;

#define BITS_PER_BLOCK (BLOCK_SIZE * 8)

static inline int bitmap_test(uint64_t block_idx) {
    uint64_t frame_idx = block_idx / BITS_PER_BLOCK;
    uint64_t bit_idx   = block_idx % BITS_PER_BLOCK;
    return (g_bitmap[frame_idx][bit_idx / 8] >> (bit_idx % 8)) & 1;
}

static inline void bitmap_set(uint64_t block_idx, int value) {
    uint64_t frame_idx = block_idx / BITS_PER_BLOCK;
    uint64_t bit_idx   = block_idx % BITS_PER_BLOCK;
    uint8_t *byte = &g_bitmap[frame_idx][bit_idx / 8];
    if (value) {
        *byte |= (uint8_t)(1u << (bit_idx % 8));
    } else {
        *byte &= (uint8_t)~(1u << (bit_idx % 8));
    }
}

static void bitmap_reserve_range(uint64_t start_block, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        bitmap_set(start_block + i, 1);
    }
}

static uint64_t allocate_block() {
    if (!g_data || !g_bitmap || !g_sectors_per_block) {
        return UINT64_MAX;
    }

    for (uint64_t i = 0; i < g_data->block_count; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i, 1);
            return i * g_sectors_per_block;
        }
    }

    return UINT64_MAX;
}

static void free_block(uint64_t sector) {
    if (!g_data || !g_bitmap || !g_sectors_per_block) {
        return;
    }
    uint64_t block_idx = sector / g_sectors_per_block;
    if (block_idx >= g_data->block_count) {
        return;
    }
    bitmap_set(block_idx, 0);
}

uint8_t tiny_fs_write(uint8_t controller, uint8_t port) {
    struct fs_meta_data_block *data = (struct fs_meta_data_block *)frame_alloc();
    if (!data) {
        return 1;
    }
    memset(data, 0, sizeof(struct fs_meta_data_block));
    memcpy(data->sig, SIG, 8);

    drive_t *drive = ahci_get_drive(controller, port);
    if (!drive) {
        frame_free((uintptr_t)data);
        return 1;
    }

    data->disk_size   = drive->sector_count * drive->sector_size;
    data->block_size  = BLOCK_SIZE;
    data->block_count = data->disk_size / BLOCK_SIZE;

    uint32_t sectors_per_block = BLOCK_SIZE / drive->sector_size;

    uint64_t bitmap_bits  = data->block_count;
    uint64_t bitmap_bytes = (bitmap_bits + 7) / 8;
    data->bitmap_blocks = (bitmap_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

    data->bitmap_sector = sectors_per_block;
    data->node_blocks   = 1;
    data->node_sector   = data->bitmap_sector +
                           data->bitmap_blocks * sectors_per_block;

    uint8_t **bitmap = (uint8_t **)frame_alloc();
    if (!bitmap) {
        frame_free((uintptr_t)data);
        return 1;
    }
    memset(bitmap, 0, BLOCK_SIZE);

    for (uint64_t i = 0; i < data->bitmap_blocks; i++) {
        uintptr_t *frame = (uintptr_t *)frame_alloc();
        if (!frame) {
            for (uint64_t j = 0; j < i; j++) {
                frame_free((uintptr_t)bitmap[j]);
            }
            frame_free((uintptr_t)bitmap);
            frame_free((uintptr_t)data);
            return 1;
        }
        memset(frame, 0, BLOCK_SIZE);
        bitmap[i] = (uint8_t *)frame;
    }

    struct node_table *node_table = (struct node_table *)frame_alloc();
    if (!node_table) {
        for (uint64_t i = 0; i < data->bitmap_blocks; i++) {
            frame_free((uintptr_t)bitmap[i]);
        }
        frame_free((uintptr_t)bitmap);
        frame_free((uintptr_t)data);
        return 1;
    }
    memset(node_table, 0, BLOCK_SIZE);
    node_table->nodes    = 0;
    node_table->capacity =
        (BLOCK_SIZE - offsetof(struct node_table, node)) / sizeof(struct node);

    g_data              = data;
    g_bitmap            = bitmap;
    g_bitmap_blocks     = data->bitmap_blocks;
    g_node_table        = node_table;
    g_sectors_per_block = sectors_per_block;

    bitmap_reserve_range(0, 1);
    bitmap_reserve_range(data->bitmap_sector / sectors_per_block,
                          data->bitmap_blocks);
    bitmap_reserve_range(data->node_sector / sectors_per_block,
                          data->node_blocks);

    if (ahci_write(controller, port, 0, sectors_per_block, data) != 0) {
        goto fail;
    }
    for (uint64_t i = 0; i < g_bitmap_blocks; i++) {
        uint64_t lba = data->bitmap_sector + i * sectors_per_block;
        if (ahci_write(controller, port, lba, sectors_per_block, g_bitmap[i]) != 0) {
            goto fail;
        }
    }
    if (ahci_write(controller, port, data->node_sector, sectors_per_block,
                   g_node_table) != 0) {
        goto fail;
    }

    return 0;

fail:
    for (uint64_t i = 0; i < g_bitmap_blocks; i++) {
        frame_free((uintptr_t)g_bitmap[i]);
    }
    frame_free((uintptr_t)g_bitmap);
    frame_free((uintptr_t)g_node_table);
    frame_free((uintptr_t)g_data);
    g_bitmap            = NULL;
    g_bitmap_blocks     = 0;
    g_node_table        = NULL;
    g_data              = NULL;
    g_sectors_per_block = 0;
    return 1;
}