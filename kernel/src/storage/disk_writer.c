#include "logging/print.h"
#include <stdint.h>
#include <storage/ahci.h>
#include <multitasking/thread.h>
#include <mm/heap.h>
#include <multitasking/sched.h>

struct pending_io {
    struct tcb *tcb;
    uint8_t status;
    uint8_t done;
};

static void disk_complete(uint8_t controller, uint8_t port, uint8_t slot, uint8_t status, void *ctx) {
    print("Disk completed\nController: %d\nPort: %d\n", controller, port);
    struct pending_io *io = ctx;
    io->status = status;
    io->done = 1;
    unblock(io->tcb);
}

uint8_t disk_writer(uint8_t controller, uint8_t port, uint64_t sector, uint8_t count, const void *data) {
    print("Writing to disk\nController: %d\nPort: %d\n", controller, port);
    struct pending_io *io = kmalloc(sizeof(struct pending_io));
    io->tcb = sched_current_thread();
    io->status = 0;
    io->done = 0;

    asm volatile ("cli");

    int slot = ahci_write(controller, port, sector, count, data, disk_complete, io);
    if (slot < 0) {
        asm volatile ("sti");
        print("Failed to write to disk\nController: %d\nPort: %d\n", controller, port);;
        kfree(io);
        return 1;
    }

    if (!io->done) {
        block_current();
    }

    asm volatile ("sti");

    uint8_t status = io->status;
    kfree(io);
    return status;
}

uint8_t disk_reader(uint8_t controller, uint8_t port, uint64_t sector, uint8_t count, void *data) {
    print("Reading from disk\nController: %d\nPort: %d\n", controller, port);
    struct pending_io *io = kmalloc(sizeof(struct pending_io));
    io->tcb = sched_current_thread();
    io->status = 0;
    io->done = 0;

    asm volatile ("cli");

    int slot = ahci_read(controller, port, sector, count, data, disk_complete, io);
    if (slot < 0) {
        asm volatile ("sti");
        print("Failed to read disk\nController: %d\nPort: %d\n", controller, port);
        kfree(io);
        return 1;
    }

    if (!io->done) {
        block_current();
    }

    asm volatile ("sti");

    uint8_t status = io->status;
    kfree(io);
    return status;
}