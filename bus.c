#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bus.h"
#include "cpu.h"
#include "disk.h"
#include "serial.h"

extern fox32_vm_t vm;
extern disk_controller_t disk_controller;

int bus_io_read(void *user, uint32_t *value, uint32_t port) {
    (void) user;
    switch (port) {
        case 0x00000000: { // serial port
            *value = serial_get();
            break;
        };

        case 0x80001000 ... 0x80002003: { // disk controller port
            size_t id = port & 0xFF;
            uint8_t operation = (port & 0x0000F000) >> 8;
            switch (operation) {
                case 0x10: {
                    // current insert state of specified disk id
                    // size will be zero if disk isn't inserted
                    *value = get_disk_size(id);
                    break;
                };
                case 0x20: {
                    // current buffer pointer
                    *value = disk_controller.buffer_pointer;
                    break;
                };
            }

            break;
        };
    }

    return 0;
}

int bus_io_write(void *user, uint32_t value, uint32_t port) {
    (void) user;
    switch (port) {
        case 0x00000000: { // serial port
            serial_put(value);
            break;
        };

        case 0x80001000 ... 0x80005003: { // disk controller port
            size_t id = port & 0xFF;
            uint8_t operation = (port & 0x0000F000) >> 8;
            switch (operation) {
                case 0x10: {
                    // no-op
                    break;
                };
                case 0x20: {
                    // set the buffer pointer
                    disk_controller.buffer_pointer = value;
                    break;
                };
                case 0x30: {
                    // read specified disk sector into memory
                    set_disk_sector(id, value);
                    read_disk_into_memory(id);
                    break;
                };
                case 0x40: {
                    // write specified disk sector from memory
                    set_disk_sector(id, value);
                    write_disk_from_memory(id);
                    break;
                };
                case 0x50: {
                    // remove specified disk
                    remove_disk(id);
                    break;
                };
            }

            break;
        };
    }

    return 0;
}
