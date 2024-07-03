#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

#include <uzebox.h>
#include <spiram.h>
#include <fatfs/ff.h>
#include <fatfs/diskio.h>

#include "cpu.h"
#include "disk.h"

FATFS fs;
disk_controller_t disk_controller;
uint8_t disk_buffer[512];

extern fox32_vm_t vm;

void new_disk(const char *filename, size_t id) {
    f_open(&disk_controller.disks[id].file, filename, 0x03);
    disk_controller.disks[id].size = f_size(&disk_controller.disks[id].file);
}

void remove_disk(size_t id) {
    f_close(&disk_controller.disks[id].file);
    disk_controller.disks[id].size = 0;
}

uint64_t get_disk_size(size_t id) {
    return disk_controller.disks[id].size;
}

void set_disk_sector(size_t id, uint64_t sector) {
    f_lseek(&disk_controller.disks[id].file, sector * 512);
}

size_t read_disk_into_memory(size_t id) {
    UINT bytes_read;
    SetBorderColor(0x07);
    f_read(&disk_controller.disks[id].file, disk_buffer, 512, &bytes_read);
    for (int i = 0; i < 512; i++) {
        SpiRamWriteU8(disk_controller.buffer_pointer > 0xFFFF ? 1 : 0, (disk_controller.buffer_pointer & 0xFFFF) + i, disk_buffer[i]);
    }
    SetBorderColor(0x00);
    return bytes_read;
}

size_t write_disk_from_memory(size_t id) {
    // TODO: write this
}
