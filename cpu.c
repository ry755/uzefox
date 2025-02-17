#include <stdio.h>
#include <stdnoreturn.h>
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <spiram.h>

#include "cpu.h"
#include "disk.h"

#include "smolrom.h"

typedef fox32_err_t err_t;

typedef fox32_io_read_t io_read_t;
typedef fox32_io_write_t io_write_t;

static int io_read_default_impl(void *user, uint32_t *value, uint32_t port) {
    return (void) user, (void) value, (int) port;
}
static int io_write_default_impl(void *user, uint32_t value, uint32_t port) {
    if (port == 0) {
        putchar((int) value);
        fflush(stdout);
    }
    return (void) user, (int) port;
}

static io_read_t *const io_read_default = io_read_default_impl;
static io_write_t *const io_write_default = io_write_default_impl;

enum {
    OP_NOP   = 0x00,
    OP_ADD   = 0x01,
    OP_MUL   = 0x02,
    OP_AND   = 0x03,
    OP_SLA   = 0x04,
    OP_SRA   = 0x05,
    OP_BSE   = 0x06,
    OP_CMP   = 0x07,
    OP_JMP   = 0x08,
    OP_RJMP  = 0x09,
    OP_PUSH  = 0x0A,
    OP_IN    = 0x0B,
    OP_ISE   = 0x0C,
    OP_MSE   = 0x0D,
    OP_HALT  = 0x10,
    OP_INC   = 0x11,
    OP_OR    = 0x13,
    OP_IMUL  = 0x14,
    OP_SRL   = 0x15,
    OP_BCL   = 0x16,
    OP_MOV   = 0x17,
    OP_CALL  = 0x18,
    OP_RCALL = 0x19,
    OP_POP   = 0x1A,
    OP_OUT   = 0x1B,
    OP_ICL   = 0x1C,
    OP_MCL   = 0x1D,
    OP_BRK   = 0x20,
    OP_SUB   = 0x21,
    OP_DIV   = 0x22,
    OP_XOR   = 0x23,
    OP_ROL   = 0x24,
    OP_ROR   = 0x25,
    OP_BTS   = 0x26,
    OP_MOVZ  = 0x27,
    OP_LOOP  = 0x28,
    OP_RLOOP = 0x29,
    OP_RET   = 0x2A,
    OP_INT   = 0x2C,
    OP_TLB   = 0x2D,
    OP_DEC   = 0x31,
    OP_REM   = 0x32,
    OP_NOT   = 0x33,
    OP_IDIV  = 0x34,
    OP_IREM  = 0x35,
    OP_ICMP  = 0x37,
    OP_RTA   = 0x39,
    OP_RETI  = 0x3A,
    OP_FLP   = 0x3D,
};

enum {
    SZ_BYTE,
    SZ_HALF,
    SZ_WORD
};

#define OP(_size, _optype) (((uint8_t) (_optype)) | (((uint8_t) (_size)) << 6))

enum {
    CD_ALWAYS,
    CD_IFZ,
    CD_IFNZ,
    CD_IFC,
    CD_IFNC,
    CD_IFGT,
    CD_IFLTEQ
};

enum {
    TY_REG,
    TY_REGPTR,
    TY_IMM,
    TY_IMMPTR,
    TY_NONE
};

enum {
    EX_DIVZERO  = 256 + 0x00,
    EX_ILLEGAL  = 256 + 0x01,
    EX_FAULT_RD = 256 + 0x02,
    EX_FAULT_WR = 256 + 0x03,
    EX_DEBUGGER = 256 + 0x04,
    EX_BUS      = 256 + 0x05
};

#define SIZE8 1
#define SIZE16 2
#define SIZE32 4

typedef struct {
    uint8_t opcode;
    uint8_t condition;
    uint8_t offset;
    uint8_t target;
    uint8_t source;
    uint8_t size;
} asm_instr_t;

static asm_instr_t asm_instr_from(uint16_t half) {
    asm_instr_t instr = {
        (half >>  8),
        (half >>  4) & 7,
        (half >>  7) & 1,
        (half >>  2) & 3,
        (half      ) & 3,
        (half >> 14)
    };
    return instr;
}

typedef fox32_vm_t vm_t;

static void vm_init(vm_t *vm) {
    memset(vm, 0, sizeof(vm_t));
    vm->pointer_instr = FOX32_POINTER_DEFAULT_INSTR;
    vm->pointer_stack = FOX32_POINTER_DEFAULT_STACK;
    vm->halted = true;
    vm->soft_halted = false;
    vm->mmu_enabled = false;
    vm->is_consecutive_read = false;
    vm->io_user = NULL;
    vm->io_read = io_read_default;
    vm->io_write = io_write_default;
}

static noreturn void vm_panic(vm_t *vm, err_t err) {
    longjmp(vm->panic_jmp, (vm->panic_err = err, 1));
}
static noreturn void vm_unreachable(vm_t *vm) {
    vm_panic(vm, FOX32_ERR_INTERNAL);
}

static uint32_t vm_io_read(vm_t *vm, uint32_t port) {
    uint32_t value = 0;
    int status = vm->io_read(vm->io_user, &value, port);
    if (status != 0) {
        vm_panic(vm, FOX32_ERR_IOREAD);
    }
    return value;
}
static void vm_io_write(vm_t *vm, uint32_t port, uint32_t value) {
    int status = vm->io_write(vm->io_user, value, port);
    if (status != 0) {
        vm_panic(vm, FOX32_ERR_IOWRITE);
    }
}

static uint8_t vm_flags_get(vm_t *vm) {
    return (((uint8_t) vm->flag_swap_sp) << 3) |
           (((uint8_t) vm->flag_interrupt) << 2) |
           (((uint8_t) vm->flag_carry) << 1) |
           ((uint8_t) vm->flag_zero);
}
static void vm_flags_set(vm_t *vm, uint8_t flags) {
    vm->flag_zero = (flags & 1) != 0;
    vm->flag_carry = (flags & 2) != 0;
    vm->flag_interrupt = (flags & 4) != 0;
    vm->flag_swap_sp = (flags & 8) != 0;
}

static uint32_t *vm_findlocal(vm_t *vm, uint8_t local) {
    if (local < FOX32_REGISTER_COUNT) {
        return &vm->registers[local];
    }
    if (local == FOX32_REGISTER_COUNT) {
        return &vm->pointer_stack;
    }
    if (local == FOX32_REGISTER_COUNT + 1) {
        return &vm->pointer_exception_stack;
    }
    if (local == FOX32_REGISTER_COUNT + 2) {
        return &vm->pointer_frame;
    }
    vm_panic(vm, FOX32_ERR_BADREGISTER);
}

static uint8_t spi_read8(vm_t *vm, uint32_t address) {
    if ((!vm->is_consecutive_read) && (address == vm->previous_read_address + 1)) {
        vm->is_consecutive_read = true;
        vm->previous_read_address = address;
        u8 bank = 0;
        if (address > 0xFFFF) {
            bank = 1;
            address &= 0xFFFF;
        }
        SpiRamSeqReadStart(bank, (u16) address);
        return SpiRamSeqReadU8();
    }
    if ((vm->is_consecutive_read) && (address == vm->previous_read_address + 1)) {
        vm->previous_read_address = address;
        return SpiRamSeqReadU8();
    }
    vm->previous_read_address = address;
    if (vm->is_consecutive_read) {
        vm->is_consecutive_read = false;
        SpiRamSeqReadEnd();
        u8 bank = 0;
        if (address > 0xFFFF) {
            bank = 1;
            address &= 0xFFFF;
        }
        return SpiRamReadU8(bank, (u16) address);
    } else {
        u8 bank = 0;
        if (address > 0xFFFF) {
            bank = 1;
            address &= 0xFFFF;
        }
        return SpiRamReadU8(bank, (u16) address);
    }
}
static uint8_t vm_read8(vm_t *vm, uint32_t address) {
    uint32_t address_end = address + 1;

    if (address_end > address) {
        if (address_end <= FOX32_MEMORY_RAM) {
            // is this page in memory?
            uint8_t page = address / 4096;
            uint32_t offset = address % 4096;
            if (vm->page_is_in_memory_bitmap[page / 8] & (1 << (page % 8))) {
                // yes! find where it is
                address = ((uint32_t) vm->page_on_disk_is_at[page] * (uint32_t) 4096) + offset;
            } else {
                // nope! load it into memory
                load_page_in(vm, page);
                address = ((uint32_t) vm->page_on_disk_is_at[page] * (uint32_t) 4096) + offset;
            }
            return spi_read8(vm, address);
        }

        // special case for the system jump table
        if (
            (address >= FOX32_MEMORY_ROM_START + 0x40000) &&
            (address <= FOX32_MEMORY_ROM_START + 0x40FFF)
        ) {
            address -= 0x40000 - 0x3000;
        }
        // special case for the disk jump table
        else if (
            (address >= FOX32_MEMORY_ROM_START + 0x45000) &&
            (address <= FOX32_MEMORY_ROM_START + 0x45FFF)
        ) {
            address -= 0x45000 - 0x3100;
        }
        // special case for the memory jump table
        else if (
            (address >= FOX32_MEMORY_ROM_START + 0x46000) &&
            (address <= FOX32_MEMORY_ROM_START + 0x46FFF)
        ) {
            address -= 0x46000 - 0x3200;
        }
        // special case for the integer jump table
        else if (
            (address >= FOX32_MEMORY_ROM_START + 0x47000) &&
            (address <= FOX32_MEMORY_ROM_START + 0x47FFF)
        ) {
            address -= 0x47000 - 0x3300;
        }

        if (
            (address >= FOX32_MEMORY_ROM_START) &&
            (address -= FOX32_MEMORY_ROM_START) + 1 <= FOX32_MEMORY_ROM
        ) {
            return pgm_read_byte(&(fox32_rom[address]));
        }
    }
    vm->exception_operand = address;
    vm_panic(vm, FOX32_ERR_FAULT_RD);
}
static uint16_t vm_read16(vm_t *vm, uint32_t address) {
    uint32_t address_end = address + 2;

    if (address_end > address) {
        uint16_t value = (uint32_t) vm_read8(vm, address) |
                         (uint32_t) vm_read8(vm, address + 1) << 8;
        return value;
    }
    vm->exception_operand = address;
    vm_panic(vm, FOX32_ERR_FAULT_RD);
}
static uint32_t vm_read32(vm_t *vm, uint32_t address) {
    uint32_t address_end = address + 4;

    if (address_end > address) {
        uint32_t value = (uint32_t) vm_read8(vm, address) |
                         ((uint32_t) vm_read8(vm, address + 1) << 8) |
                         ((uint32_t) vm_read8(vm, address + 2) << 16) |
                         ((uint32_t) vm_read8(vm, address + 3) << 24);
        return value;
    }
    vm->exception_operand = address;
    vm_panic(vm, FOX32_ERR_FAULT_RD);
}

static void vm_write8(vm_t *vm, uint32_t address, uint8_t value) {
    if (vm->is_consecutive_read) {
        vm->is_consecutive_read = false;
        SpiRamSeqReadEnd();
    }

    // is this page in memory?
    uint8_t page = address / 4096;
    uint32_t offset = address % 4096;
    if (vm->page_is_in_memory_bitmap[page / 8] & (1 << (page % 8))) {
        // yes! find where it is
        address = ((uint32_t) vm->page_on_disk_is_at[page] * (uint32_t) 4096) + offset;
    } else {
        // nope! load it into memory
        load_page_in(vm, page);
        address = ((uint32_t) vm->page_on_disk_is_at[page] * (uint32_t) 4096) + offset;
    }

    u8 bank = 0;
    if (address > 0xFFFF) {
        bank = 1;
        address &= 0xFFFF;
    }
    SpiRamWriteU8(bank, (u16) address, value);
}
static void vm_write16(vm_t *vm, uint32_t address, uint16_t value) {
    vm_write8(vm, address, value & 0xFF);
    vm_write8(vm, address + 1, value >> 8);
}
static void vm_write32(vm_t *vm, uint32_t address, uint32_t value) {
    vm_write8(vm, address, value & 0xFF);
    vm_write8(vm, address + 1, (value >> 8) & 0xFF);
    vm_write8(vm, address + 2, (value >> 16) & 0xFF);
    vm_write8(vm, address + 3, (value >> 24) & 0xFF);
}

#define VM_PUSH_BODY(_vm_write, _size) \
    _vm_write(vm, vm->pointer_stack - _size, value); \
    vm->pointer_stack -= _size;

static void vm_push8(vm_t *vm, uint8_t value) {
    VM_PUSH_BODY(vm_write8, SIZE8)
}
static void vm_push16(vm_t *vm, uint16_t value) {
    VM_PUSH_BODY(vm_write16, SIZE16)
}
static void vm_push32(vm_t *vm, uint32_t value) {
    VM_PUSH_BODY(vm_write32, SIZE32)
}

#define VM_POP_BODY(_vm_read, _size)                 \
    uint32_t result = _vm_read(vm, vm->pointer_stack); \
    vm->pointer_stack += _size; \
    return result;

static uint8_t vm_pop8(vm_t *vm) {
    VM_POP_BODY(vm_read8, SIZE8)
}
static uint16_t vm_pop16(vm_t *vm) {
    VM_POP_BODY(vm_read16, SIZE16)
}
static uint32_t vm_pop32(vm_t *vm) {
    VM_POP_BODY(vm_read32, SIZE32)
}

#define VM_SOURCE_BODY(_vm_read, _size, _type, _move, _offset)                  \
    uint32_t pointer_base = vm->pointer_instr_mut;                              \
    switch (prtype) {                                                           \
        case TY_REG: {                                                          \
            if (_move) vm->pointer_instr_mut += SIZE8;                          \
            return (_type) *vm_findlocal(vm, vm_read8(vm, pointer_base));       \
        };                                                                      \
        case TY_REGPTR: {                                                       \
            if (_move) vm->pointer_instr_mut += SIZE8+_offset;                  \
            return _vm_read(vm, *vm_findlocal(vm, vm_read8(vm, pointer_base))   \
                            +(_offset ? vm_read8(vm, pointer_base + 1) : 0));   \
        };                                                                      \
        case TY_IMM: {                                                          \
            if (_move) vm->pointer_instr_mut += _size;                          \
            return _vm_read(vm, pointer_base);                                  \
        };                                                                      \
        case TY_IMMPTR: {                                                       \
            if (_move) vm->pointer_instr_mut += SIZE32;                         \
            return _vm_read(vm, vm_read32(vm, pointer_base));                   \
        };                                                                      \
    }                                                                           \
    vm_unreachable(vm);

static uint8_t vm_source8(vm_t *vm, uint8_t prtype, uint8_t offset) {
    VM_SOURCE_BODY(vm_read8, SIZE8, uint8_t, true, offset)
}
static uint8_t vm_source8_stay(vm_t *vm, uint8_t prtype, uint8_t offset) {
    VM_SOURCE_BODY(vm_read8, SIZE8, uint8_t, false, offset)
}
static uint16_t vm_source16(vm_t *vm, uint8_t prtype, uint8_t offset) {
    VM_SOURCE_BODY(vm_read16, SIZE16, uint16_t, true, offset)
}
static uint16_t vm_source16_stay(vm_t *vm, uint8_t prtype, uint8_t offset) {
    VM_SOURCE_BODY(vm_read16, SIZE16, uint16_t, false, offset)
}
static uint32_t vm_source32(vm_t *vm, uint8_t prtype, uint8_t offset) {
    VM_SOURCE_BODY(vm_read32, SIZE32, uint32_t, true, offset)
}
static uint32_t vm_source32_stay(vm_t *vm, uint8_t prtype, uint8_t offset) {
    VM_SOURCE_BODY(vm_read32, SIZE32, uint32_t, false, offset)
}

#define VM_TARGET_BODY(_vm_write, _localvalue, _offset)                          \
    uint32_t pointer_base = vm->pointer_instr_mut;                               \
    switch (prtype) {                                                            \
        case TY_REG: {                                                           \
            vm->pointer_instr_mut += SIZE8;                                      \
            uint8_t local = vm_read8(vm, pointer_base);                          \
            *vm_findlocal(vm, local) = _localvalue;                              \
            return;                                                              \
        };                                                                       \
        case TY_REGPTR: {                                                        \
            vm->pointer_instr_mut += SIZE8+_offset;                              \
            _vm_write(vm, ( _offset ? vm_read8(vm, pointer_base + 1) : 0) +      \
                          *vm_findlocal(vm, vm_read8(vm, pointer_base)), value); \
            return;                                                              \
        };                                                                       \
        case TY_IMM: {                                                           \
            vm_panic(vm, FOX32_ERR_BADIMMEDIATE);                                \
            return;                                                              \
        };                                                                       \
        case TY_IMMPTR: {                                                        \
            vm->pointer_instr_mut += SIZE32;                                     \
            _vm_write(vm, vm_read32(vm, pointer_base), value);                   \
            return;                                                              \
        };                                                                       \
    };                                                                           \
    vm_unreachable(vm);

static void vm_target8(vm_t *vm, uint8_t prtype, uint8_t value, uint8_t offset) {
    VM_TARGET_BODY(vm_write8, (*vm_findlocal(vm, local) & 0xFFFFFF00) | (uint32_t) value, offset)
}
static void vm_target8_zero(vm_t *vm, uint8_t prtype, uint8_t value, uint8_t offset) {
    VM_TARGET_BODY(vm_write32, (uint32_t) value, offset)
}
static void vm_target16(vm_t *vm, uint8_t prtype, uint16_t value, uint8_t offset) {
    VM_TARGET_BODY(vm_write16, (*vm_findlocal(vm, local) & 0xFFFF0000) | (uint32_t) value, offset)
}
static void vm_target16_zero(vm_t *vm, uint8_t prtype, uint16_t value, uint8_t offset) {
    VM_TARGET_BODY(vm_write32, (uint32_t) value, offset)
}
static void vm_target32(vm_t *vm, uint8_t prtype, uint32_t value, uint8_t offset) {
    VM_TARGET_BODY(vm_write32, value, offset)
}

static bool vm_shouldskip(vm_t *vm, uint8_t condition) {
    switch (condition) {
        case CD_ALWAYS: {
            return false;
        };
        case CD_IFZ: {
            return vm->flag_zero == false;
        };
        case CD_IFNZ: {
            return vm->flag_zero == true;
        };
        case CD_IFC: {
            return vm->flag_carry == false;
        };
        case CD_IFNC: {
            return vm->flag_carry == true;
        };
        case CD_IFGT: {
            return (vm->flag_zero == true) || (vm->flag_carry == true);
        };
        case CD_IFLTEQ: {
            return (vm->flag_zero == false) && (vm->flag_carry == false);
        };
    }
    vm_panic(vm, FOX32_ERR_BADCONDITION);
}

static void vm_skipparam(vm_t *vm, uint32_t size, uint8_t prtype, uint8_t offset) {
    if (prtype < TY_IMM) {
        vm->pointer_instr_mut += SIZE8;
        if (offset && prtype==TY_REGPTR) 
            vm->pointer_instr_mut += SIZE8;
    } else if (prtype == TY_IMMPTR) {
        vm->pointer_instr_mut += SIZE32;
    } else {
        vm->pointer_instr_mut += size;
    }
}

#define CHECKED_ADD(_a, _b, _out) __builtin_add_overflow(_a, _b, _out)
#define CHECKED_SUB(_a, _b, _out) __builtin_sub_overflow(_a, _b, _out)
#define CHECKED_MUL(_a, _b, _out) __builtin_mul_overflow(_a, _b, _out)

#define OPER_DIV(_a, _b) ((_a) / (_b))
#define OPER_REM(_a, _b) ((_a) % (_b))
#define OPER_AND(_a, _b) ((_a) & (_b))
#define OPER_XOR(_a, _b) ((_a) ^ (_b))
#define OPER_OR(_a, _b) ((_a) | (_b))
#define OPER_SHIFT_LEFT(_a, _b) ((_a) << (_b))
#define OPER_SHIFT_RIGHT(_a, _b) ((_a) >> (_b))
#define OPER_BIT_SET(_a, _b) ((_a) | (1 << (_b)))
#define OPER_BIT_CLEAR(_a, _b) ((_a) & ~(1 << (_b)))

#define ROTATE_LEFT(_size, _a, _b) (((_a) << (_b)) | ((_a) >> (((_size) * 8) - (_b))))
#define ROTATE_LEFT8(_a, _b) ROTATE_LEFT(SIZE8, _a, _b)
#define ROTATE_LEFT16(_a, _b) ROTATE_LEFT(SIZE16, _a, _b)
#define ROTATE_LEFT32(_a, _b) ROTATE_LEFT(SIZE32, _a, _b)
#define ROTATE_RIGHT(_size, _a, _b) (((_a) >> (_b)) | ((_a) << (((_size) * 8) - (_b))))
#define ROTATE_RIGHT8(_a, _b) ROTATE_RIGHT(SIZE8, _a, _b)
#define ROTATE_RIGHT16(_a, _b) ROTATE_RIGHT(SIZE16, _a, _b)
#define ROTATE_RIGHT32(_a, _b) ROTATE_RIGHT(SIZE32, _a, _b)

#define SOURCEMAP_IDENTITY(x) (x)
#define SOURCEMAP_RELATIVE(x) (instr_base + (x))

#define VM_PRELUDE_0() {                      \
    if (vm_shouldskip(vm, instr.condition)) { \
        break;                                \
    }                                         \
}
#define VM_PRELUDE_1(_size) {                                \
    if (vm_shouldskip(vm, instr.condition)) {                \
        vm_skipparam(vm, _size, instr.source, instr.offset); \
        break;                                               \
    }                                                        \
}
#define VM_PRELUDE_2(_size) {                                \
    if (vm_shouldskip(vm, instr.condition)) {                \
        vm_skipparam(vm, _size, instr.target, instr.offset); \
        vm_skipparam(vm, _size, instr.source, instr.offset); \
        break;                                               \
    }                                                        \
}
#define VM_PRELUDE_BIT(_size) {                              \
    if (vm_shouldskip(vm, instr.condition)) {                \
        vm_skipparam(vm, _size, instr.target, instr.offset); \
        vm_skipparam(vm, SIZE8, instr.source, instr.offset); \
        break;                                               \
    }                                                        \
}

#define VM_IMPL_JMP(_size, _sourcemap) {                                                                              \
    VM_PRELUDE_1(_size);                                                                                              \
    switch (_size) {                                                                                                  \
        case SIZE8: vm->pointer_instr_mut = _sourcemap((int8_t)vm_source8(vm, instr.source, instr.offset)); break;    \
        case SIZE16: vm->pointer_instr_mut = _sourcemap((int16_t)vm_source16(vm, instr.source, instr.offset)); break; \
        default: vm->pointer_instr_mut = _sourcemap(vm_source32(vm, instr.source, instr.offset)); break;              \
    }                                                                                                                 \
    break;                                                                                                            \
}

#define VM_IMPL_LOOP(_size, _sourcemap) {                                                                                 \
    if (                                                                                                                  \
        !vm_shouldskip(vm, instr.condition) &&                                                                            \
        (vm->registers[FOX32_REGISTER_LOOP] -= 1) != 0                                                                    \
    ) {                                                                                                                   \
        switch (_size) {                                                                                                  \
            case SIZE8: vm->pointer_instr_mut = _sourcemap((int8_t)vm_source8(vm, instr.source, instr.offset)); break;    \
            case SIZE16: vm->pointer_instr_mut = _sourcemap((int16_t)vm_source16(vm, instr.source, instr.offset)); break; \
            default: vm->pointer_instr_mut = _sourcemap(vm_source32(vm, instr.source, instr.offset)); break;              \
        }                                                                                                                 \
    } else {                                                                                                              \
        vm_skipparam(vm, _size, instr.source, instr.offset);                                                              \
    }                                                                                                                     \
    break;                                                                                                                \
}

#define VM_IMPL_CALL(_size, _sourcemap) {                                                                             \
    VM_PRELUDE_1(_size);                                                                                              \
    uint32_t pointer_call;                                                                                            \
    switch (_size) {                                                                                                  \
        case SIZE8: pointer_call = (int8_t)vm_source8(vm, instr.source, instr.offset); break;                         \
        case SIZE16: pointer_call = (int16_t)vm_source16(vm, instr.source, instr.offset); break;                      \
        default: pointer_call = vm_source32(vm, instr.source, instr.offset); break;                                   \
    }                                                                                                                 \
    vm_push32(vm, vm->pointer_instr_mut);                                                                             \
    switch (_size) {                                                                                                  \
        case SIZE8: vm->pointer_instr_mut = _sourcemap((int8_t)pointer_call); break;                                  \
        case SIZE16: vm->pointer_instr_mut = _sourcemap((int16_t)pointer_call); break;                                \
        default: vm->pointer_instr_mut = _sourcemap(pointer_call); break;                                             \
    }                                                                                                                 \
    break;                                                                                                            \
}

// make sure NOT to update the stack pointer until the full instruction has
// been read, and the target has been written. otherwise a pagefault halfway
// through could wreak havoc.

#define VM_IMPL_POP(_size, _vm_target, _vm_pop) {     \
    VM_PRELUDE_1(_size);                              \
    uint32_t oldsp = vm->pointer_stack;               \
    uint32_t val = _vm_pop(vm);                       \
    uint32_t newsp = vm->pointer_stack;               \
    vm->pointer_stack = oldsp;                        \
    _vm_target(vm, instr.source, val, instr.offset);  \
    vm->pointer_stack = newsp;                        \
    break;                                            \
}

#define VM_IMPL_PUSH(_size, _vm_source, _vm_push) {           \
    VM_PRELUDE_1(_size);                                      \
    _vm_push(vm, _vm_source(vm, instr.source, instr.offset)); \
    break;                                                    \
}

#define VM_IMPL_MOV(_size, _vm_source, _vm_target) {                                        \
    VM_PRELUDE_2(_size);                                                                    \
    _vm_target(vm, instr.target, _vm_source(vm, instr.source, instr.offset), instr.offset); \
    break;                                                                                  \
}

#define VM_IMPL_NOT(_size, _type, _vm_source_stay, _vm_target) { \
    VM_PRELUDE_1(_size);                                         \
    _type v = _vm_source_stay(vm, instr.source, instr.offset);   \
    _type x = ~v;                                                \
    _vm_target(vm, instr.source, x, instr.offset);               \
    vm->flag_zero = x == 0;                                      \
    break;                                                       \
}

#define VM_IMPL_INC(_size, _type, _vm_source_stay, _vm_target, _oper) { \
    VM_PRELUDE_1(_size);                                                \
    _type v = _vm_source_stay(vm, instr.source, instr.offset);          \
    _type x;                                                            \
    bool carry = _oper(v, 1 << instr.target, &x);                       \
    _vm_target(vm, instr.source, x, instr.offset);                      \
    vm->flag_carry = carry;                                             \
    vm->flag_zero = x == 0;                                             \
    break;                                                              \
}

#define VM_IMPL_ADD(_size, _type, _type_target, _vm_source, _vm_source_stay, _vm_target, _oper) { \
    VM_PRELUDE_2(_size);                                                                          \
    _type a = (_type) _vm_source(vm, instr.source, instr.offset);                                 \
    _type b = (_type) _vm_source_stay(vm, instr.target, instr.offset);                            \
    _type x;                                                                                      \
    bool carry = _oper(b, a, &x);                                                                 \
    _vm_target(vm, instr.target, (_type_target) x, instr.offset);                                 \
    vm->flag_carry = carry;                                                                       \
    vm->flag_zero = x == 0;                                                                       \
    break;                                                                                        \
}

#define VM_IMPL_AND(_size, _type, _type_target, _vm_source, _vm_source_stay, _vm_target, _oper) { \
    VM_PRELUDE_2(_size);                                                                          \
    _type a = (_type) _vm_source(vm, instr.source, instr.offset);                                 \
    _type b = (_type) _vm_source_stay(vm, instr.target, instr.offset);                            \
    _type x = _oper(b, a);                                                                        \
    _vm_target(vm, instr.target, (_type_target) x, instr.offset);                                 \
    vm->flag_zero = x == 0;                                                                       \
    break;                                                                                        \
}

#define VM_IMPL_SHIFT(_size, _type, _type_target, _vm_source, _vm_source_stay, _vm_target, _oper){\
    VM_PRELUDE_BIT(_size);                                                                        \
    _type a = (_type) vm_source8(vm, instr.source, instr.offset);                                 \
    _type b = (_type) _vm_source_stay(vm, instr.target, instr.offset);                            \
    _type x = _oper(b, a);                                                                        \
    _vm_target(vm, instr.target, (_type_target) x, instr.offset);                                 \
    vm->flag_zero = x == 0;                                                                       \
    break;                                                                                        \
}

#define VM_IMPL_DIV(_size, _type, _type_target, _vm_source, _vm_source_stay, _vm_target, _oper) { \
    VM_PRELUDE_2(_size);                                                                          \
    _type a = (_type) _vm_source(vm, instr.source, instr.offset);                                 \
    _type b = (_type) _vm_source_stay(vm, instr.target, instr.offset);                            \
    if (a == 0) {                                                                                 \
        vm_panic(vm, FOX32_ERR_DIVZERO);                                                          \
        break;                                                                                    \
    }                                                                                             \
    _type x = _oper(b, a);                                                                        \
    _vm_target(vm, instr.target, (_type_target) x, instr.offset);                                 \
    vm->flag_zero = x == 0;                                                                       \
    break;                                                                                        \
}

#define VM_IMPL_CMP(_size, _type, _vm_source) {           \
    VM_PRELUDE_2(_size);                                  \
    _type a = _vm_source(vm, instr.source, instr.offset); \
    _type b = _vm_source(vm, instr.target, instr.offset); \
    _type x;                                              \
    vm->flag_carry = CHECKED_SUB(b, a, &x);               \
    vm->flag_zero = x == 0;                               \
    break;                                                \
}

#define VM_IMPL_BTS(_size, _type, _vm_source) {           \
    VM_PRELUDE_BIT(_size);                                \
    _type a = vm_source8(vm, instr.source, instr.offset); \
    _type b = _vm_source(vm, instr.target, instr.offset); \
    _type x = b & (1 << a);                               \
    vm->flag_zero = x == 0;                               \
    break;                                                \
}

static void vm_execute(vm_t *vm) {
    uint32_t instr_base = vm->pointer_instr;
    uint16_t instr_raw = vm_read16(vm, instr_base);

    asm_instr_t instr = asm_instr_from(instr_raw);

    vm->pointer_instr_mut = instr_base + SIZE16;

    switch (instr.opcode) {
        case OP(SZ_BYTE, OP_NOP):
        case OP(SZ_HALF, OP_NOP):
        case OP(SZ_WORD, OP_NOP): {
            break;
        };

        case OP(SZ_BYTE, OP_HALT):
        case OP(SZ_HALF, OP_HALT):
        case OP(SZ_WORD, OP_HALT): {
            VM_PRELUDE_0();
            vm->soft_halted = true;
            break;
        };

        case OP(SZ_BYTE, OP_BRK):
        case OP(SZ_HALF, OP_BRK):
        case OP(SZ_WORD, OP_BRK): {
            VM_PRELUDE_0();
            vm->pointer_instr = vm->pointer_instr_mut;
            vm_panic(vm, FOX32_ERR_DEBUGGER);
            break;
        };

        case OP(SZ_WORD, OP_IN): {
            VM_PRELUDE_2(SIZE32);
            vm_target32(vm, instr.target, vm_io_read(vm, vm_source32(vm, instr.source, 0)), instr.offset);
            break;
        };
        case OP(SZ_WORD, OP_OUT): {
            VM_PRELUDE_2(SIZE32);
            uint32_t value = vm_source32(vm, instr.source, instr.offset);
            uint32_t port = vm_source32(vm, instr.target, instr.offset);
            vm_io_write(vm, port, value);
            break;
        };

        case OP(SZ_BYTE, OP_RTA): {
            VM_PRELUDE_2(SIZE8);
            vm_target32(vm, instr.target, instr_base + (int8_t)vm_source8(vm, instr.source, instr.offset), instr.offset);
            break;
        };
        case OP(SZ_HALF, OP_RTA): {
            VM_PRELUDE_2(SIZE16);
            vm_target32(vm, instr.target, instr_base + (int16_t)vm_source16(vm, instr.source, instr.offset), instr.offset);
            break;
        };
        case OP(SZ_WORD, OP_RTA): {
            VM_PRELUDE_2(SIZE32);
            vm_target32(vm, instr.target, instr_base + vm_source32(vm, instr.source, instr.offset), instr.offset);
            break;
        };

        case OP(SZ_WORD, OP_RET): {
            VM_PRELUDE_0();
            vm->pointer_instr_mut = vm_pop32(vm);
            break;
        };
        case OP(SZ_WORD, OP_RETI): {
            VM_PRELUDE_0();
            vm_flags_set(vm, vm_pop8(vm));
            vm->pointer_instr_mut = vm_pop32(vm);
            if (vm->flag_swap_sp) {
                vm->pointer_stack = vm_pop32(vm);
            }
            break;
        };

        case OP(SZ_WORD, OP_ISE): {
            VM_PRELUDE_0();
            vm->flag_interrupt = true;
            break;
        };
        case OP(SZ_WORD, OP_ICL): {
            VM_PRELUDE_0();
            vm->flag_interrupt = false;
            break;
        };

        case OP(SZ_WORD, OP_JMP): VM_IMPL_JMP(SIZE32, SOURCEMAP_IDENTITY);
        case OP(SZ_WORD, OP_CALL): VM_IMPL_CALL(SIZE32, SOURCEMAP_IDENTITY);
        case OP(SZ_WORD, OP_LOOP): VM_IMPL_LOOP(SIZE32, SOURCEMAP_IDENTITY);

        case OP(SZ_BYTE, OP_RJMP): VM_IMPL_JMP(SIZE8, SOURCEMAP_RELATIVE);
        case OP(SZ_BYTE, OP_RCALL): VM_IMPL_CALL(SIZE8, SOURCEMAP_RELATIVE);
        case OP(SZ_BYTE, OP_RLOOP): VM_IMPL_LOOP(SIZE8, SOURCEMAP_RELATIVE);
        case OP(SZ_HALF, OP_RJMP): VM_IMPL_JMP(SIZE16, SOURCEMAP_RELATIVE);
        case OP(SZ_HALF, OP_RCALL): VM_IMPL_CALL(SIZE16, SOURCEMAP_RELATIVE);
        case OP(SZ_HALF, OP_RLOOP): VM_IMPL_LOOP(SIZE16, SOURCEMAP_RELATIVE);
        case OP(SZ_WORD, OP_RJMP): VM_IMPL_JMP(SIZE32, SOURCEMAP_RELATIVE);
        case OP(SZ_WORD, OP_RCALL): VM_IMPL_CALL(SIZE32, SOURCEMAP_RELATIVE);
        case OP(SZ_WORD, OP_RLOOP): VM_IMPL_LOOP(SIZE32, SOURCEMAP_RELATIVE);

        case OP(SZ_BYTE, OP_POP): VM_IMPL_POP(SIZE8, vm_target8, vm_pop8);
        case OP(SZ_HALF, OP_POP): VM_IMPL_POP(SIZE16, vm_target16, vm_pop16);
        case OP(SZ_WORD, OP_POP): VM_IMPL_POP(SIZE32, vm_target32, vm_pop32);

        case OP(SZ_BYTE, OP_PUSH): VM_IMPL_PUSH(SIZE8, vm_source8, vm_push8);
        case OP(SZ_HALF, OP_PUSH): VM_IMPL_PUSH(SIZE16, vm_source16, vm_push16);
        case OP(SZ_WORD, OP_PUSH): VM_IMPL_PUSH(SIZE32, vm_source32, vm_push32);

        case OP(SZ_BYTE, OP_MOV): VM_IMPL_MOV(SIZE8, vm_source8, vm_target8);
        case OP(SZ_BYTE, OP_MOVZ): VM_IMPL_MOV(SIZE8, vm_source8, vm_target8_zero);
        case OP(SZ_HALF, OP_MOV): VM_IMPL_MOV(SIZE16, vm_source16, vm_target16);
        case OP(SZ_HALF, OP_MOVZ): VM_IMPL_MOV(SIZE16, vm_source16, vm_target16_zero);
        case OP(SZ_WORD, OP_MOV):
        case OP(SZ_WORD, OP_MOVZ): VM_IMPL_MOV(SIZE32, vm_source32, vm_target32);

        case OP(SZ_BYTE, OP_NOT): VM_IMPL_NOT(SIZE8, uint8_t, vm_source8_stay, vm_target8);
        case OP(SZ_HALF, OP_NOT): VM_IMPL_NOT(SIZE16, uint16_t, vm_source16_stay, vm_target16);
        case OP(SZ_WORD, OP_NOT): VM_IMPL_NOT(SIZE32, uint32_t, vm_source32_stay, vm_target32);

        case OP(SZ_BYTE, OP_INC): VM_IMPL_INC(SIZE8, uint8_t, vm_source8_stay, vm_target8, CHECKED_ADD);
        case OP(SZ_HALF, OP_INC): VM_IMPL_INC(SIZE16, uint16_t, vm_source16_stay, vm_target16, CHECKED_ADD);
        case OP(SZ_WORD, OP_INC): VM_IMPL_INC(SIZE32, uint32_t, vm_source32_stay, vm_target32, CHECKED_ADD);
        case OP(SZ_BYTE, OP_DEC): VM_IMPL_INC(SIZE8, uint8_t, vm_source8_stay, vm_target8, CHECKED_SUB);
        case OP(SZ_HALF, OP_DEC): VM_IMPL_INC(SIZE16, uint16_t, vm_source16_stay, vm_target16, CHECKED_SUB);
        case OP(SZ_WORD, OP_DEC): VM_IMPL_INC(SIZE32, uint32_t, vm_source32_stay, vm_target32, CHECKED_SUB);

        case OP(SZ_BYTE, OP_ADD): VM_IMPL_ADD(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, CHECKED_ADD);
        case OP(SZ_HALF, OP_ADD): VM_IMPL_ADD(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, CHECKED_ADD);
        case OP(SZ_WORD, OP_ADD): VM_IMPL_ADD(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, CHECKED_ADD);
        case OP(SZ_BYTE, OP_SUB): VM_IMPL_ADD(SIZE8, uint8_t, uint8_t ,vm_source8, vm_source8_stay, vm_target8, CHECKED_SUB);
        case OP(SZ_HALF, OP_SUB): VM_IMPL_ADD(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, CHECKED_SUB);
        case OP(SZ_WORD, OP_SUB): VM_IMPL_ADD(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, CHECKED_SUB);
        case OP(SZ_BYTE, OP_MUL): VM_IMPL_ADD(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, CHECKED_MUL);
        case OP(SZ_HALF, OP_MUL): VM_IMPL_ADD(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, CHECKED_MUL);
        case OP(SZ_WORD, OP_MUL): VM_IMPL_ADD(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, CHECKED_MUL);
        case OP(SZ_BYTE, OP_IMUL): VM_IMPL_ADD(SIZE8, int8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, CHECKED_MUL);
        case OP(SZ_HALF, OP_IMUL): VM_IMPL_ADD(SIZE16, int16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, CHECKED_MUL);
        case OP(SZ_WORD, OP_IMUL): VM_IMPL_ADD(SIZE32, int32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, CHECKED_MUL);

        case OP(SZ_BYTE, OP_DIV): VM_IMPL_DIV(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, OPER_DIV);
        case OP(SZ_HALF, OP_DIV): VM_IMPL_DIV(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, OPER_DIV);
        case OP(SZ_WORD, OP_DIV): VM_IMPL_DIV(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, OPER_DIV);
        case OP(SZ_BYTE, OP_REM): VM_IMPL_DIV(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, OPER_REM);
        case OP(SZ_HALF, OP_REM): VM_IMPL_DIV(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, OPER_REM);
        case OP(SZ_WORD, OP_REM): VM_IMPL_DIV(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, OPER_REM);
        case OP(SZ_BYTE, OP_IDIV): VM_IMPL_DIV(SIZE8, int8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, OPER_DIV);
        case OP(SZ_HALF, OP_IDIV): VM_IMPL_DIV(SIZE16, int16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, OPER_DIV);
        case OP(SZ_WORD, OP_IDIV): VM_IMPL_DIV(SIZE32, int32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, OPER_DIV);
        case OP(SZ_BYTE, OP_IREM): VM_IMPL_DIV(SIZE8, int8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, OPER_REM);
        case OP(SZ_HALF, OP_IREM): VM_IMPL_DIV(SIZE16, int16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, OPER_REM);
        case OP(SZ_WORD, OP_IREM): VM_IMPL_DIV(SIZE32, int32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, OPER_REM);

        case OP(SZ_BYTE, OP_AND): VM_IMPL_AND(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, OPER_AND);
        case OP(SZ_HALF, OP_AND): VM_IMPL_AND(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, OPER_AND);
        case OP(SZ_WORD, OP_AND): VM_IMPL_AND(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, OPER_AND);
        case OP(SZ_BYTE, OP_XOR): VM_IMPL_AND(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, OPER_XOR);
        case OP(SZ_HALF, OP_XOR): VM_IMPL_AND(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, OPER_XOR);
        case OP(SZ_WORD, OP_XOR): VM_IMPL_AND(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, OPER_XOR);
        case OP(SZ_BYTE, OP_OR): VM_IMPL_AND(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, OPER_OR);
        case OP(SZ_HALF, OP_OR): VM_IMPL_AND(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, OPER_OR);
        case OP(SZ_WORD, OP_OR): VM_IMPL_AND(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, OPER_OR);

        case OP(SZ_BYTE, OP_SLA): VM_IMPL_SHIFT(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, OPER_SHIFT_LEFT);
        case OP(SZ_HALF, OP_SLA): VM_IMPL_SHIFT(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, OPER_SHIFT_LEFT);
        case OP(SZ_WORD, OP_SLA): VM_IMPL_SHIFT(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, OPER_SHIFT_LEFT);
        case OP(SZ_BYTE, OP_SRL): VM_IMPL_SHIFT(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, OPER_SHIFT_RIGHT);
        case OP(SZ_HALF, OP_SRL): VM_IMPL_SHIFT(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, OPER_SHIFT_RIGHT);
        case OP(SZ_WORD, OP_SRL): VM_IMPL_SHIFT(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, OPER_SHIFT_RIGHT);
        case OP(SZ_BYTE, OP_SRA): VM_IMPL_SHIFT(SIZE8, int8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, OPER_SHIFT_RIGHT);
        case OP(SZ_HALF, OP_SRA): VM_IMPL_SHIFT(SIZE16, int16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, OPER_SHIFT_RIGHT);
        case OP(SZ_WORD, OP_SRA): VM_IMPL_SHIFT(SIZE32, int32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, OPER_SHIFT_RIGHT);

        case OP(SZ_BYTE, OP_ROL): VM_IMPL_SHIFT(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, ROTATE_LEFT8);
        case OP(SZ_HALF, OP_ROL): VM_IMPL_SHIFT(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, ROTATE_LEFT16);
        case OP(SZ_WORD, OP_ROL): VM_IMPL_SHIFT(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, ROTATE_LEFT32);
        case OP(SZ_BYTE, OP_ROR): VM_IMPL_SHIFT(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, ROTATE_RIGHT8);
        case OP(SZ_HALF, OP_ROR): VM_IMPL_SHIFT(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, ROTATE_RIGHT16);
        case OP(SZ_WORD, OP_ROR): VM_IMPL_SHIFT(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, ROTATE_RIGHT32);

        case OP(SZ_BYTE, OP_BSE): VM_IMPL_SHIFT(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, OPER_BIT_SET);
        case OP(SZ_HALF, OP_BSE): VM_IMPL_SHIFT(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, OPER_BIT_SET);
        case OP(SZ_WORD, OP_BSE): VM_IMPL_SHIFT(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, OPER_BIT_SET);
        case OP(SZ_BYTE, OP_BCL): VM_IMPL_SHIFT(SIZE8, uint8_t, uint8_t, vm_source8, vm_source8_stay, vm_target8, OPER_BIT_CLEAR);
        case OP(SZ_HALF, OP_BCL): VM_IMPL_SHIFT(SIZE16, uint16_t, uint16_t, vm_source16, vm_source16_stay, vm_target16, OPER_BIT_CLEAR);
        case OP(SZ_WORD, OP_BCL): VM_IMPL_SHIFT(SIZE32, uint32_t, uint32_t, vm_source32, vm_source32_stay, vm_target32, OPER_BIT_CLEAR);

        case OP(SZ_BYTE, OP_CMP): VM_IMPL_CMP(SIZE8, uint8_t, vm_source8);
        case OP(SZ_HALF, OP_CMP): VM_IMPL_CMP(SIZE16, uint16_t, vm_source16);
        case OP(SZ_WORD, OP_CMP): VM_IMPL_CMP(SIZE32, uint32_t, vm_source32);
        case OP(SZ_BYTE, OP_ICMP): VM_IMPL_CMP(SIZE8, int8_t, vm_source8);
        case OP(SZ_HALF, OP_ICMP): VM_IMPL_CMP(SIZE16, int16_t, vm_source16);
        case OP(SZ_WORD, OP_ICMP): VM_IMPL_CMP(SIZE32, int32_t, vm_source32);

        case OP(SZ_BYTE, OP_BTS): VM_IMPL_BTS(SIZE8, uint8_t, vm_source8);
        case OP(SZ_HALF, OP_BTS): VM_IMPL_BTS(SIZE16, uint16_t, vm_source16);
        case OP(SZ_WORD, OP_BTS): VM_IMPL_BTS(SIZE32, uint32_t, vm_source32);

        case OP(SZ_WORD, OP_MSE): {
            VM_PRELUDE_0();
            vm->mmu_enabled = true;
            break;
        };
        case OP(SZ_WORD, OP_MCL): {
            VM_PRELUDE_0();
            vm->mmu_enabled = false;
            break;
        };
        case OP(SZ_WORD, OP_INT): {
            VM_PRELUDE_1(SIZE32);
            uint32_t intr = vm_source32(vm, instr.source, instr.offset);
            vm->pointer_instr = vm->pointer_instr_mut;
            fox32_raise(vm, intr);
            vm->pointer_instr_mut = vm->pointer_instr;
            break;
        };

        default:
            vm->exception_operand = instr_raw;
            vm_panic(vm, FOX32_ERR_BADOPCODE);
    }

    vm->pointer_instr = vm->pointer_instr_mut;
}

static err_t vm_step(vm_t *vm) {
    if (setjmp(vm->panic_jmp) != 0) {
        return vm->halted = true, vm->panic_err;
    }
    vm_execute(vm);
    return FOX32_ERR_OK;
}
static err_t vm_resume(vm_t *vm, uint32_t count, uint32_t *executed) {
    if (setjmp(vm->panic_jmp) != 0) {
        return vm->halted = true, vm->panic_err;
    }

    vm->halted = false;

    uint32_t remaining = count;
    while (!vm->halted && !vm->soft_halted && remaining > 0) {
        vm_execute(vm);
        remaining -= 1;
        *executed += 1;
    }

    if (vm->soft_halted) {
        *executed = count;
    }

    return FOX32_ERR_OK;
}

static fox32_err_t vm_raise(vm_t *vm, uint16_t vector) {
    if (!vm->flag_interrupt && vector < 256) {
        return FOX32_ERR_NOINTERRUPTS;
    }
    if (setjmp(vm->panic_jmp) != 0) {
        return vm->panic_err;
    }

    uint32_t pointer_handler = vm_read32(vm, SIZE32 * (uint32_t) vector);

    if (vm->flag_swap_sp) {
        uint32_t old_stack_pointer = vm->pointer_stack;
        vm->pointer_stack = vm->pointer_exception_stack;
        vm_push32(vm, old_stack_pointer);
        vm_push32(vm, vm->pointer_instr);
        vm_push8(vm, vm_flags_get(vm));
        vm->flag_swap_sp = false;
    } else {
        vm_push32(vm, vm->pointer_instr);
        vm_push8(vm, vm_flags_get(vm));
    }

    if (vector >= 256) {
        // if this is an exception, push the operand
        vm_push32(vm, vm->exception_operand);
        vm->exception_operand = 0;
    } else {
        // if this is an interrupt, push the vector
        vm_push32(vm, (uint32_t) vector);
    }

    vm->pointer_instr = pointer_handler;
    vm->halted = true;
    vm->soft_halted = false;
    vm->flag_interrupt = false;

    return FOX32_ERR_OK;
}

static fox32_err_t vm_recover(vm_t *vm, err_t err) {
    switch (err) {
        case FOX32_ERR_DEBUGGER:
            return vm_raise(vm, EX_DEBUGGER);
        case FOX32_ERR_FAULT_RD:
            return vm_raise(vm, EX_FAULT_RD);
        case FOX32_ERR_FAULT_WR:
            return vm_raise(vm, EX_FAULT_WR);
        case FOX32_ERR_BADOPCODE:
        case FOX32_ERR_BADCONDITION:
        case FOX32_ERR_BADREGISTER:
        case FOX32_ERR_BADIMMEDIATE:
            return vm_raise(vm, EX_ILLEGAL);
        case FOX32_ERR_DIVZERO:
            return vm_raise(vm, EX_DIVZERO);
        case FOX32_ERR_IOREAD:
        case FOX32_ERR_IOWRITE:
            return vm_raise(vm, EX_BUS);
        default:
            return FOX32_ERR_CANTRECOVER;
    }
}

#define VM_SAFEPUSH_BODY(_vm_push)    \
    if (setjmp(vm->panic_jmp) != 0) { \
        return vm->panic_err;         \
    }                                 \
    _vm_push(vm, value);              \
    return FOX32_ERR_OK;

static fox32_err_t vm_safepush_byte(vm_t *vm, uint8_t value) {
    VM_SAFEPUSH_BODY(vm_push8)
}
static fox32_err_t vm_safepush_half(vm_t *vm, uint16_t value) {
    VM_SAFEPUSH_BODY(vm_push16)
}
static fox32_err_t vm_safepush_word(vm_t *vm, uint32_t value) {
    VM_SAFEPUSH_BODY(vm_push32)
}

#define VM_SAFEPOP_BODY(_vm_pop)      \
    *value = 0;                       \
    if (setjmp(vm->panic_jmp) != 0) { \
        return vm->panic_err;         \
    }                                 \
    *value = _vm_pop(vm);             \
    return FOX32_ERR_OK;

static fox32_err_t vm_safepop_byte(vm_t *vm, uint8_t *value) {
    VM_SAFEPOP_BODY(vm_pop8)
}
static fox32_err_t vm_safepop_half(vm_t *vm, uint16_t *value) {
    VM_SAFEPOP_BODY(vm_pop16)
}
static fox32_err_t vm_safepop_word(vm_t *vm, uint32_t *value) {
    VM_SAFEPOP_BODY(vm_pop32)
}

void fox32_init(fox32_vm_t *vm) {
    vm_init(vm);
}
fox32_err_t fox32_step(fox32_vm_t *vm) {
    return vm_step(vm);
}
fox32_err_t fox32_resume(fox32_vm_t *vm, uint32_t count, uint32_t *executed) {
    return vm_resume(vm, count, executed);
}
fox32_err_t fox32_raise(fox32_vm_t *vm, uint16_t vector) {
    return vm_raise(vm, vector);
}
fox32_err_t fox32_recover(fox32_vm_t *vm, fox32_err_t err) {
    return vm_recover(vm, err);
}
fox32_err_t fox32_push_byte(fox32_vm_t *vm, uint8_t value) {
    return vm_safepush_byte(vm, value);
}
fox32_err_t fox32_push_half(fox32_vm_t *vm, uint16_t value) {
    return vm_safepush_half(vm, value);
}
fox32_err_t fox32_push_word(fox32_vm_t *vm, uint32_t value) {
    return vm_safepush_word(vm, value);
}
fox32_err_t fox32_pop_byte(fox32_vm_t *vm, uint8_t *value) {
    return vm_safepop_byte(vm, value);
}
fox32_err_t fox32_pop_half(fox32_vm_t *vm, uint16_t *value) {
    return vm_safepop_half(vm, value);
}
fox32_err_t fox32_pop_word(fox32_vm_t *vm, uint32_t *value) {
    return vm_safepop_word(vm, value);
}
