; PROCEDURE check_disk(
;    diskId: INT;
; ): INT;
check_disk:
    or r0, 0x80001000
    in r0, r0
    cmp r0, 0
    ifz jmp check_disk_fail
check_disk_continue:
    mov r0, 1
    ret
check_disk_fail:
    mov r0, 0
    ret

; PROCEDURE compare_memory_bytes_wrapper(
;    source, destination: POINTER TO CHAR;
;    size: INT;
; ): INT;
compare_memory_bytes_wrapper:
    call compare_memory_bytes
    ifz mov r0, 1
    ifnz mov r0, 0
    ret

; PROCEDURE brk(
;    value: INT;
; );
brk:
    brk
    ret
