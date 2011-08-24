; init.asm contains initialization and startup code 

        include "tracer.asm"

@nowindInit:                            ; this label should be accessible from others banks as well, hence the @
        call flashWriter
        call installTracer

        DEBUGMESSAGE "nowindInit"
        ld a,(IDBYTE_2D)
        or a
        push af
        call z,INITXT                   ; SCREEN 0 (MSX1)
        pop af
        ld ix,SDFSCR                    ; restore screen mode from clockchip (on MSX2 and higher)
        call nz,EXTROM

        call PRINTTEXT
        db "Nowind Interface v4.2"
        ifdef DEBUG
        db " [beta]"
        endif
        db 0

        call enableNowindPage0          ; clear hostToMSXFifo by reading 4Kb of random data
        ld bc,4096
.loop:  ld a,(usbReadPage0)
        dec bc
        ld a,b
        or c
        jr nz,.loop
        call restorePage0
        
getBootArgs:
        DEBUGMESSAGE "Any commands?"
        call enableNowindPage0
        ld c,0                      ; c=0 means reset startup queue index
.loop:  ld b,0                      ; b=0 means request startup command
        call sendRegisters
        ld (hl),C_CMDREQUEST
        call getHeaderInPage0
        jr c,noNextCommand

        ld d,(hl)
        ld d,(hl)
        ld d,(hl)

        ; todo: read command here
        and a
        jr z,noNextCommand
        DEBUGMESSAGE "Got cmd!"

        ld a,(hl)
        ;cp 1
        ;jr z, Com

        ld c,1
        jr .loop

noNextCommand:
        call restorePage0
        DEBUGMESSAGE "End of startup cmds"

        ld a,(IDBYTE_2D)                ; send MSX version to host
        call sendRegisters
        ld (hl),C_GETDOSVERSION
        call enableNowindPage0
        call getHeaderInPage0

        call restorePage0
        jp nc,gotHostReply                

        ; no reply (host not connected?)
        ld a,(IDBYTE_2D)
        or a
        jr z, bootMSXDOS1           ; on MSX1 boot DOS1
        cp 3
        jr z, bootMSXDOS1           ; on MSX Turbo R disable DOS2 because it has its own DOS2.xx roms        
                                    ; otherwise requested DOS version in A (1 = dos1, 2, = dos2)
bootMSXDOS2:            
        DEBUGMESSAGE "Booting DOS2"            
        jp  $47d6                   ; address of ROMINIT in DOS2

gotHostReply:                                        
        DEBUGDUMPREGISTERS
        cp 1
        jr nz,bootMSXDOS2

bootMSXDOS1:
        DEBUGMESSAGE "Booting DOS1"
        ld hl,$576f                     ; address of ROMINIT in DOS1
        push hl
        ld a,4
        jp switchBank


        

