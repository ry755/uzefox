#include <stdbool.h>
#include <avr/io.h>
#include <stdlib.h>
#include <avr/pgmspace.h>
#include <uzebox.h>
#include <bootlib.h>
#include <spiram.h>

#include "bus.h"
#include "cpu.h"
#include "disk.h"

fox32_vm_t vm;

extern sdc_struct_t sd_struct;
extern uint8_t disk_buffer[512];

int main() {
    fox32_init(&vm);
    vm.io_read = bus_io_read;
    vm.io_write = bus_io_write;
    vm.halted = false;
    vm.debug = false;

    sd_struct.bufp = &(disk_buffer[0]);
    FS_Init(&sd_struct);
    if (!SpiRamInit()) {
        ClearVram();
        SetBorderColor(0xBF);
        Print(0, 0, PSTR("No SPI RAM?"));
        while (true);
    }

    for (uint16_t i = 0; i < 0xFFFF; i++) {
        SpiRamWriteU8(0, i, 0);
        SpiRamWriteU8(1, i, 0);
    }

    ClearVram();

    new_disk("DISK0   IMG", 0);

    while (true) {
        uint32_t executed = 0;
        fox32_err_t error = fox32_resume(&vm, 65535, &executed);
        if (error != FOX32_ERR_OK) {
            PrintHexByte(0, 22, error);
            PrintHexLong(0, 23, vm.pointer_instr);
            PrintHexLong(0, 24, vm.exception_operand);
            while (true);
        }
    }
}
