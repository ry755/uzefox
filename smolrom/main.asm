    ; entry point
    ; fox32 starts here on reset
    org 0xF0000000

const FOX32ROM_VERSION_MAJOR: 0
const FOX32ROM_VERSION_MINOR: 8
const FOX32ROM_VERSION_PATCH: 0

const FOX32ROM_API_VERSION: 2

const SYSTEM_STACK: 0x0000FFFF

    opton

    ; initialization code
entry:
    ; disable the MMU
    mcl

    ; set the stack pointer
    mov rsp, SYSTEM_STACK

    ; ensure the event queue gets initialized properly
    mov [EVENT_QUEUE_POINTER], 0

    ; enable interrupts
    ise

    mov r0, smolrom_welcome_str
    call debug_print

event_loop:
    call get_next_event

    ; no event handling here

    ; check if a disk is inserted as disk 0
    ; if port 0x8000100n returns a non-zero value, then a disk is inserted as disk n
    in r0, 0x80001000
    cmp r0, 0
    ifnz call start_boot_process

    jmp event_loop

get_rom_version:
    mov r0, FOX32ROM_VERSION_MAJOR
    mov r1, FOX32ROM_VERSION_MINOR
    mov r2, FOX32ROM_VERSION_PATCH
    ret

get_rom_api_version:
    mov r0, FOX32ROM_API_VERSION
    ret

poweroff:
    mov r0, 0x80010000
    mov r1, 0
    out r0, r1
poweroff_wait:
    jmp poweroff_wait

    ; code
    #include "boot.asm"
    #include "debug.asm"
    #include "disk.asm"
    #include "event.asm"
    #include "exception.asm"
    #include "integer.asm"
    #include "memory.asm"
    #include "random.asm"
    #include "string.asm"

    #include "okameron.asm"

    ; data
smolrom_welcome_str: data.str "smolrom" data.8 10 data.8 0

    ; system jump table
    org.pad 0xF0003000
    data.32 get_rom_version
    data.32 0
    data.32 0
    data.32 new_event
    data.32 wait_for_event
    data.32 get_next_event
    data.32 0
    data.32 0
    data.32 0
    data.32 0
    data.32 0
    data.32 0
    data.32 poweroff
    data.32 get_rom_api_version

    ; disk jump table
    org.pad 0xF0003100
    data.32 read_sector
    data.32 write_sector
    data.32 ryfs_open
    data.32 ryfs_seek
    data.32 ryfs_read
    data.32 ryfs_read_whole_file
    data.32 ryfs_get_size
    data.32 ryfs_get_file_list
    data.32 ryfs_tell
    data.32 ryfs_write
    data.32 0
    data.32 ryfs_create
    data.32 ryfs_delete
    data.32 ryfs_format
    data.32 0

    ; memory copy/compare jump table
    org.pad 0xF0003200
    data.32 copy_memory_bytes
    data.32 copy_memory_words
    data.32 copy_string
    data.32 compare_memory_bytes
    data.32 compare_memory_words
    data.32 compare_string
    data.32 string_length

    ; integer jump table
    org.pad 0xF0003300
    data.32 string_to_int
