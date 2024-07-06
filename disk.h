#pragma once

#include "cpu.h"

typedef struct {
    uint32_t file;
    uint64_t size;
    uint32_t swap_begin;
} disk_t;

typedef struct {
    disk_t disks[4];
    uint32_t buffer_pointer;
} disk_controller_t;

void flush_physical_page_out(fox32_vm_t *vm, uint8_t physical_page);
void load_page_in(fox32_vm_t *vm, uint8_t page);
void new_disk(const char *filename, size_t id);
void remove_disk(size_t id);
uint64_t get_disk_size(size_t id);
void set_disk_sector(size_t id, uint64_t sector);
size_t read_disk_into_memory(size_t id);
size_t write_disk_from_memory(size_t id);
