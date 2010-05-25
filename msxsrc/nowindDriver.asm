MYSIZE          equ 8                   ; size of work area (not used, some programs write here to stop drives spinning)
SECLEN          equ 512                 ; sector size

; SLTWRK entry
; +0    rom drive number
; +1..5 previous EXTBIO
; +6    not used
; +7    not used


; if this is defined we tell DOS2.3 that we _are DOS2.3 also, so
; it does not try to override our initilazations

define  PRETEND_2B_DOS23

INIHRD:
        DEBUGMESSAGE "INIHRD"
        
;        call getWorkArea
;        DEBUGDUMPREGISTERS

        ld h,HIGH usbWritePage1
        ld (hl),$af                     ; signal MSX reset to host
        ld (hl),$ff
        jp nowindInit

DRIVES:
        DEBUGMESSAGE "DRIVES"
        push af                         ; A, BC and DE should be preserved!
        push bc
        push de
        ld a,(DEVICE)

        push hl
        call sendRegisters
        ld (hl),C_DRIVES
        ld hl,drivesCommand
        call executeCommandNowindInPage0

        pop hl
        ld l,2
        jr c,.error
        ld l,a
.error:
        pop de
        pop bc
        pop af
        ret

drivesCommand:
        call getHeaderInPage0
        ret c
        ld (LASTDRV),a                  ; install phantom drives (CRTL key)?
        ld a,(hl)                       ; allow other drives (SHIFT key)?
        ld (DEVICE),a
        ld a,(hl)                       ; number of drives
        ret

INIENV:
; Interrupt handler can be installed here and
; work area can be initialized when it was requested
        DEBUGMESSAGE "INIENV"

        ifdef PRETEND_2B_DOS23
        DEBUGMESSAGE "Lie about being DOS v2.31"
        ld a,$23
        ld ($f313),a
        endif

        call installExtendedBios

        call sendRegisters
        ld (hl),C_DRIVES
        ld hl,inienvCommand
        call executeCommandNowindInPage0

        push af
        call getEntrySLTWRK
        pop af
        ld (hl),0                       ; default drive number for romdisk
        ret c
        DEBUGDUMPREGISTERS
        ld (hl),a                       ; drive number for romdisk
        ret

inienvCommand:
        call getHeaderInPage0
        ret


checkWorkArea:
;        ld a,1
        cp 1
        ret

        push bc
        push hl
        push af
;        call GETWRK
        call getEntrySLTWRK
        pop af
        cp (hl)
        pop hl
        pop bc
        ret

DSKIO:
; Input     F   Carry for set for write, reset for read
;           A   Drive number
;           B   Number of sectors to read/write
;           C   Media descriptor
;           DE  Logical sector number
;           HL  Transfer address
; Output    F   Carry set when not successful
;           A   Error code
;           B   Number of remaining sectors (TODO: check! even without error?)

        DEBUGMESSAGE "DSKIO"
        push af
        call checkWorkArea
        jp z,ROMDISK_DSKIO
        pop af

        call sendRegisters
        ld (hl),C_DSKIO
        jr c,.write
.read:
        call blockRead
        DEBUGMESSAGE "exit_dskio_rd"
        ret c                           ; return error (error code in a)
        xor a                           ; no error, clear a
        ret
        
.write:
        call blockWrite
        DEBUGMESSAGE "exit_dskio_wrt"
        ret c
        xor a
        ret
        
DSKCHG:
; Input     A   Drive number
;           B   0
;           C   Media descriptor (previous)
;           HL  Base address of DPB
; Output    B   1   Disk unchanged
;               0   Unknown (DPB is updated)
;               -1  Disk changed (DPB is updated)
;           F   Carry set when not succesfull
;           A   Error code

        DEBUGMESSAGE "DSKCHG"
        push af
        call checkWorkArea
        jp z,ROMDISK_DSKCHG
        pop af

        push af
        push hl
        call sendRegisters
        ld (hl),C_DSKCHG
        ld hl,dskchgCommand
        call executeCommandNowindInPage0
        pop hl
        pop de
        ret c                   ; not ready (reg_a = 2)

        or a
        ld b,1
        ret z                   ; not changed
        ld a,d
        ld b,c
        call GETDPB
        ld a,10
        ret c
        ld b,255
        ret

dskchgCommand:
        call getHeaderInPage0
        ret c                
        ld c,(hl)
        ret

GETDPB:
; Input     A   Drive number
;           B   Media descriptor (first byte of FAT)
;           C   Previous media descriptor (does not seem to be used in other drivers)
;           HL  Base address of HL
; Output    DPB for specified drive in [HL+1]..[HL+18]

        DEBUGMESSAGE "GETDPB"
        DEBUGDUMPREGISTERS
        ex de,hl
        inc de
        ld h,a
        ld a,b
        cp $f0
        ld a,h
        DEBUGDUMPREGISTERS
        jr z,.hddImage

;        MESSAGE "ROM GETDPB"

        ld a,b
        sub $f8
        ret c                           ; not supported in msxdos1
        rlca                            ; 2x
        ld c,a
        rlca                            ; 4x
        rlca                            ; 8x
        rlca                            ; 16x
        add a,c                         ; 18x
        ld c,a
        ld b,0
        ld hl,supportedMedia
        add hl,bc
        ld c,18
        ldir
        ret

.hddImage:
        DEBUGMESSAGE ".hddImage"
        ;MESSAGE "HOST GETDPB"

        call sendRegisters
        ld (hl),C_GETDPB
        call enableNowindPage0
        call getHeaderInPage0

        jr c,.exit                      ; not ready
        ld e,a                          ; destination
        ld d,(hl)
        ld bc,18
        DEBUGDUMPREGISTERS
        ldir
        ;DB $ed, $0a
.exit:  jp restorePage0

CHOICE:
        ;DEBUGMESSAGE "CHOICE"
        if MSXDOSVER = 2
        ld hl,.none
        ret
.none:  db 0
        else
        ld hl,0                         ; no choice
        ret
        endif

DSKFMT:
        scf
        ld a,16                         ; other error
        ret

OEMSTA:
        push hl
        ld hl,.statement
        call findStatementName
        ld e,(hl)
        inc hl
        ld d,(hl)
        pop hl
        ret c
        push de
        ret

.statement:
        db "IMAGE",0
        dw changeImage
;        db "VSTREAM",0
;        dw videoStream
        db 0

; send arguments, command, filename, end with ":"
changeImage:
        DEBUGMESSAGE "changeImage"
        push hl
        call sendRegisters
        ld (hl),C_CHANGEIMAGE
        pop hl

call_exit:
        DEBUGMESSAGE "call_exit"
.loop:  ld a,(hl)
        ld (usbWritePage1),a
        cp ":"
        ret z
        or a
        ret z
        inc hl
        jr .loop

supportedMedia:

        MAKEDPB $f8, 512, 2, 112, 1 * 80 * 9, 2, 2      ; 360 kB (1 side * 80 tracks * 9 tracks/sector)
DEFDPB: MAKEDPB $f9, 512, 2, 112, 2 * 80 * 9, 3, 2      ; 720 kB
        MAKEDPB $fa, 512, 2, 112, 1 * 80 * 8, 1, 2      ; 320 kB
        MAKEDPB $fb, 512, 2, 112, 2 * 80 * 8, 2, 2      ; 640 kB
        MAKEDPB $fc, 512, 1, 64,  1 * 40 * 9, 2, 2      ; 180 kB
        MAKEDPB $fd, 512, 2, 112, 2 * 40 * 9, 2, 2      ; 360 kB
        MAKEDPB $fe, 512, 1, 64,  1 * 40 * 8, 1, 2      ; 160 kB
        MAKEDPB $ff, 512, 2, 112, 2 * 40 * 8, 1, 1      ; 320 kB
