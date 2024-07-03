; fox32os bootloader

    org 0x00000800

const LOAD_ADDRESS: 0x00018000

    ; fox32rom passed the boot disk id in r0, save it
    mov.8 [boot_disk_id], r0

    push r0
    mov r0, loading_str
    call print
    pop r0

    ; open kernel.fxf
    mov r1, r0
    mov r0, kernel_file_name
    mov r2, kernel_file_struct
    call [0xF0045008] ; ryfs_open
    cmp r0, 0
    ifz jmp error

    ; load it into memory
    mov r0, kernel_file_struct
    mov r1, LOAD_ADDRESS
    call [0xF0045014] ; ryfs_read_whole_file

    ; relocate it and off we go!!
    mov r0, relocating_str
    call print
    mov r0, LOAD_ADDRESS
    call fxf_reloc
    mov r1, r0
    mov r0, booting_str
    call print
    movz.8 r0, [boot_disk_id]
    jmp r1

error:
    mov r0, error_str
    call print
    rjmp 0

print:
    out 0, [r0]
    inc r0
    cmp.8 [r0], 0x00
    ifnz jmp print
    ret

loading_str: data.str "smolos bootloader" data.8 10 data.str "loading... " data.8 0
relocating_str: data.str "relocating... " data.8 0
booting_str: data.str "booting" data.8 10 data.8 0
kernel_file_name: data.strz "kernel  fxf"
kernel_file_struct: data.fill 0, 32
error_str: data.strz "failed to open kernel file"
boot_disk_id: data.8 0

    #include "reloc.asm"

    ; bootable magic bytes
    org.pad 0x000009FC
    data.32 0x523C334C
