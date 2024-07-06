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

static uint8_t find_first_clear(uint8_t byte) {
    if (byte == 0xFF) return 0xFF;
    uint8_t first_clear = 0;
    while (byte % 2 == 1) {
        first_clear += 1;
        byte >>= 1;
    }
    return first_clear;
}

void flush_physical_page_out(fox32_vm_t *vm, uint8_t physical_page) {
    if (vm->is_consecutive_read) {
        vm->is_consecutive_read = false;
        SpiRamSeqReadEnd();
    }

    uint32_t old_pos = FS_Get_Pos(&sd_struct);

    SetBorderColor(0xF0);

    // find the page that corresponds to this physical page
    // FIXME: optimize this better?
    uint16_t page = 0xFFFF;
    for (uint16_t i = 0; i < 256; i++) {
        if (vm->page_on_disk_is_at[i] == physical_page) {
            page = i;
            break;
        }
    }
    if (page == 0xFFFF) {
        Print(0, 0, PSTR("page not found?"));
        SetBorderColor(0xBF);
        while (true);
    }
    FS_Set_Pos(&sd_struct, disk_controller.disks[0].swap_begin);
    for (uint16_t i = 0; i < page * 8; i++)
        FS_Next_Sector(&sd_struct);

    // mark it as free
    vm->physical_memory_bitmap[physical_page / 8] &= ~(1 << (physical_page % 8));
    vm->page_is_in_memory_bitmap[page / 8] &= ~(1 << (page % 8));
    uint32_t physical_address = (uint32_t) physical_page * (uint32_t) 4096;

    uint8_t physical_bank = 0;
    for (uint8_t j = 0; j < 8; j++) { // 4096 / 512 = 8
        if (physical_address > 0xFFFF) {
            physical_bank = 1;
            physical_address &= 0xFFFF;
        }
        SpiRamReadInto(physical_bank, physical_address, disk_buffer, 512);
        FS_Write_Sector(&sd_struct);
        FS_Next_Sector(&sd_struct);
        physical_address += 512;
    }

    // NOTE; i think this means attempting to flush page 0 will break things. so dont do that
    vm->page_on_disk_is_at[page] = 0;

    FS_Set_Pos(&sd_struct, old_pos);
    SetBorderColor(0x00);
}

void load_page_in(fox32_vm_t *vm, uint8_t page) {
    if (vm->is_consecutive_read) {
        vm->is_consecutive_read = false;
        SpiRamSeqReadEnd();
    }

    SetBorderColor(0xE0);

    // find the first free physical page
    uint8_t first_clear = 0xFF;
    uint16_t i;
    bool flag = false;
retry:
    for (i = 0; i < 4; i++) {
        first_clear = find_first_clear(vm->physical_memory_bitmap[i]);
        if (first_clear != 0xFF) break;
    }
    // if first_clear == 0xFF, free up some memory
    if (first_clear == 0xFF) {
        // TODO: smarter page flushing
        for (uint8_t j = 0; j < 8; j++) {
            flush_physical_page_out(vm, 20 + j);
        }
        SetBorderColor(0xE0);
        i = 0; first_clear = 0xFF;
        if (flag) {
            Print(0, 0, PSTR("flushed but still no page?"));
            SetBorderColor(0xBF);
            while (true);
        }
        flag = true;
        goto retry;
    }

    // mark it as used
    vm->physical_memory_bitmap[i] |= (1 << first_clear);
    vm->page_is_in_memory_bitmap[page / 8] |= (1 << (page % 8));
    uint8_t physical_page = (i * 8) + first_clear;
    uint32_t physical_address = (uint32_t) physical_page * (uint32_t) 4096;
    // physical_address now equals the physical address to load this page to

    FS_Set_Pos(&sd_struct, disk_controller.disks[0].swap_begin);
    for (i = 0; i < page * 8; i++)
        FS_Next_Sector(&sd_struct);

    uint8_t physical_bank = 0;
    for (uint8_t j = 0; j < 8; j++) { // 4096 / 512 = 8
        FS_Read_Sector(&sd_struct);
        if (physical_address > 0xFFFF) {
            physical_bank = 1;
            physical_address &= 0xFFFF;
        }
        SpiRamWriteFrom(physical_bank, physical_address, disk_buffer, 512);
        physical_address += 512;
        FS_Next_Sector(&sd_struct);
    }

    // save the physical location of this page
    vm->page_on_disk_is_at[page] = physical_page;
    SetBorderColor(0x00);
}

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
    disk_controller.disks[id].size = 0xF00000; // TODO: actual size?

    // find and save the position of the swap data
    // NOTE: this is hardcoded to put swap at 15 MiB into the file!!
    for (uint32_t i = 0; i < 0xF00000 / 512; i++)
        FS_Next_Sector(&sd_struct);
    disk_controller.disks[id].swap_begin = FS_Get_Pos(&sd_struct);
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
    uint8_t page = disk_controller.buffer_pointer / 4096;
    uint32_t offset = disk_controller.buffer_pointer % 4096;
    uint32_t physical_address = ((uint32_t) vm.page_on_disk_is_at[page] * (uint32_t) 4096) + offset;
    uint8_t physical_bank = physical_address > 0xFFFF ? 1 : 0;
    physical_address &= 0xFFFF;
    SpiRamSeqWriteStart(physical_bank, physical_address);
    SpiRamSeqWriteFrom(disk_buffer, 512);
    SpiRamSeqWriteEnd();
    SetBorderColor(0x00);
    return 512;
}

size_t write_disk_from_memory(size_t id) {
    SetBorderColor(0x30);
    uint8_t page = disk_controller.buffer_pointer / 4096;
    uint32_t offset = disk_controller.buffer_pointer % 4096;
    uint32_t physical_address = ((uint32_t) vm.page_on_disk_is_at[page] * (uint32_t) 4096) + offset;
    uint8_t physical_bank = physical_address > 0xFFFF ? 1 : 0;
    physical_address &= 0xFFFF;
    SpiRamSeqReadStart(physical_bank, physical_address);
    for (int i = 0; i < 512; i++) disk_buffer[i] = SpiRamSeqReadU8();
    SpiRamSeqReadEnd();
    FS_Write_Sector(&sd_struct);
    SetBorderColor(0x00);
    return 512;
}
