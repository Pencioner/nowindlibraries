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

        call enableNowindPage0          ; clear hostToMSXFifo by reading 4Kb of random data
        ld bc,4096
.loop:  ld a,(usbrd)
        dec bc
        ld a,b
        or c
        jr nz,.loop
        call restorePage0

        ld h,HIGH usbwr
        ld (hl),$af                     ; INIHRD command
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
        ld h,HIGH usbrd
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
; Output    F   Carry set when not succesfull
;           A   Error code
;           B   Number of remaining sectors (TODO: check! even without error?)

        DEBUGMESSAGE "DSKIO"
        push af
        call checkWorkArea
        jp z,ROMDISK_DSKIO
        pop af

        call sendRegisters
        ld (hl),C_DSKIO
        jr c,dskioWrite                 ; read or write?

dskioRead:
        rlca                            ; tranfer address < 0x8000 ?
        jr c,.page2and3

        DEBUGMESSAGE "blockRead01"
        ld b,HIGH usb2
        ld hl,blockRead2
        call executeCommandNowindInPage2
        DEBUGMESSAGE "back"
        DEBUGDUMPREGISTERS
        ret

.page2and3:
        DEBUGMESSAGE "blockRead23"
        ld b,HIGH usbrd
        ld hl,blockRead
        jp executeCommandNowindInPage0

dskioWrite:
        DEBUGMESSAGE "dskwrite"
        rlca
        jr c,.page2and3

        ;call enableNowindPage2 (todo: make common routine?)
        call getSlotPage2               ; save current slot page 2
        ld ixh,a
        call getSlotPage1
        ld ixl,a
        ld h,$80
        call ENASLT                     ; nowind in page 2
        jp .page2

        PHASE $ + $4000
.page2:
        ld a,(RAMAD1)
        ld h,$40
        call ENASLT                     ; ram in page 1

        call writeLoop01
        push af

        ld a,ixl
        ld h,$40
        call ENASLT                     ; restore nowind in page 1
        jp .page1

        DEPHASE
.page1:
        ld a,ixh
        ld h,$80
        call ENASLT
        pop af
        ret c                           ; return error (error code in a)
        ret pe                          ; host returns 0xfe when data for page 2/3 is available
        ;DEBUGMESSAGE "doorgaan!"

.page2and3:
        DEBUGMESSAGE "p2&3"
        call enableNowindPage0
        call .writeLoop23
        jp restorePage0

.writeLoop23:
        ;DEBUGMESSAGE "writeLoop23"

        ld h,HIGH usbrd
        call getHeader
        ret c                           ; exit (not ready)
        or a
        ret m                           ; exit (no error)
        jr nz,.error

        ;DEBUGMESSAGE "send23"
        ld e,(hl)                       ; address
        ld d,(hl)
        ld c,(hl)                       ; number of bytes
        ld b,(hl)
        ld a,(hl)                       ; block sequence number

        ;DEBUGDUMPREGISTERS
        ex de,hl
        ld de,usbwr
        ld (de),a                       ; mark block begin
        ldir
        ld (de),a                       ; mark block end
        jr .writeLoop23

.error: scf
        ld a,(hl)                       ; get error code
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

        push hl
        ;call sendRegisters
        ;ld (hl),C_DSKCHG
        ;call enableNowindPage0
        ;ld h,HIGH usbrd
        ;call getHeader

    SEND_CMD_AND_WAIT C_DSKCHG

        ld c,(hl)               ; media descriptor (when disk was changed)
        push af
        push bc
        call restorePage0
        pop bc
        pop af
        pop hl
        ret c           ; not ready
        or a
        ld b,1
        ret z           ; not changed
        ld b,c
        call GETDPB
        ld a,10
        ret c
        ld b,255
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
        ;jr z,.hddImage

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

        ;call sendRegisters
        ;ld (hl),C_GETDPB
        ;call enableNowindPage0
        ;ld h,HIGH usbrd
        ;call getHeader
    SEND_CMD_AND_WAIT C_GETDPB
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
        ifdef MSXDOS2
        ld hl,.noFormat
        else
        ld hl,0                         ; no choice
        endif
        ret

.noFormat:
        db 0

DSKFMT:
        scf
        ld a,16                         ; other error
        ret

        PHASE $ + $4000


writeLoop01:
        ld h,HIGH usb2
        call getHeader + $4000
        ret c                           ; exit (not ready)
        or a
        ret m                           ; exit (no error)
        jr nz,.error

        DEBUGMESSAGE "send01"
        ld e,(hl)                       ; address
        ld d,(hl)
        ld c,(hl)                       ; number of bytes
        ld b,(hl)
        ld a,(hl)                       ; block sequence number

        ex de,hl
        ld de,usb2
        ld (de),a                       ; mark block begin
        ldir
        ld (de),a                       ; mark block end
        jr writeLoop01

.error: scf
        ld a,(hl)                       ; get error code
        ret

        DEPHASE


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
        db "VSTREAM",0
        dw videoStream
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
        ld (usbwr),a
        cp ":"
        ret z
        or a
        ret z
        inc hl
        jr .loop

videoStream:
        push hl
        include "vram.asm"
        pop hl
        jp call_exit

; hl points to text
printVdpText2:
        push af
.loop:  ld a,(hl)
        out ($98),a
        inc hl
        or a
        jr nz,.loop
        pop af
        ret

; in:  hl = callback to execute
;      bc = arguments for callback
; out: bc = callback return data
; changed: all
; requirements: stack available
executeCommandNowindInPage0:
        push hl
        push bc
        call enableNowindPage0
        ld h,HIGH usbrd
        call getHeader
        jr c,.exit      ; timeout occurred?

        pop bc
        ld hl,.restorePage
        ex (sp),hl
        jp (hl)     ; jump to callback, a = first data read by getHeader, ret will resume at .restorePage

.exit:  call .restorePage
        pop hl
        pop bc
        ret

.restorePage:
        push bc
        call restorePage0
        pop bc
        ret

executeCommandNowindInPage2:
        push hl
        push bc

        DEBUGMESSAGE "1"
        call getSlotPage2               ; enable nowind in page 2
        ld ixl,a
        call getSlotPage1
        ld ixh,a                        ; ixh = current nowind slot
        ld h,$80
        call ENASLT
        jp .page2

        PHASE $ + $4000
.page2:
        DEBUGMESSAGE "2"
        ld a,(RAMAD1)                   ; enable RAM in page 1
        ld h,$40
        call ENASLT

        ld h,HIGH usb2
        call getHeader + $4000
        jr c,.exit      ; timeout occurred?

        pop bc
        ld hl,.restorePage
        ex (sp),hl
        DEBUGMESSAGE "3"
        jp (hl)     ; jump to callback, a = first data read by getHeader, ret will resume at .restorePage

.exit:  pop bc
        pop hl
; reg_af moet bewaard!
.restorePage:
        ld a,ixh                        ; enable nowind in page 1
        ld h,$40
        call ENASLT
        jp .page1

        DEPHASE
.page1:
        ld a,ixl
        ld h,$80
        call ENASLT                     ; restore page 2
        ret

supportedMedia:

.f8:    MAKEDPB $f8, 512, 2, 112, 1 * 80 * 9, 2, 2      ; 360 kB (1 side * 80 tracks * 9 tracks/sector)
.def:   MAKEDPB $f9, 512, 2, 112, 2 * 80 * 9, 3, 2      ; 720 kB
        MAKEDPB $fa, 512, 2, 112, 1 * 80 * 8, 1, 2      ; 320 kB
        MAKEDPB $fb, 512, 2, 112, 2 * 80 * 8, 2, 2      ; 640 kB
        MAKEDPB $fc, 512, 1, 64,  1 * 40 * 9, 2, 2      ; 180 kB
        MAKEDPB $fd, 512, 2, 112, 2 * 40 * 9, 2, 2      ; 360 kB
        MAKEDPB $fe, 512, 1, 64,  1 * 40 * 8, 1, 2      ; 160 kB
        MAKEDPB $ff, 512, 2, 112, 2 * 40 * 8, 1, 1      ; 320 kB

; WARNING: in some cases DEFDPB-1 is expected!
DEFDPB  equ supportedMedia.def
