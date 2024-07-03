#include <stdbool.h>
#include <avr/io.h>
#include <stdlib.h>
#include <avr/pgmspace.h>
#include <uzebox.h>
#include <spiram.h>

#include "bus.h"
#include "cpu.h"
#include "disk.h"

fox32_vm_t vm;

extern FATFS fs;

int main() {
    fox32_init(&vm);
    vm.io_read = bus_io_read;
    vm.io_write = bus_io_write;
    vm.halted = false;
    vm.debug = false;

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

    f_mount(0, &fs);
    new_disk("disk0.img", 0);

    while (true) {
        uint32_t executed = 0;
        fox32_err_t error = fox32_resume(&vm, 256, &executed);
        if (error != FOX32_ERR_OK) {
            PrintHexByte(0, 22, error);
            PrintHexLong(0, 23, vm.pointer_instr);
            PrintHexLong(0, 24, vm.exception_operand);
            while (true);
        }
    }
}
