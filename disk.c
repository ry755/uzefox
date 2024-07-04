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
#include <bootlib.h>
#include <spiram.h>

#include "cpu.h"
#include "disk.h"

sdc_struct_t sd_struct;
disk_controller_t disk_controller;
uint8_t disk_buffer[512];

extern fox32_vm_t vm;

void new_disk(const char *filename, size_t id) {
    uint32_t t32;
    t32 = FS_Find(&sd_struct,
        ((u16)(filename[0])  << 8) |
        ((u16)(filename[1])      ),
        ((u16)(filename[2])  << 8) |
        ((u16)(filename[3])      ),
        ((u16)(filename[4])  << 8) |
        ((u16)(filename[5])      ),
        ((u16)(filename[6])  << 8) |
        ((u16)(filename[7])      ),
        ((u16)(filename[8])  << 8) |
        ((u16)(filename[9])      ),
        ((u16)(filename[10]) << 8) |
        ((u16)(0)               ));
    if (t32 == 0) {
        ClearVram();
        SetBorderColor(0xBF);
        Print(0, 0, PSTR("No disk image?"));
        while (true);
    }
    FS_Select_Cluster(&sd_struct, t32);
    disk_controller.disks[id].file = t32;
    disk_controller.disks[id].size = 512; // TODO: actual size?
}

void remove_disk(size_t id) {
    // TODO; multiple disks?
    disk_controller.disks[id].size = 0;
}

uint64_t get_disk_size(size_t id) {
    return disk_controller.disks[id].size;
}

void set_disk_sector(size_t id, uint64_t sector) {
    uint32_t current_sector = FS_Get_Sector(&sd_struct);
    if (current_sector == sector) return;
    if (current_sector < sector) {
        sector -= current_sector;
        for (uint32_t i = 0; i < sector; i++)
            FS_Next_Sector(&sd_struct);
    } else {
        FS_Reset_Sector(&sd_struct);
        for (uint32_t i = 0; i < sector; i++)
            FS_Next_Sector(&sd_struct);
    }
}

size_t read_disk_into_memory(size_t id) {
    SetBorderColor(0x07);
    FS_Read_Sector(&sd_struct);
    SpiRamSeqWriteStart(disk_controller.buffer_pointer > 0xFFFF ? 1 : 0, disk_controller.buffer_pointer & 0xFFFF);
    SpiRamSeqWriteFrom(disk_buffer, 512);
    SpiRamSeqWriteEnd();
    SetBorderColor(0x00);
    return 512;
}

size_t write_disk_from_memory(size_t id) {
    SetBorderColor(0x30);
    SpiRamSeqReadStart(disk_controller.buffer_pointer > 0xFFFF ? 1 : 0, disk_controller.buffer_pointer & 0xFFFF);
    for (int i = 0; i < 512; i++) disk_buffer[i] = SpiRamSeqReadU8();
    SpiRamSeqReadEnd();
    FS_Write_Sector(&sd_struct);
    SetBorderColor(0x00);
    return 512;
}

DWORD get_fattime(void) {
    return 0;
}
