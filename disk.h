#pragma once

#include <fatfs/ff.h>
#include <fatfs/diskio.h>

typedef struct {
    uint32_t file;
    uint64_t size;
} disk_t;

typedef struct {
    disk_t disks[4];
    uint32_t buffer_pointer;
} disk_controller_t;

void new_disk(const char *filename, size_t id);
void remove_disk(size_t id);
uint64_t get_disk_size(size_t id);
void set_disk_sector(size_t id, uint64_t sector);
size_t read_disk_into_memory(size_t id);
size_t write_disk_from_memory(size_t id);
